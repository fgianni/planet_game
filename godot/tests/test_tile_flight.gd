extends SceneTree

## R1-04 acceptance, V9 and V11 (ADR-0022 §5): a scripted flight on the
## main scene, from orbit down to 100 m above the ground and back, with the
## terrain tiles streaming as they would for a player.
##
## V9: every captured frame is searched for holes: pixels whose camera ray
## meets the planet well inside its silhouette, yet show the background.
## V11: frame times (wall without vsync, and the GPU's) are reported by
## altitude band; with --gate-fps the run fails if any band's 95th
## percentile misses 60 frames per second. Gate it on §31.9's reference
## machine at L6; a software renderer (CI) only reports.
##
## Pass --subdivision=N (the main scene's argument); --output=DIR keeps
## sample frames.

const FRAMES_DOWN := 900 # --frames=N changes it (CI runs a short flight)
const FRAMES_HOLD := 120
const CAPTURE_EVERY := 15
const STEP := 6 # pixels between samples, both ways
const BACKGROUND := Color(0.02, 0.02, 0.04)
const INSIDE := 0.995 # rays meeting a sphere this much smaller are well inside
const FRAME_BUDGET_MS := 16.7

var _failures := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var output := ""
	var gate_fps := false
	var frames_down := FRAMES_DOWN
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--output="):
			output = argument.trim_prefix("--output=")
		elif argument == "--gate-fps":
			gate_fps = true
		elif argument.begins_with("--frames="):
			frames_down = maxi(argument.trim_prefix("--frames=").to_int(), 30)
	if not output.is_empty():
		DirAccess.make_dir_recursive_absolute(output)
	var main: Node3D = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await process_frame
	var planet: PlanetMeshNode = main.get_node("Planet")
	var globe: Node3D = main.get_node("CameraPivot")
	var camera: Camera3D = main.get_node("CameraPivot/Camera3D")
	main.get_node("Hud").visible = false
	planet.set_day_night_shading(false)
	main._paused = true
	globe.set_process(false) # the flight sets the view each frame
	RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(), true)
	# Frame times are measured without vsync's cap.
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)

	var focus: Vector3 = globe.get_camera_position().normalized()
	var high := 3.0
	var low: float = globe.range_for_altitude(100.0)
	var times := {"planet": [], "region": [], "local": []}
	var gpu := {"planet": [], "region": [], "local": []}
	var holes_total := 0
	var frames_checked := 0
	var deepest := 0
	var path := []
	for frame in frames_down:
		path.append(exp(lerp(log(high), log(low), float(frame) / (frames_down - 1))))
	for frame in FRAMES_HOLD:
		path.append(low)
	var hold_end := path.size()
	for frame in frames_down:
		path.append(exp(lerp(log(low), log(high), float(frame) / (frames_down - 1))))
	var last := Time.get_ticks_usec()
	var captured_last := false
	var turn := 0.0
	var hold_start_ms := Time.get_ticks_msec()
	var frame := 0
	var settle := 0
	while frame < path.size():
		# Turning as it goes, by an angle in proportion to the altitude (as a
		# player's pan), so the view sweeps new ground; still while holding.
		if frame < frames_down or frame >= hold_end:
			turn += 0.002 * path[frame]
		globe.look_at_direction(focus.rotated(Vector3.UP, turn), path[frame], 0.0)
		await process_frame
		var now := Time.get_ticks_usec()
		var band: String = globe.get_current_band()
		# A capture reads the frame back from the GPU: that frame and the next
		# are not timed.
		if not captured_last:
			times[band].append((now - last) / 1000.0)
			gpu[band].append(RenderingServer.viewport_get_measured_render_time_gpu(root.get_viewport_rid()))
		captured_last = false
		last = now
		var stats: Dictionary = planet.get_terrain_tile_stats()
		deepest = maxi(deepest, int(stats.get("deepest_level", 0)))
		# At the bottom, hold until the tiles have settled (at most a minute).
		var settling := frame == hold_end - 1 and int(stats.get("pending", 0)) > 0
		if settling and settle == 0:
			hold_start_ms = Time.get_ticks_msec()
		if settling and Time.get_ticks_msec() - hold_start_ms < 60_000:
			settle += 1
		else:
			frame += 1
		if frame % CAPTURE_EVERY != 0:
			continue
		captured_last = true
		var image := root.get_texture().get_image()
		var holes := _holes(image, camera, globe.get_camera_position())
		frames_checked += 1
		if holes > 0:
			holes_total += holes
			push_error("frame %d at %.0f m: %d hole samples" % [frame, globe.get_altitude_m(), holes])
			if not output.is_empty():
				image.save_png(output.path_join("hole_%04d.png" % frame))
		elif not output.is_empty() and frame % (CAPTURE_EVERY * 20) == 0:
			image.save_png(output.path_join("flight_%04d.png" % frame))

	print("V9: %d frames checked, %d hole samples; deepest tile level %d; %d frames to settle at 100 m" % [
		frames_checked, holes_total, deepest, settle])
	if holes_total > 0:
		_failures += 1
	for band in ["planet", "region", "local"]:
		var wall := _percentiles(times[band])
		var g := _percentiles(gpu[band])
		print("V11 %s: %d frames; wall p50 %.1f p95 %.1f p99 %.1f ms; GPU p50 %.1f p95 %.1f ms" % [
			band, times[band].size(), wall[0], wall[1], wall[2], g[0], g[1]])
		if gate_fps and wall[1] > FRAME_BUDGET_MS:
			push_error("the %s band misses 60 fps: p95 %.1f ms" % [band, wall[1]])
			_failures += 1
	main.free()
	quit(1 if _failures > 0 else 0)


## Samples whose ray meets the planet well inside its silhouette but show
## the background colour.
func _holes(image: Image, camera: Camera3D, camera_position: Vector3) -> int:
	var count := 0
	for y in range(STEP / 2, image.get_height(), STEP):
		for x in range(STEP / 2, image.get_width(), STEP):
			var direction := camera.project_ray_normal(Vector2(x, y))
			var b := camera_position.dot(direction)
			var c := camera_position.length_squared() - INSIDE * INSIDE
			if b * b - c < 0.0 or -b - sqrt(b * b - c) < 0.0:
				continue
			var colour := image.get_pixel(x, y)
			if abs(colour.r - BACKGROUND.r) < 0.01 and abs(colour.g - BACKGROUND.g) < 0.01 \
					and abs(colour.b - BACKGROUND.b) < 0.015:
				count += 1
	return count


func _percentiles(values: Array) -> Array:
	if values.is_empty():
		return [0.0, 0.0, 0.0]
	var sorted := values.duplicate()
	sorted.sort()
	var pick := func(p: float) -> float: return sorted[mini(int(p * sorted.size()), sorted.size() - 1)]
	return [pick.call(0.5), pick.call(0.95), pick.call(0.99)]
