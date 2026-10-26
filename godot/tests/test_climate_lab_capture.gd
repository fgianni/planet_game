extends SceneTree

## A real display driver is required: the dummy headless renderer cannot
## read pixels. CI runs this under Xvfb/OpenGL, not with --headless.
func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var output := "/tmp/planetsim-ui-capture.png"
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--output="):
			output = argument.trim_prefix("--output=")
	var main: Node3D = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await process_frame
	main._paused = true
	var ui: CanvasLayer = main.get_node("Hud")
	ui.toggle_tab("Controls")
	assert(ui.is_drawer_open())
	main._capture_screenshot.call_deferred(output, false, true)
	var written := false
	for unused in 180:
		await process_frame
		if FileAccess.file_exists(output) and FileAccess.file_exists(output + ".json"):
			written = true
			break
	assert(written, "capture and metadata were not saved")
	assert(ui.visible and ui.is_drawer_open(), "capture must restore the drawer")
	var image := Image.new()
	assert(image.load(output) == OK and image.get_width() > 800)
	# The buttons occupy this part of the window when drawn; a clear capture
	# must show only the dark background there.
	var bright := 0
	for y in range(12, 40):
		for x in range(745, 900):
			var pixel := image.get_pixel(x, y)
			if maxf(pixel.r, maxf(pixel.g, pixel.b)) > 0.4:
				bright += 1
	assert(bright == 0, "saved view still contains the Help/Controls/Log buttons")
	var metadata: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(output + ".json"))
	assert(metadata.seed == main.world_seed and metadata.has("altitude_m"),
		"capture must include reproducible view metadata")
	var globe: Node3D = main.get_node("CameraPivot")
	var original_pose: Dictionary = globe.save_view()
	main._capture_series.call_deferred()
	var directory := ""
	var complete := false
	for unused in 4500:
		await process_frame
		directory = main._capture_series_directory
		if not directory.is_empty() and FileAccess.file_exists(directory.path_join("local.png.json")) \
				and ui.visible and not main._capture_in_progress and not main._capture_series_active:
			complete = true
			break
	assert(complete, "orbit/region/local capture did not finish")
	var shared_focus := []
	var shared_day := -1.0
	for band in ["orbit", "coast", "local"]:
		var path: String = directory.path_join(band + ".png")
		assert(FileAccess.file_exists(path), "missing %s capture" % band)
		var view: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(path + ".json"))
		var expected_band := "planet" if band == "orbit" else "region" if band == "coast" else "local"
		assert(view.seed == main.world_seed and view.band == expected_band,
			"capture preset did not reach the expected altitude band: %s" % band)
		if shared_focus.is_empty():
			shared_focus = view.focus_unit
			shared_day = view.day
		else:
			assert(view.focus_unit == shared_focus and view.day == shared_day,
				"capture presets changed focus or simulation time")
	assert(globe.save_view() == original_pose and main._paused,
		"capture presets changed the camera or playback state")
	main.free()
	print("clear-view capture pass: ", output)
	quit(0)
