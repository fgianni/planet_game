extends SceneTree

## R1-05 (ADR-0030 Amendment 1): a frame of the main scene once the coast
## pages its view needs have arrived. Places the camera above the default
## focus at --altitude-km=N, waits until no page is pending or being remade
## (at most a minute), and saves --output=FILE. Pass --subdivision=N (the
## main scene's argument) and --style=map|stylised.


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var output := ""
	var altitude_km := 4000.0
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--output="):
			output = argument.trim_prefix("--output=")
		elif argument.begins_with("--altitude-km="):
			altitude_km = argument.trim_prefix("--altitude-km=").to_float()
	var main: Node3D = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await process_frame
	var planet: PlanetMeshNode = main.get_node("Planet")
	var globe: Node3D = main.get_node("CameraPivot")
	main.get_node("Hud").visible = false
	planet.set_day_night_shading(false)
	main._paused = true
	globe.set_process(false)
	var focus: Vector3 = globe.get_camera_position().normalized()
	var range: float = globe.range_for_altitude(altitude_km * 1000.0)
	var start := Time.get_ticks_msec()
	var stats: Dictionary = {}
	while Time.get_ticks_msec() - start < 60_000:
		globe.look_at_direction(focus, range, 0.0)
		await process_frame
		stats = planet.get_terrain_tile_stats()
		if Time.get_ticks_msec() - start > 500 and int(stats.get("pending", 0)) == 0 \
				and int(stats.get("coast_pages_pending", 0)) == 0 and int(stats.get("coast_remakes", 0)) == 0:
			break
	await process_frame
	await process_frame
	var image := root.get_texture().get_image()
	image.save_png(output)
	print("captured %s at %.0f km (band %s) after %.1f s: %d pages, %d pending, %d layers" % [
		output, altitude_km, globe.get_current_band(), (Time.get_ticks_msec() - start) / 1000.0,
		int(stats.get("coast_pages", 0)), int(stats.get("coast_pages_pending", 0)),
		int(stats.get("coast_layers", 0))])
	main.free()
	quit(0)
