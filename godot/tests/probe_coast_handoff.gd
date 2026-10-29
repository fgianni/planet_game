extends SceneTree

## Read-only rendering probe for proposed ADR-0030. Render the same coastal
## focus on the cell mesh and on streamed tiles; count pixels whose map-style
## water/land classification differs. Not a CI gate or a shoreline metric.

const SIZE := 512
const OUTPUT := "/tmp/opencode/coast-handoff"
const DISTANCES := [1.45, 1.25, 1.10]


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var atlas_path := ""
	var prefix_base := OUTPUT
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--coast-atlas="):
			atlas_path = arg.trim_prefix("--coast-atlas=")
		if arg.begins_with("--coast-prefix="):
			prefix_base = arg.trim_prefix("--coast-prefix=")
	var viewport := SubViewport.new()
	viewport.size = Vector2i(SIZE, SIZE)
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport.world_3d = World3D.new()
	root.add_child(viewport)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.02, 0.02, 0.02)
	var world := WorldEnvironment.new()
	world.environment = environment
	viewport.add_child(world)
	var camera := Camera3D.new()
	camera.near = 0.001
	camera.far = 10.0
	camera.current = true
	viewport.add_child(camera)
	var planet := PlanetMeshNode.new()
	viewport.add_child(planet)
	planet.rebuild(4, 6_371_000.0, 20260928, "earth_like")
	planet.set_style("map")
	planet.set_day_night_shading(false)
	planet.set_relief_exaggeration(1.0)
	if not atlas_path.is_empty():
		var atlas_image := Image.load_from_file(atlas_path)
		assert(atlas_image != null and atlas_image.get_width() % 3 == 0)
		var side: int = atlas_image.get_width() / 3
		assert(atlas_image.get_height() == 2 * side)
		var material: ShaderMaterial = planet.material_override
		var code: String = material.shader.code
		var old_water := "float open_water = mix(detail_cover(classes.r, detail_uniform(direction, detail_seed_coast), amount),\n\t                       detail_geometry_water(tile_surface.y), tile_surface.x);"
		assert(code.contains(old_water), "expected map-style coastline code")
		code = code.replace("void fragment() {", "#include \"res://tests/coast_mask_spike.gdshaderinc\"\nvoid fragment() {")
		code = code.replace(old_water, "float open_water = coast_probe_water(direction);")
		var shader := Shader.new()
		shader.code = code
		material.shader = shader
		material.set_shader_parameter("coast_probe_atlas", ImageTexture.create_from_image(atlas_image))
		material.set_shader_parameter("coast_probe_side", side)
		if prefix_base == OUTPUT:
			prefix_base = "/tmp/opencode/coast-mask"
	var focus: Vector3 = planet.get_coastal_focus(Vector3(0.0, 0.0, 1.0)).normalized()
	# L4's planet–region edge is about ten cell spacings above ground,
	# approximately 2.8 Mm (1.44 planet radii at this focus).
	var forward := PackedFloat64Array([-focus.x, -focus.y, -focus.z])
	for distance in DISTANCES:
		var position: Vector3 = focus * distance
		planet.position = -position
		camera.look_at(-position)
		var camera_position := PackedFloat64Array([position.x, position.y, position.z])
		var prefix := prefix_base + "-" + str(distance).replace(".", "_")
		planet.update_terrain_tiles(camera_position, forward, 650.0, 0.6, 0.0, 1.0)
		var cell_image := await _capture(viewport, prefix + "-cell.png")
		planet.update_terrain_tiles(camera_position, forward, 650.0, 0.6, 1.0, 1.0)
		for unused in 300:
			await process_frame
			planet.update_terrain_tiles(camera_position, forward, 650.0, 0.6, 1.0, 1.0)
			if int(planet.get_terrain_tile_stats().get("pending", 1)) == 0:
				break
		var stats := planet.get_terrain_tile_stats()
		assert(int(stats.get("pending", 1)) == 0, "terrain tiles did not settle")
		var tile_image := await _capture(viewport, prefix + "-tile.png")
		planet.update_terrain_tiles(camera_position, forward, 650.0, 0.6, 0.5, 1.0)
		await _capture(viewport, prefix + "-blend.png")
		var changed := 0
		var pixels := 0
		for y in range(80, SIZE - 80):
			for x in range(80, SIZE - 80):
				var a := cell_image.get_pixel(x, y)
				var b := tile_image.get_pixel(x, y)
				if maxf(a.r, maxf(a.g, a.b)) < 0.08 or maxf(b.r, maxf(b.g, b.b)) < 0.08:
					continue
				var water_a := a.b > a.r * 1.25 and a.b > a.g * 1.15
				var water_b := b.b > b.r * 1.25 and b.b > b.g * 1.15
				changed += int(water_a != water_b)
				pixels += 1
		print("coast handoff distance %.2f: %d/%d map pixels change; tile stats %s" % [
			distance, changed, pixels, stats])
	quit(0)


func _capture(viewport: SubViewport, path: String) -> Image:
	for unused in 3:
		await process_frame
	var image := viewport.get_texture().get_image()
	assert(image != null, "offscreen renderer did not produce an image")
	image.save_png(path)
	return image
