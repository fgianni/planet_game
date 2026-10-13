extends SceneTree

## Non-constant readability case (land rendering survey, step 1).  The V8
## harness drives the styles with constant textures, where any texel address
## reads the right value; this renders the real planet mesh with a channel
## texture that differs in every cell, so a fragment that fetches anything
## but its own cell's texel shows.  Mode 0 checks the cell texel (UV) against
## the cell found independently under each pixel; mode 1 checks the corner
## texels and barycentric weights that smooth styles use (UV2, CUSTOM0).

const IMAGE_SIZE := 512
const SUBDIVISION := 4
const CHANNEL_TEXTURE_WIDTH := 2048 # planet_mesh_node.cpp channel_texture_width
const LEVELS := 16 # code levels per colour channel
const LIMB := 0.9 # sample only well inside the disc
const REQUIRED_MATCH := 0.97 # pixels on cell edges may round either way

var _viewport: SubViewport
var _planet: PlanetMeshNode
var _material: ShaderMaterial


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var output_dir := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--output="):
			output_dir = argument.trim_prefix("--output=")
	if not output_dir.is_empty():
		DirAccess.make_dir_recursive_absolute(output_dir)

	_planet = PlanetMeshNode.new()
	_planet.rebuild(SUBDIVISION, 6_371_000.0, 20260928, "earth_like")
	_planet.set_relief_exaggeration(0.0)
	var cells: int = 10 * int(pow(4, SUBDIVISION)) + 2
	var corners: int = 2 * cells - 4
	assert(cells < LEVELS * LEVELS * LEVELS - 1, "too many cells to encode")
	_build_scene(cells, corners)

	var failures := 0
	_material.set_shader_parameter("mode", 0)
	var own := await _capture(output_dir, "cell_texels_own")
	var samples := 0
	var matches := 0
	var corner_reads := 0
	for y in IMAGE_SIZE:
		for x in IMAGE_SIZE:
			var direction := _surface_direction(x, y)
			if direction == Vector3.ZERO:
				continue
			samples += 1
			var code := _decode(own.get_pixel(x, y))
			if code >= cells:
				corner_reads += 1
			elif code == _planet.find_cell(direction):
				matches += 1
	var fraction := float(matches) / float(max(samples, 1))
	if samples == 0 or fraction < REQUIRED_MATCH or corner_reads > 0:
		push_error("cell texels: %d of %d pixels show their own cell (%.4f < %.2f), %d read a corner texel"
			% [matches, samples, fraction, REQUIRED_MATCH, corner_reads])
		failures += 1

	_material.set_shader_parameter("mode", 1)
	var smooth := await _capture(output_dir, "cell_texels_corners")
	var bad_smooth := 0
	for y in IMAGE_SIZE:
		for x in IMAGE_SIZE:
			if _surface_direction(x, y) == Vector3.ZERO:
				continue
			var flags := smooth.get_pixel(x, y)
			if flags.r < 0.5 or flags.g < 0.5 or flags.b < 0.5:
				bad_smooth += 1
	if bad_smooth > 0:
		push_error("corner texels: %d of %d pixels lack two corner addresses and barycentric weights"
			% [bad_smooth, samples])
		failures += 1

	_planet.free()
	if failures > 0:
		quit(1)
		return
	print("cell texel pass: %d of %d pixels show their own cell (%.4f), corner addresses on all"
		% [matches, samples, fraction])
	quit(0)


func _build_scene(cells: int, corners: int) -> void:
	_viewport = SubViewport.new()
	_viewport.size = Vector2i(IMAGE_SIZE, IMAGE_SIZE)
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_viewport.transparent_bg = false
	_viewport.world_3d = World3D.new()
	root.add_child(_viewport)

	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color.BLACK
	environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var world_environment := WorldEnvironment.new()
	world_environment.environment = environment
	_viewport.add_child(world_environment)

	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 2.0
	camera.position = Vector3(0.0, 0.0, 3.0)
	camera.current = true
	_viewport.add_child(camera)

	# Every cell texel holds its own index; every corner texel a flag in
	# alpha and the out-of-range code LEVELS³ - 1.
	var rows := int(ceil(float(cells + corners) / CHANNEL_TEXTURE_WIDTH))
	var image := Image.create(CHANNEL_TEXTURE_WIDTH, rows, false, Image.FORMAT_RGBA8)
	for texel in cells + corners:
		var corner := texel >= cells
		var colour := _encode(LEVELS * LEVELS * LEVELS - 1 if corner else texel)
		colour.a = 1.0 if corner else 0.0
		image.set_pixel(texel % CHANNEL_TEXTURE_WIDTH, texel / CHANNEL_TEXTURE_WIDTH, colour)
	_material = ShaderMaterial.new()
	_material.shader = load("res://tests/data/cell_texel_probe.gdshader")
	_material.set_shader_parameter("codes", ImageTexture.create_from_image(image))
	_viewport.add_child(_planet)
	_planet.material_override = _material


func _encode(code: int) -> Color:
	var step := 1.0 / float(LEVELS - 1)
	return Color((code % LEVELS) * step, ((code / LEVELS) % LEVELS) * step,
		(code / (LEVELS * LEVELS)) * step, 1.0)


func _decode(colour: Color) -> int:
	var scale := float(LEVELS - 1)
	return roundi(colour.r * scale) + LEVELS * roundi(colour.g * scale) \
		+ LEVELS * LEVELS * roundi(colour.b * scale)


## The direction of the sphere point seen at a pixel, or ZERO off the disc.
func _surface_direction(x: int, y: int) -> Vector3:
	var u := (x + 0.5) / IMAGE_SIZE * 2.0 - 1.0
	var v := 1.0 - (y + 0.5) / IMAGE_SIZE * 2.0
	var r2 := u * u + v * v
	if r2 > LIMB * LIMB:
		return Vector3.ZERO
	return Vector3(u, v, sqrt(1.0 - r2))


func _capture(output_dir: String, image_name: String) -> Image:
	await process_frame
	await process_frame
	var image := _viewport.get_texture().get_image()
	if image == null:
		push_error("The active display driver did not produce an offscreen image")
		quit(1)
		return Image.new()
	image.convert(Image.FORMAT_RGB8)
	if not output_dir.is_empty():
		image.save_png(output_dir.path_join(image_name + ".png"))
	return image
