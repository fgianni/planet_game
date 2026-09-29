extends Node3D

## Presentation-only controls. PlanetSim owns the planet; this script only
## chooses what to generate, how fast simulated time advances and what to show.

@export var subdivision: int = 6
@export var world_seed: int = 20260928
@export var preset: String = "earth_like"
@export var simulated_hours_per_second: float = 2.0
@export var snapshot_updates_per_second: float = 12.0

const PRESETS := ["earth_like", "aqua_planet", "dead_rock"]

var _pending_simulation_ticks: float = 0.0
var _time_since_snapshot_s: float = 0.0
var _paused: bool = false
var _dragging: bool = false
var _yaw: float = 0.0
var _pitch: float = 0.3
var _distance: float = 3.2

@onready var _planet = $Planet
@onready var _pivot: Node3D = $CameraPivot
@onready var _camera: Camera3D = $CameraPivot/Camera3D
@onready var _hud: Label = $Hud/Info


func _ready() -> void:
	var view := 0
	for argument in OS.get_cmdline_user_args():
		var parts: PackedStringArray = argument.trim_prefix("--").split("=", true, 1)
		if parts.size() != 2:
			continue
		match parts[0]:
			"seed": world_seed = parts[1].to_int()
			"subdivision": subdivision = parts[1].to_int()
			"preset": preset = parts[1]
			"view": view = parts[1].to_int()
			"yaw": _yaw = parts[1].to_float()
			"pitch": _pitch = parts[1].to_float()
			"distance": _distance = parts[1].to_float()
	_generate()
	_planet.set_view_mode(view)
	_update_camera()


func _generate() -> void:
	var start := Time.get_ticks_msec()
	_planet.rebuild(subdivision, 6_371_000.0, world_seed, preset)
	print("generated L%d seed %d (%s) in %d ms" % [subdivision, world_seed, preset, Time.get_ticks_msec() - start])


func _process(delta: float) -> void:
	if not _paused:
		_pending_simulation_ticks += delta * simulated_hours_per_second * 60.0
	_time_since_snapshot_s += delta
	var update_interval_s := 1.0 / maxf(snapshot_updates_per_second, 1.0)
	if _time_since_snapshot_s >= update_interval_s:
		var whole_ticks := floori(_pending_simulation_ticks)
		if whole_ticks > 0:
			_planet.advance_simulation_ticks(whole_ticks)
			_pending_simulation_ticks -= whole_ticks
		_time_since_snapshot_s = 0.0
	_update_hud()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT:
			_dragging = event.pressed
		elif event.button_index == MOUSE_BUTTON_WHEEL_UP and event.pressed:
			_distance = maxf(1.3, _distance * 0.9)
			_update_camera()
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN and event.pressed:
			_distance = minf(10.0, _distance * 1.1)
			_update_camera()
	elif event is InputEventMouseMotion and _dragging:
		_yaw -= event.relative.x * 0.006
		_pitch = clampf(_pitch + event.relative.y * 0.006, -1.5, 1.5)
		_update_camera()
	elif event is InputEventKey and event.pressed and not event.echo:
		match event.keycode:
			KEY_1, KEY_2, KEY_3, KEY_4, KEY_5:
				_planet.set_view_mode(event.keycode - KEY_1)
			KEY_N:
				_planet.set_day_night_shading(not _planet.get_day_night_shading())
			KEY_R:
				world_seed = randi()
				_generate()
			KEY_P:
				preset = PRESETS[(PRESETS.find(preset) + 1) % PRESETS.size()]
				_generate()
			KEY_BRACKETLEFT:
				_planet.set_relief_exaggeration(_planet.get_relief_exaggeration() / 1.5)
			KEY_BRACKETRIGHT:
				_planet.set_relief_exaggeration(maxf(1.0, _planet.get_relief_exaggeration() * 1.5))
			KEY_PAGEUP:
				subdivision = mini(7, subdivision + 1)
				_generate()
			KEY_PAGEDOWN:
				subdivision = maxi(2, subdivision - 1)
				_generate()
			KEY_SPACE:
				_paused = not _paused
			KEY_EQUAL, KEY_KP_ADD:
				simulated_hours_per_second *= 2.0
			KEY_MINUS, KEY_KP_SUBTRACT:
				simulated_hours_per_second = maxf(0.125, simulated_hours_per_second / 2.0)


func _update_camera() -> void:
	_pivot.rotation = Vector3(-_pitch, _yaw, 0.0)
	_camera.position = Vector3(0.0, 0.0, _distance)


func _update_hud() -> void:
	var days: float = _planet.get_simulation_time() / 86400.0
	var legend := "cyan: catchment/path   magenta: filled depression   pale cyan: coastal outlet" \
		if _planet.get_view_mode() == 4 else ""
	_hud.text = "\n".join([
		"PlanetSim  L%d  seed %d  preset %s" % [_planet.get_subdivision(), _planet.get_seed(), _planet.get_preset()],
		"view: %s   day/night: %s   relief x%.0f" % [
			_planet.get_view_mode_name(),
			"on" if _planet.get_day_night_shading() else "off",
			_planet.get_relief_exaggeration()],
		"plates %d   land %.1f %%   sea level %.0f m" % [
			_planet.get_plate_count(), 100.0 * _planet.get_land_fraction(), _planet.get_sea_level()],
		"day %.2f   %.2f sim h/s%s" % [days, simulated_hours_per_second, "  (paused)" if _paused else ""],
		"drainage: %d outlets   %d basins   %d depressions" % [
			_planet.get_drainage_outlet_count(), _planet.get_drainage_basin_count(),
			_planet.get_drainage_depression_count()],
		legend,
		"drag: rotate  wheel: zoom  1-5: view  N: day/night  [ ]: relief",
		"R: new seed  P: preset  PgUp/PgDn: resolution  Space: pause  +/-: speed",
	])
