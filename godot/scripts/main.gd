extends Node3D

## Climate-lab controls. PlanetSim owns the planet; this script chooses the
## view and run setup, and sends live forcing through validated commands.

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
var _yaw: float = 0.0
var _pitch: float = 0.3
var _view_given := false # --yaw or --pitch; otherwise start on the day side

# The camera's light: a soft fill beside the planet's own sun with day/night
# shading, the only light without it.
const FILL_WITH_SUN := 0.25
const FILL_ALONE := 0.8
# Start the view this far from the subsolar point, so relief shades.
const DAY_SIDE_OFFSET := 0.6 # rad
var _distance: float = 3.2 # the opening view's distance from the centre, in radii
var _selected_cell: int = -1
var _live_water_cycle: int = -1 # -1 the preset's, 0 off, 1 on
var _screenshot_path: String = ""
var _quit_after_screenshot: bool = false
var _screenshot_started: bool = false
var _capture_generation: int = 0
var _capture_in_progress := false
var _capture_series_directory := ""
var _capture_series_active := false
var _capture_series_cancelled := false
var _capture_focus_cell := -1

@onready var _planet = $Planet
@onready var _globe: Node3D = $CameraPivot # scripts/globe_camera.gd
@onready var _camera: Camera3D = $CameraPivot/Camera3D
@onready var _fill: DirectionalLight3D = $CameraPivot/Camera3D/Light
@onready var _hud = $Hud


func _ready() -> void:
	_hud.action.connect(_ui_action)
	_hud.set_help("Camera\nDrag: turn the globe\nShift+drag: heading and tilt\nWheel: zoom at cursor\nRight-click: inspect a cell\n\nPresentation\n1–9: views and overlays\nS: switch style   N: day/night   C: clouds\n[ ] (or ^ $): relief −/+   F12: screenshot\n\nRun\nL: toggle live climate   Space: pause   +/−: speed\nR: new seed   P: next preset   PgUp/PgDn: resolution\n\nF1: Help   Esc: close panel\n\nSolar forcing in Controls affects only live climate at the next simulation step. Recorded playback is read-only.")
	var view := 0
	var style := "stylised"
	var presentation_record := ""
	var live := false
	var day_night := true
	var relief_override := -1.0
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
			"water": _live_water_cycle = 1 if parts[1].to_lower() in ["1", "true", "yes"] else 0
			"screenshot": _screenshot_path = parts[1]
			"quit-after-screenshot": _quit_after_screenshot = \
				parts[1].to_lower() in ["1", "true", "yes"]
			"yaw":
				_yaw = parts[1].to_float()
				_view_given = true
			"pitch":
				_pitch = parts[1].to_float()
				_view_given = true
			"distance": _distance = parts[1].to_float()
			"day-night": day_night = parts[1].to_lower() in ["1", "true", "yes"]
			"relief-exaggeration": relief_override = parts[1].to_float()
	_generate()
	if relief_override >= 0.0:
		_planet.set_relief_exaggeration(relief_override)
	_planet.set_style(style)
	if not presentation_record.is_empty():
		_planet.load_presentation_record(presentation_record)
	elif live:
		_planet.start_live_run(0, _live_water_cycle)
	_planet.set_view_mode(view)
	_planet.set_day_night_shading(day_night)
	_globe.planet = _planet
	_globe.camera = _camera
	if not _view_given:
		_face_day_side()
	_update_lighting()
	_update_camera()


func _face_day_side() -> void:
	var sun: Vector3 = _planet.get_sun_direction()
	_pitch = clampf(asin(clampf(sun.y, -1.0, 1.0)), -1.2, 1.2)
	_yaw = atan2(sun.x, sun.z) + DAY_SIDE_OFFSET


func _update_lighting() -> void:
	_fill.light_energy = FILL_WITH_SUN if _planet.get_day_night_shading() else FILL_ALONE


func _generate() -> void:
	var resume_live: bool = _planet.has_live_run()
	var start := Time.get_ticks_msec()
	_planet.rebuild(subdivision, 6_371_000.0, world_seed, preset)
	_globe.set_cell_spacing(sqrt(4.0 * PI / (10.0 * pow(4.0, _planet.get_subdivision()) + 2.0)))
	_selected_cell = -1
	_hud.set_planet_setup(world_seed, preset, subdivision)
	if resume_live:
		_planet.start_live_run(0, _live_water_cycle)
	print("generated L%d seed %d (%s) in %d ms" % [subdivision, world_seed, preset, Time.get_ticks_msec() - start])
	_hud.log_event("Action", "Planet generated: L%d seed %d (%s)" % [subdivision, world_seed, preset])


