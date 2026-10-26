extends SceneTree

## Diagnostic capture: point the real renderer at the cloudiest live cell,
## without moving or inventing cloud state. Run under X11/OpenGL, not --headless.
func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var output := "/tmp/opencode/cloud-focus.png"
	var steps := 12
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--output="):
			output = argument.trim_prefix("--output=")
		elif argument.begins_with("--steps="):
			steps = maxi(1, argument.trim_prefix("--steps=").to_int())
	var main: Node3D = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	main.set_process(false)
	var planet: PlanetMeshNode = main.get_node("Planet")
	var globe: Node3D = main.get_node("CameraPivot")
	planet.start_live_run(1)
	for step in steps:
		planet.request_live_steps()
		var ready := false
		for unused in 1000:
			await create_timer(0.01).timeout
			if planet.poll_live_frame():
				ready = true
				break
		assert(ready, "no live cloud snapshot at step %d" % step)
	var covers: PackedFloat32Array = planet.get_channel_values("cloud_cover")
	var thicknesses: PackedFloat32Array = planet.get_channel_values("cloud_thickness")
	assert(covers.size() > 0 and covers.size() == thicknesses.size())
	var best := 0
	var mean := 0.0
	for index in covers.size():
		mean += covers[index] / covers.size()
		if covers[index] * thicknesses[index] > covers[best] * thicknesses[best]:
			best = index
	print("cloud focus cell %d: cover %.3f thickness %.3f, global cell mean cover %.3f" % [
		best, covers[best], thicknesses[best], mean])
	var direction: Vector3 = planet.get_cell_direction(best).normalized()
	globe.look_at_direction(direction, 1.2, 0.0)
	planet.set_day_night_shading(false)
	main._paused = true
	await process_frame
	await process_frame
	main._capture_screenshot.call_deferred(output, true, true)
