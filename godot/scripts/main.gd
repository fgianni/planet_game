extends Node3D

## Presentation-only controls. PlanetSim owns the planet; this script only
## chooses what to generate, how fast simulated time advances and what to show.

@export var subdivision: int = 6
@export var world_seed: int = 20260928
@export var preset: String = "earth_like"
@export var simulated_hours_per_second: float = 2.0
@export var snapshot_updates_per_second: float = 12.0
@export var presentation_frames_per_second: float = 2.0
@export var live_steps_per_second: float = 2.0

const PRESETS := ["earth_like", "aqua_planet", "dead_rock"]

var _pending_simulation_ticks: float = 0.0
var _time_since_snapshot_s: float = 0.0
var _presentation_frame_accumulator: float = 0.0
var _paused: bool = false
var _dragging: bool = false
var _yaw: float = 0.0
var _pitch: float = 0.3
var _distance: float = 3.2
var _selected_cell: int = -1

@onready var _planet = $Planet
@onready var _pivot: Node3D = $CameraPivot
@onready var _camera: Camera3D = $CameraPivot/Camera3D
@onready var _hud: Label = $Hud/Info


func _ready() -> void:
	var view := 0
	var style := "stylised"
	var presentation_record := ""
	var live := false
	for argument in OS.get_cmdline_user_args():
		var parts: PackedStringArray = argument.trim_prefix("--").split("=", true, 1)
		if parts.size() != 2:
			continue
		match parts[0]:
			"seed": world_seed = parts[1].to_int()
			"subdivision": subdivision = parts[1].to_int()
			"preset": preset = parts[1]
			"view": view = parts[1].to_int()
			"style": style = parts[1]
			"presentation-record": presentation_record = parts[1]
			"live": live = parts[1].to_lower() in ["1", "true", "yes"]
			"yaw": _yaw = parts[1].to_float()
			"pitch": _pitch = parts[1].to_float()
			"distance": _distance = parts[1].to_float()
	_generate()
	_planet.set_style(style)
	if not presentation_record.is_empty():
		_planet.load_presentation_record(presentation_record)
	elif live:
		_planet.start_live_run()
	_planet.set_view_mode(view)
	_update_camera()


func _generate() -> void:
	var resume_live: bool = _planet.has_live_run()
	var start := Time.get_ticks_msec()
	_planet.rebuild(subdivision, 6_371_000.0, world_seed, preset)
	_selected_cell = -1
	if resume_live:
		_planet.start_live_run()
	print("generated L%d seed %d (%s) in %d ms" % [subdivision, world_seed, preset, Time.get_ticks_msec() - start])


func _process(delta: float) -> void:
	if _planet.has_live_run():
		if not _paused:
			_presentation_frame_accumulator += delta * live_steps_per_second
			var requested_steps := floori(_presentation_frame_accumulator)
			if requested_steps > 0:
				_planet.request_live_steps(requested_steps)
				_presentation_frame_accumulator -= requested_steps
		_planet.poll_live_frame()
	elif not _paused and _planet.has_presentation_record():
		_presentation_frame_accumulator += delta * presentation_frames_per_second
		while _presentation_frame_accumulator >= 1.0:
			_planet.advance_presentation_frame()
			_presentation_frame_accumulator -= 1.0
	elif not _paused:
		_pending_simulation_ticks += delta * simulated_hours_per_second * 60.0
	_time_since_snapshot_s += delta
	var update_interval_s := 1.0 / maxf(snapshot_updates_per_second, 1.0)
	if not _planet.has_live_run() and not _planet.has_presentation_record() \
			and _time_since_snapshot_s >= update_interval_s:
		var whole_ticks := floori(_pending_simulation_ticks)
		if whole_ticks > 0:
			_planet.advance_simulation_ticks(whole_ticks)
			_pending_simulation_ticks -= whole_ticks
			_time_since_snapshot_s = 0.0
	var simulated_years_per_wall_second := live_steps_per_second / 12.0 \
		if _planet.has_live_run() else presentation_frames_per_second / 12.0 \
		if _planet.has_presentation_record() else simulated_hours_per_second / (24.0 * 365.24219)
	_planet.advance_presentation(delta, simulated_years_per_wall_second)
	_update_hud()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT:
			_dragging = event.pressed
		elif event.button_index == MOUSE_BUTTON_RIGHT and event.pressed:
			_select_cell(event.position)
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
			KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7:
				_planet.set_view_mode(event.keycode - KEY_1)
			KEY_S:
				_planet.next_style()
			KEY_N:
				_planet.set_day_night_shading(not _planet.get_day_night_shading())
			KEY_L:
				if _planet.has_live_run():
					_planet.stop_live_run()
				else:
					if _planet.has_presentation_record():
						_planet.clear_presentation_record()
					_presentation_frame_accumulator = 0.0
					_planet.start_live_run()
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
				if _planet.has_live_run():
					live_steps_per_second *= 2.0
				elif _planet.has_presentation_record():
					presentation_frames_per_second *= 2.0
				else:
					simulated_hours_per_second *= 2.0
			KEY_MINUS, KEY_KP_SUBTRACT:
				if _planet.has_live_run():
					live_steps_per_second = maxf(0.125, live_steps_per_second / 2.0)
				elif _planet.has_presentation_record():
					presentation_frames_per_second = maxf(0.125, presentation_frames_per_second / 2.0)
				else:
					simulated_hours_per_second = maxf(0.125, simulated_hours_per_second / 2.0)