func _process(delta: float) -> void:
	if _planet.has_live_run():
		if not _paused:
			_presentation_frame_accumulator += delta * live_steps_per_second
			var requested_steps := floori(_presentation_frame_accumulator)
			if requested_steps > 0:
				_planet.request_live_steps(requested_steps)
				_presentation_frame_accumulator -= requested_steps
		if _planet.poll_live_frame():
			_hud.log_event("Simulation", "tick %d · state %s" % [
				_planet.get_simulation_tick(), _planet.get_live_state_hash()])
		for event in _planet.take_live_events():
			_hud.log_event("Simulation", event)
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
	if not _screenshot_path.is_empty() and not _screenshot_started:
		var content_ready: bool = (not _planet.has_live_run() \
			or _planet.get_simulation_tick() > 0) \
			and int(_planet.get_terrain_tile_stats().get("pending", 0)) == 0
		if content_ready and not _planet.is_live_run_busy():
			_screenshot_started = true
			_capture_screenshot.call_deferred(_screenshot_path, _quit_after_screenshot)


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_ESCAPE and _hud.is_drawer_open():
			_hud.close_drawer()
			return
		if event.keycode == KEY_F1:
			_hud.toggle_tab("Help")
			return
	if _globe.handle_input(event):
		return
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_RIGHT and event.pressed:
			_select_cell(event.position)
	elif event is InputEventKey and event.pressed and not event.echo:
		# Views by key position, not character: AZERTY's top row types
		# & é " ' ( - è _ ç. The keypad digits work too. With AltGr, Alt or
		# Ctrl held the row types other characters, so it selects no view.
		var physical_view: int = event.physical_keycode - KEY_1
		var keypad_view: int = event.keycode - KEY_KP_1
		var modified: bool = event.alt_pressed or event.ctrl_pressed
		# Relief by key position too: the two keys right of P ([ ] on
		# QWERTY, ^ $ on AZERTY).
		if event.physical_keycode == KEY_BRACKETLEFT:
			_planet.set_relief_exaggeration(_planet.get_relief_exaggeration() / 1.5)
			_hud.log_event("Action", "Relief ×%.1f" % _planet.get_relief_exaggeration())
			return
		if event.physical_keycode == KEY_BRACKETRIGHT:
			_planet.set_relief_exaggeration(maxf(1.0, _planet.get_relief_exaggeration() * 1.5))
			_hud.log_event("Action", "Relief ×%.1f" % _planet.get_relief_exaggeration())
			return
		if physical_view >= 0 and physical_view < 9 and not modified:
			_planet.set_view_mode(physical_view)
			_hud.log_event("Action", "View: %s" % _planet.get_view_mode_name())
			return
		if keypad_view >= 0 and keypad_view < 9:
			_planet.set_view_mode(keypad_view)
			_hud.log_event("Action", "View: %s" % _planet.get_view_mode_name())
			return
		match event.keycode:
			KEY_S:
				_planet.next_style()
			KEY_N:
				_planet.set_day_night_shading(not _planet.get_day_night_shading())
				_update_lighting()
			KEY_C:
				_planet.set_clouds_visible(not _planet.get_clouds_visible())
			KEY_L:
				if _planet.has_live_run():
					_planet.stop_live_run()
				else:
					if _planet.has_presentation_record():
						_planet.clear_presentation_record()
					_presentation_frame_accumulator = 0.0
					_planet.start_live_run(0, _live_water_cycle)
			KEY_R:
				world_seed = randi()
				_generate()
			KEY_P:
				preset = PRESETS[(PRESETS.find(preset) + 1) % PRESETS.size()]
				_generate()
			KEY_PAGEUP:
				subdivision = mini(7, subdivision + 1)
				_generate()
			KEY_PAGEDOWN:
				subdivision = maxi(2, subdivision - 1)
				_generate()
			KEY_SPACE:
				_paused = not _paused
			KEY_F12:
				var filename := "planet_%d.png" % Time.get_ticks_msec()
				_capture_screenshot.call_deferred(
					ProjectSettings.globalize_path("user://captures/" + filename), false, true)
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
		if event.keycode in [KEY_S, KEY_N, KEY_C, KEY_L, KEY_P, KEY_R,
			KEY_PAGEUP, KEY_PAGEDOWN, KEY_SPACE, KEY_EQUAL, KEY_KP_ADD, KEY_MINUS, KEY_KP_SUBTRACT]:
			_hud.log_event("Action", "Shortcut: %s" % OS.get_keycode_string(event.keycode))


