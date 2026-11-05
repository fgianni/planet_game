extends SceneTree

## Cross-check the Python boundary-sample nadir projection against Godot's
## Camera3D projection; this does not test coastline geometry or handoff.

func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var viewport := SubViewport.new()
	viewport.size = Vector2i(512, 512)
	viewport.world_3d = World3D.new()
	root.add_child(viewport)
	var camera := Camera3D.new()
	camera.current = true
	viewport.add_child(camera)
	camera.position = Vector3(0.0, 0.0, 0.7)
	camera.look_at(Vector3.ZERO, Vector3.UP)
	var direction := Vector3(0.1, 0.0, sqrt(0.99))
	for probe in [[75.0, 303.32195041718757], [60.0, 318.89320645832083]]:
		camera.fov = probe[0]
		await process_frame
		var pixel := camera.unproject_position(direction - Vector3(0.0, 0.0, 1.0))
		assert(pixel.distance_to(Vector2(probe[1], 256.0)) < 0.05,
			"boundary projection disagrees with Camera3D: %s" % pixel)
	quit(0)
