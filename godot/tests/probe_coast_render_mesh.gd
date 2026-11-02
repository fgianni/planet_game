extends SceneTree

## ADR-0030 probe only: fixed-pose water-mask renders of clipped geometry.
## --coast-prefix=/tmp/opencode/coast-render-L4 --coast-altitudes=0.15,0.08,0.04
const SIZE := 512

func _initialize() -> void:
	call_deferred("_run")

func _mesh(path: String) -> ArrayMesh:
	var file := FileAccess.open(path, FileAccess.READ)
	assert(file != null, "missing mesh: " + path)
	var count := file.get_32()
	assert(count > 0 and count % 3 == 0 and file.get_length() == 4 + count * 16,
		"invalid triangle-list header: " + path)
	var positions := PackedVector3Array()
	var colours := PackedColorArray()
	positions.resize(count)
	colours.resize(count)
	for index in count:
		positions[index] = Vector3(file.get_float(), file.get_float(), file.get_float())
		var water := file.get_float()
		colours[index] = Color(0.05, 0.25, 0.95) if water > 0.5 else Color(0.95, 0.3, 0.05)
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = positions
	arrays[Mesh.ARRAY_COLOR] = colours
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh

func _run() -> void:
	var prefix := "/tmp/opencode/coast-render-L4"
	var altitudes := "0.15,0.08,0.04"
	var include_ultra := false
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--coast-prefix="):
			prefix = argument.trim_prefix("--coast-prefix=")
		if argument.begins_with("--coast-altitudes="):
			altitudes = argument.trim_prefix("--coast-altitudes=")
		if argument == "--coast-ultra":
			include_ultra = true
	var focus_file := FileAccess.open(prefix + "-focus.txt", FileAccess.READ)
	assert(focus_file != null, "missing focus")
	var parts := focus_file.get_line().split(" ", false)
	assert(parts.size() == 3)
	var focus := Vector3(float(parts[0]), float(parts[1]), float(parts[2])).normalized()
	var viewport := SubViewport.new()
	viewport.size = Vector2i(SIZE, SIZE)
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport.world_3d = World3D.new()
	root.add_child(viewport)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color.BLACK
	environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var world := WorldEnvironment.new()
	world.environment = environment
	viewport.add_child(world)
	var camera := Camera3D.new()
	camera.near = 0.00001
	camera.far = 2.0
	camera.fov = 60.0
	camera.current = true
	viewport.add_child(camera)
	var instance := MeshInstance3D.new()
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.vertex_color_use_as_albedo = true
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	instance.material_override = material
	viewport.add_child(instance)
	for altitude in altitudes.split(",", false):
		var height := float(altitude)
		assert(height > 0.0)
		camera.position = focus * height
		camera.look_at(Vector3.ZERO, Vector3.UP if absf(focus.dot(Vector3.UP)) < 0.9 else Vector3.FORWARD)
		var kinds := ["parent", "children", "detail"]
		if include_ultra:
			kinds.append("ultra")
		for kind in kinds:
			instance.mesh = _mesh(prefix + "-" + kind + ".bin")
			for unused in 4:
				await process_frame
			var image := viewport.get_texture().get_image()
			assert(image != null)
			var output: String = prefix + "-" + altitude.replace(".", "_") + "-" + kind + ".png"
			assert(image.save_png(output) == OK, "failed to save " + output)
			print(output)
	quit(0)