func _ui_action(kind: String, value: Variant) -> void:
	match kind:
		"style": _planet.set_style(value)
		"clouds": _planet.set_clouds_visible(value)
		"day_night":
			_planet.set_day_night_shading(value)
			_update_lighting()
		"relief": _planet.set_relief_exaggeration(value)
		"pause": _paused = value
		"live":
			if value and not _planet.has_live_run():
				if _planet.has_presentation_record():
					_planet.clear_presentation_record()
				_planet.start_live_run(0, _live_water_cycle)
			elif not value:
				_planet.stop_live_run()
		"speed":
			if _planet.has_live_run():
				live_steps_per_second = value
			elif _planet.has_presentation_record():
				presentation_frames_per_second = value
			else:
				simulated_hours_per_second = value
		"regenerate":
			if not value.seed.is_valid_int():
				_hud.log_event("Error", "Seed must be an integer")
				return
			world_seed = value.seed.to_int()
			preset = value.preset
			subdivision = value.level
			_generate()
		"solar":
			if _planet.queue_live_solar_factor(value):
				_hud.log_event("Action", "Solar factor %.3f queued for the next live step" % value)
			else:
				_hud.log_event("Error", "Solar factor requires a live run and a value in (0, 10]")
		"capture":
			var filename := "planet_%d.png" % Time.get_ticks_msec()
			_capture_screenshot.call_deferred(
				ProjectSettings.globalize_path("user://captures/" + filename), false, true)
		"capture_series":
			_capture_series.call_deferred()
		"cancel_capture_series":
			_capture_series_cancelled = true
	if kind not in ["regenerate", "solar", "capture", "capture_series", "cancel_capture_series"]:
		_hud.log_event("Action", "%s: %s" % [kind.replace("_", " "), str(value)])


func _capture_series() -> void:
	if _capture_in_progress or _capture_series_active:
		_hud.log_event("Error", "Another capture is in progress")
		return
	_capture_series_active = true
	_capture_series_cancelled = false
	var pose: Dictionary = _globe.save_view()
	var was_paused := _paused
	var hud_visible: bool = _hud.visible
	var focus: Vector3 = _planet.get_cell_direction(_selected_cell) if _selected_cell >= 0 else \
		Vector3(pose.focus[0], pose.focus[1], pose.focus[2])
	var coastal_cell: int = _planet.find_coastal_cell(focus)
	_capture_focus_cell = coastal_cell
	if coastal_cell >= 0:
		focus = _planet.get_coastal_focus(focus)
	var directory := ProjectSettings.globalize_path(
		"user://captures/series_%d_%d" % [int(Time.get_unix_time_from_system()), Time.get_ticks_msec()])
	_capture_series_directory = directory
	DirAccess.make_dir_recursive_absolute(directory)
	_paused = true
	_hud.log_event("Action", "Capturing three views near coastal cell %d: %s" % [coastal_cell, directory])
	for preset_name in ["orbit", "coast", "local"]:
		if _capture_series_cancelled:
			break
		_hud.set_capture_progress("Preparing %s view…" % preset_name, true)
		var range_radii: float = maxf(3.2,
			1.8 * _globe.planet_band_altitude_m() / 6_371_000.0) if preset_name == "orbit" else \
			_globe.range_for_altitude(100_000.0 if preset_name == "coast" else 1_000.0)
		_globe.look_at_direction(focus, range_radii)
		var started := Time.get_ticks_msec()
		while int(_planet.get_terrain_tile_stats().get("pending", 0)) > 0 \
				and Time.get_ticks_msec() - started < 12_000 and not _capture_series_cancelled:
			_hud.set_capture_progress("%s: streaming terrain (%d pending)…" % [
				preset_name, int(_planet.get_terrain_tile_stats().get("pending", 0))], true)
			await get_tree().process_frame
		if _capture_series_cancelled:
			break
		await get_tree().process_frame
		await get_tree().process_frame
		if int(_planet.get_terrain_tile_stats().get("pending", 0)) > 0:
			_hud.log_event("Action", "%s: captured with terrain still streaming" % preset_name)
		var path := directory.path_join(preset_name + ".png")
		_hud.set_capture_progress("Saving %s image…" % preset_name, true)
		_capture_screenshot(path, false, true)
		while _capture_in_progress and Time.get_ticks_msec() - started < 30_000:
			await get_tree().process_frame
		if not FileAccess.file_exists(path):
			_hud.log_event("Error", "Failed to capture " + preset_name)
			break
		_hud.set_capture_progress("Saved %s; next view…" % preset_name, true)
	_globe.restore_view(pose)
	_paused = was_paused
	_hud.visible = hud_visible
	_capture_series_active = false
	_capture_focus_cell = -1
	_hud.set_capture_progress("Cancelled" if _capture_series_cancelled else "Saved to " + directory, false)
	_hud.log_event("Action", "Capture series %s: %s" % [
		"cancelled" if _capture_series_cancelled else "finished", directory])


