extends SceneTree

## Coastal picking with resident pages, across the planet/terrain bands.
## The returned direction must ray-project back to the cursor on the
## page-following surface; this is not the rendered AA contour gate.

func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var main: Node3D = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await process_frame
	var planet: PlanetMeshNode = main.get_node("Planet")
	var globe: Node3D = main.get_node("CameraPivot")
	var camera: Camera3D = main.get_node("CameraPivot/Camera3D")
	globe.set_process(false)
	var focus := Vector3(0.73903392263802337, -0.67266602370869411, 0.036732570536116109).normalized()
	var pick_times_ms: Array[float] = []
	for altitude_m in [2_000_000.0, 100_000.0, 5_000.0]:
		globe.look_at_direction(focus, globe.range_for_altitude(altitude_m))
		var start := Time.get_ticks_msec()
		var settled := false
		while Time.get_ticks_msec() - start < 30_000:
			globe.look_at_direction(focus, globe.range_for_altitude(altitude_m))
			await process_frame
			var stats: Dictionary = planet.get_terrain_tile_stats()
			if int(stats.get("pending", 0)) == 0 and int(stats.get("coast_pages_pending", 0)) == 0 \
					and int(stats.get("coast_remakes", 0)) == 0 and int(stats.get("coast_layers", 0)) > 0:
				settled = true
				break
		if not settled:
			push_error("coastal pages did not settle for picking at %.0f m" % altitude_m)
			quit(1)
			return
		var cursor := root.get_visible_rect().size * 0.5
		var pick_start := Time.get_ticks_usec()
		var hit = globe.pick(cursor)
		pick_times_ms.append((Time.get_ticks_usec() - pick_start) / 1000.0)
		assert(hit != null, "coastal pick missed the drawn globe")
		var radius: float = planet.get_surface_radius(hit)
		var screen := camera.unproject_position(hit * radius - globe.get_camera_position())
		assert(screen.distance_to(cursor) < 1.0,
			"coastal pick did not project to cursor: %s at %.0f m" % [screen, altitude_m])
		assert(planet.find_cell(hit) >= 0, "coastal pick did not resolve a cell")
	print("coast picking pass: planet, region and local with settled pages; pick ms ", pick_times_ms)
	main.free()
	quit(0)
