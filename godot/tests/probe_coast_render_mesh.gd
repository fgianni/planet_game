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
	var face_pair := false
	var include_smooth := false
	var include_cell := false
	var include_fan := false
	var cell_seed := 20260928
	var cell_level := 4
	var probe_fov := 60.0
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--coast-prefix="):
			prefix = argument.trim_prefix("--coast-prefix=")
		if argument.begins_with("--coast-altitudes="):
			altitudes = argument.trim_prefix("--coast-altitudes=")
		if argument == "--coast-ultra":
			include_ultra = true
		if argument == "--coast-face-pair":
			face_pair = true
		if argument == "--coast-smooth":
			include_smooth = true
		if argument == "--coast-cell":
			include_cell = true
		if argument == "--coast-fan":
			include_fan = true
		if argument.begins_with("--coast-cell-seed="):
			cell_seed = int(argument.trim_prefix("--coast-cell-seed="))
		if argument.begins_with("--coast-cell-level="):
			cell_level = int(argument.trim_prefix("--coast-cell-level="))
		if argument.begins_with("--coast-fov="):
			probe_fov = float(argument.trim_prefix("--coast-fov="))
	assert(probe_fov > 0.0 and probe_fov < 180.0)
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
	camera.fov = probe_fov
	camera.current = true
	viewport.add_child(camera)
	var instance := MeshInstance3D.new()
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.vertex_color_use_as_albedo = true
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	instance.material_override = material
	viewport.add_child(instance)
	var cell_planet: PlanetMeshNode = null
	if include_cell:
		cell_planet = PlanetMeshNode.new()
		viewport.add_child(cell_planet)
		cell_planet.rebuild(cell_level, 6_371_000.0, cell_seed, "earth_like")
		cell_planet.set_style("map")
		cell_planet.set_day_night_shading(false)
		cell_planet.set_relief_exaggeration(1.0)
		cell_planet.position = -focus
		cell_planet.visible = false
	for altitude in altitudes.split(",", false):
		var height := float(altitude)
		assert(height > 0.0)
		camera.position = focus * height
		camera.look_at(Vector3.ZERO, Vector3.UP if absf(focus.dot(Vector3.UP)) < 0.9 else Vector3.FORWARD)
		var pose_label := altitude.replace(".", "_")
		if not is_equal_approx(probe_fov, 60.0):
			pose_label += "-fov%s" % str(probe_fov).replace(".", "_")
		var kinds := ["face-same", "face-mixed"] if face_pair else ["parent", "children", "detail"]
		if include_ultra:
			kinds.append("ultra")
		if include_smooth:
			kinds.append("smooth")
		if include_fan:
			kinds.append("fan")
		for kind in kinds:
			instance.mesh = _mesh(prefix + "-" + kind + ".bin")
			for unused in 4:
				await process_frame
			var image := viewport.get_texture().get_image()
			assert(image != null)
			var output: String = prefix + "-" + pose_label + "-" + kind + ".png"
			assert(image.save_png(output) == OK, "failed to save " + output)
			print(output)
		if include_cell:
			instance.visible = false
			cell_planet.visible = true
			for unused in 4:
				await process_frame
			var cell_image := viewport.get_texture().get_image()
			var cell_output: String = prefix + "-" + pose_label + "-cell.png"
			assert(cell_image != null and cell_image.save_png(cell_output) == OK)
			print(cell_output)
			cell_planet.visible = false
			instance.visible = true
	quit(0)