## Places the view at once from _yaw, _pitch and _distance (the opening
## view, and --yaw, --pitch, --distance): looking down at the point in that
## direction from that distance from the centre.
func _update_camera() -> void:
	var direction := Vector3(cos(_pitch) * sin(_yaw), sin(_pitch), cos(_pitch) * cos(_yaw))
	_globe.look_at_direction(direction, maxf(_distance - 1.0, 0.0))


func _capture_screenshot(path: String, quit_after: bool, hide_ui: bool = false) -> void:
	if _capture_in_progress:
		return
	_capture_in_progress = true
	_capture_generation += 1
	var generation := _capture_generation
	var restore_ui: bool = hide_ui and _hud.visible
	if restore_ui:
		_hud.visible = false
		# Allow the hidden controls to reach the render server before reading
		# back the completed frame, not the previous frame with the drawer.
		await get_tree().process_frame
	RenderingServer.frame_post_draw.connect(
		_save_screenshot.bind(path, quit_after, generation, restore_ui), CONNECT_ONE_SHOT)
	get_tree().create_timer(15.0).timeout.connect(
		_screenshot_timeout.bind(quit_after, generation), CONNECT_ONE_SHOT)


func _save_screenshot(path: String, quit_after: bool, generation: int, restore_ui: bool) -> void:
	if generation != _capture_generation:
		return
	_capture_in_progress = false
	_capture_generation += 1
	var succeeded := false
	var image := get_viewport().get_texture().get_image()
	if image == null or image.is_empty():
		push_error("active display driver did not produce a screenshot")
		_hud.log_event("Error", "Screenshot failed: no image from display driver")
	else:
		var absolute_path := path if path.is_absolute_path() else ProjectSettings.globalize_path(path)
		var directory := absolute_path.get_base_dir()
		if not directory.is_empty():
			DirAccess.make_dir_recursive_absolute(directory)
		var error := image.save_png(absolute_path)
		if error == OK:
			print("render screenshot written: ", absolute_path)
			var pose: Dictionary = _globe.save_view()
			var metadata := {
				"seed": _planet.get_seed(), "preset": _planet.get_preset(),
				"subdivision": _planet.get_subdivision(), "style": _planet.get_style(),
				"view": _planet.get_view_mode_name(), "day": _planet.get_simulation_time() / 86400.0,
				"altitude_m": _globe.get_altitude_m(), "band": _globe.get_current_band(),
				"focus_unit": pose.focus, "range_radii": pose.range,
				"heading_rad": pose.heading, "tilt_override_rad": pose.tilt,
				"selected_cell": _selected_cell, "coastal_focus_cell": _capture_focus_cell,
				"live_state_hash": _planet.get_live_state_hash(),
				"terrain_tiles_pending": int(_planet.get_terrain_tile_stats().get("pending", 0)),
			}
			var sidecar := FileAccess.open(absolute_path + ".json", FileAccess.WRITE)
			if sidecar != null:
				sidecar.store_string(JSON.stringify(metadata, "  ") + "\n")
			else:
				push_warning("could not write screenshot metadata: " + absolute_path + ".json")
			_hud.log_event("Action", "Capture saved: " + absolute_path)
			succeeded = true
		else:
			push_error("failed to save screenshot %s (error %d)" % [absolute_path, error])
			_hud.log_event("Error", "Screenshot could not be saved: " + absolute_path)
	if restore_ui:
		_hud.visible = true
	if quit_after:
		get_tree().quit(0 if succeeded else 1)