func _update_camera() -> void:
	_pivot.rotation = Vector3(-_pitch, _yaw, 0.0)
	_camera.position = Vector3(0.0, 0.0, _distance)


func _select_cell(screen_position: Vector2) -> void:
	var world_origin := _camera.project_ray_origin(screen_position)
	var world_direction := _camera.project_ray_normal(screen_position)
	var origin: Vector3 = _planet.to_local(world_origin)
	var direction: Vector3 = (_planet.to_local(world_origin + world_direction) - origin).normalized()
	var along: float = origin.dot(direction)
	var discriminant: float = along * along - (origin.length_squared() - 1.0)
	if discriminant < 0.0:
		_selected_cell = -1
		return
	var distance: float = -along - sqrt(discriminant)
	if distance < 0.0:
		distance = -along + sqrt(discriminant)
	_selected_cell = _planet.find_cell((origin + distance * direction).normalized()) \
		if distance >= 0.0 else -1


func _channel_reading(name: String, suffix: String, scale: float = 1.0) -> String:
	if _selected_cell < 0 or not _planet.has_channel(name):
		return "%s unavailable" % name.replace("_", " ")
	var value: float = _planet.get_channel_value(_selected_cell, name) * scale
	return "%s %.2f%s" % [name.replace("_", " "), value, suffix]


func _update_hud() -> void:
	var days: float = _planet.get_simulation_time() / 86400.0
	var legend := "cyan: catchment/path   magenta: filled depression   pale cyan: coastal outlet" \
		if _planet.get_view_mode() == 5 else ""
	var playback := "live climate%s  hash %s" % [
		" (solving)" if _planet.is_live_run_busy() else "",
		_planet.get_live_state_hash(),
	] if _planet.has_live_run() else "frame %d/%d" % [
		_planet.get_presentation_frame() + 1, _planet.get_presentation_frame_count()] \
		if _planet.has_presentation_record() else "orbit preview"
	var selection := "right-click: select a cell"
	if _selected_cell >= 0:
		selection = "cell %d   %s   %s   %s" % [
			_selected_cell,
			_channel_reading("temperature_anomaly", " (±3σ)"),
			_channel_reading("snow_cover", "%", 100.0),
			_channel_reading("sea_ice", "%", 100.0),
		]
	_hud.text = "\n".join([
		"PlanetSim  L%d  seed %d  preset %s" % [_planet.get_subdivision(), _planet.get_seed(), _planet.get_preset()],
		"view: %s   style: %s   day/night: %s   relief x%.0f" % [
			_planet.get_view_mode_name(),
			_planet.get_style(),
			"on" if _planet.get_day_night_shading() else "off",
			_planet.get_relief_exaggeration()],
		"plates %d   land %.1f %%   sea level %.0f m" % [
			_planet.get_plate_count(), 100.0 * _planet.get_land_fraction(), _planet.get_sea_level()],
		"day %.2f   %s%s" % [days, playback, "  (paused)" if _paused else ""],
		selection,
		"drainage: %d outlets   %d basins   %d depressions" % [
			_planet.get_drainage_outlet_count(), _planet.get_drainage_basin_count(),
			_planet.get_drainage_depression_count()],
		legend,
		"drag: rotate  right-click: select  wheel: zoom  1-7: style/overlays  S: switch style",
		"N: day/night  L: live climate  [ ]: relief",
		"R: new seed  P: preset  PgUp/PgDn: resolution  Space: pause  +/-: speed",
	])