func _screenshot_timeout(quit_after: bool, generation: int) -> void:
	if generation != _capture_generation:
		return
	_capture_generation += 1
	_capture_in_progress = false
	_hud.visible = true
	push_error("timed out waiting for a completed frame to capture")
	_hud.log_event("Error", "Screenshot timed out")
	if quit_after:
		get_tree().quit(1)


func _select_cell(screen_position: Vector2) -> void:
	# The ground as drawn, at every altitude (R1-04 step 6).
	var ground = _globe.pick(screen_position)
	_selected_cell = _planet.find_cell(ground) if ground != null else -1


func _altitude_text(metres: float) -> String:
	return "%.0f m" % metres if metres < 10_000.0 else "%.0f km" % (metres / 1000.0)


func _channel_reading(name: String, suffix: String, scale: float = 1.0) -> String:
	if _selected_cell < 0 or not _planet.has_channel(name):
		return "%s unavailable" % name.replace("_", " ")
	var value: float = _planet.get_channel_value(_selected_cell, name) * scale
	return "%s %.2f%s" % [name.replace("_", " "), value, suffix]


func _wind_reading() -> String:
	if _selected_cell < 0 or not _planet.has_channel("wind"):
		return "wind unavailable"
	var wind: Vector2 = _planet.get_vector_channel_value(_selected_cell, "wind")
	return "wind %.1f m/s (E %.1f, N %.1f)" % [wind.length(), wind.x, wind.y]


func _precipitation_reading() -> String:
	if _selected_cell < 0 or not _planet.has_channel("precipitation"):
		return "precipitation unavailable"
	var normalized: float = _planet.get_channel_value(_selected_cell, "precipitation")
	return "precipitation %.1f mm/day" % (50.0 * normalized)


func _update_hud() -> void:
	var days: float = _planet.get_simulation_time() / 86400.0
	var legend := ""
	if _planet.get_view_mode() == 5:
		legend = "cyan: catchment/path   magenta: filled depression   pale cyan: coastal outlet"
	elif _planet.get_view_mode() == 7:
		legend = "wind: streak direction follows flow   cyan slow   amber 60+ m/s"
	elif _planet.get_view_mode() == 8:
		legend = "precipitation: dark dry   blue light   cyan 50+ mm/day"
	var playback := "live%s" % (" · solving" if _planet.is_live_run_busy() else "") \
		if _planet.has_live_run() else "frame %d/%d" % [
		_planet.get_presentation_frame() + 1, _planet.get_presentation_frame_count()] \
		if _planet.has_presentation_record() else "orbit preview"
	var selection := "Right-click a place on the planet to inspect its cell.\n\nNo cell selected."
	if _selected_cell >= 0:
		var direction: Vector3 = _planet.get_cell_direction(_selected_cell)
		selection = "\n".join([
			"Cell %d   %.1f° N   %.1f° E" % [
				_selected_cell, rad_to_deg(asin(direction.y)), rad_to_deg(atan2(direction.x, direction.z))],
			"Land %.1f%%   mean elevation %.0f m above sea" % [
				100.0 * _planet.get_cell_land_fraction(_selected_cell),
				_planet.get_cell_elevation_m(_selected_cell)],
			"",
			_channel_reading("temperature_anomaly", " (±3σ)"),
			_channel_reading("snow_cover", "%", 100.0),
			_channel_reading("sea_ice", "%", 100.0),
			_channel_reading("cloud_cover", "%", 100.0),
			_channel_reading("cloud_thickness", " (normalised)"),
			_wind_reading(),
			_precipitation_reading(),
		])
	_hud.set_status("L%d · %s · day %.1f · %s · %s%s%s" % [
		_planet.get_subdivision(), _planet.get_view_mode_name(), days,
		_altitude_text(_globe.get_altitude_m()), playback, " · paused" if _paused else "",
		" · cell %d" % _selected_cell if _selected_cell >= 0 else ""])
	_hud.update_state({"style": _planet.get_style(), "clouds": _planet.get_clouds_visible(),
		"day_night": _planet.get_day_night_shading(), "pause": _paused,
		"live": _planet.has_live_run(), "relief": _planet.get_relief_exaggeration(),
		"speed": live_steps_per_second if _planet.has_live_run() else
			presentation_frames_per_second if _planet.has_presentation_record() else simulated_hours_per_second,
		"selection": selection + ("\n" + legend if not legend.is_empty() else "")})
