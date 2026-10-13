extends SceneTree

## Specification §31.5: "a test samples generated detail and asserts it never
## contradicts the channel values beyond a tolerance".  Renders the real
## planet mesh (L3, cells of about a thousand pixels) through a probe that
## includes surface_detail.gdshaderinc, the code both style packs draw with,
## over channel textures that differ in every texel.  For each cell it
## compares the area drawn as water, snow, sea ice and each land class at
## full detail with the mean of the interpolated channel over the same
## pixels.  It also checks the noise statistics the equalisation assumes,
## that detail is off at planet scale and on at region scale, and that the
## bridge seeds the detail per world.

const IMAGE_SIZE := 512
const SUBDIVISION := 3
const LIMB := 0.85
const MIN_CELL_PIXELS := 200
const MAX_BIAS := 0.02 # planet-wide mean, drawn minus channel
const MAX_MEAN_CELL_ERROR := 0.10 # mean over cells of |drawn - channel|
const MAX_P95_CELL_ERROR := 0.25 # 95% of cells
const MAX_CELL_ERROR := 0.55 # any one cell
# (Measured at 4 features and 3 octaves per cell on random channels, the
# worst case: bias under 0.01, cell error mean 0.08, p95 0.20, worst 0.47.)
const WORLDS := 4 # seed sets the drawn detail is pooled over
const SEEDS := ["detail_seed_coast", "detail_seed_land_mix", "detail_seed_snow", "detail_seed_sea_ice"]
const MAX_UNIFORM_DEVIATION := 0.02 # of the equalised noise's distribution

var _viewport: SubViewport
var _planet: PlanetMeshNode
var _material: ShaderMaterial
var _failures := 0


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
	_check_bridge_seeding(cells)
	_build_scene(cells)

	var cell_of := _cells_under_pixels()

	# The equalisation: the field's quantiles, and the uniformity they give.
	_material.set_shader_parameter("mode", 0)
	var field := _decode_16(cell_of, await _capture(output_dir, "detail_field"))
	field.sort()
	var quantiles := PackedStringArray()
	for knot in 33:
		quantiles.append("%.4f" % field[min(int(knot / 32.0 * field.size()), field.size() - 1)])
	print("detail field quantiles (DETAIL_FIELD_QUANTILES): ", ", ".join(quantiles))
	_material.set_shader_parameter("mode", 6)
	var uniform := _decode_16(cell_of, await _capture(output_dir, "detail_uniform"))
	uniform.sort()
	var deviation := 0.0
	for step in range(1, 64):
		var t := step / 64.0
		deviation = max(deviation, abs(float(uniform.bsearch(t)) / uniform.size() - t))
	print("equalised noise: largest distribution deviation %.4f" % deviation)
	_expect(deviation <= MAX_UNIFORM_DEVIATION,
		"equalised detail noise is not uniform (deviation %.4f > %.2f): update DETAIL_FIELD_QUANTILES"
			% [deviation, MAX_UNIFORM_DEVIATION])

	# The channels do not depend on the seeds; the drawn detail is pooled
	# over several worlds' seeds, so the bias is measured on enough noise
	# features to mean something.
	_material.set_shader_parameter("mode", 1)
	var fractions := await _capture(output_dir, "detail_fractions")
	_material.set_shader_parameter("mode", 3)
	var shares := await _capture(output_dir, "detail_shares")
	var covers: Array[Image] = []
	var patches: Array[Image] = []
	var seeds := {}
	for parameter in SEEDS:
		seeds[parameter] = _material.get_shader_parameter(parameter)
	for world in WORLDS:
		for parameter in SEEDS:
			_material.set_shader_parameter(parameter,
				seeds[parameter] + Vector3(97.31, 41.17, 13.73) * world)
		_material.set_shader_parameter("mode", 2)
		covers.append(await _capture(output_dir, "detail_covers_%d" % world))
		_material.set_shader_parameter("mode", 4)
		patches.append(await _capture(output_dir, "detail_patches_%d" % world))
	_compare(cell_of, fractions, covers, ["water (coast)", "snow", "sea ice"])
	_compare(cell_of, shares, patches, ["rock", "sand", "soil"])

	_material.set_shader_parameter("mode", 5)
	var region := await _capture(output_dir, "detail_amount_region")
	var centre := _decode(region.get_pixel(IMAGE_SIZE / 2, IMAGE_SIZE / 2), 0)
	_expect(centre > 0.99, "detail is not on at region scale: amount %.3f" % centre)
	# The same view of an L6 planet: cells of a few pixels, planet scale.
	_material.set_shader_parameter("cell_angular_size", sqrt(4.0 * PI / 40962.0))
	var planet_scale := await _capture(output_dir, "detail_amount_planet")
	var largest := 0.0
	for pixel in cell_of:
		largest = max(largest, _decode(planet_scale.get_pixel(pixel.x, pixel.y), 0))
	_expect(largest < 0.01, "detail is not off at planet scale: amount up to %.3f" % largest)

	_planet.free()
	if _failures > 0:
		quit(1)
		return
	print("detail coverage pass")
	quit(0)


## The bridge sets the detail uniforms from the world seed (ADR-0018 §4.8).
func _check_bridge_seeding(cells: int) -> void:
	var material: ShaderMaterial = _planet.material_override
	var angle: float = material.get_shader_parameter("cell_angular_size")
	_expect(abs(angle - sqrt(4.0 * PI / cells)) < 1e-4, "cell_angular_size not set: %s" % angle)
	var seeds := {}
	for kind in ["coast", "land_mix", "snow", "sea_ice", "texture"]:
		var seed: Vector3 = material.get_shader_parameter("detail_seed_" + kind)
		_expect(seed != Vector3.ZERO and not seeds.has(seed), "detail_seed_%s not seeded: %s" % [kind, seed])
		seeds[seed] = kind
	var first: Vector3 = material.get_shader_parameter("detail_seed_coast")
	_planet.rebuild(SUBDIVISION, 6_371_000.0, 20260929, "earth_like")
	_expect(material.get_shader_parameter("detail_seed_coast") != first,
		"another world drew the same detail")
	_planet.rebuild(SUBDIVISION, 6_371_000.0, 20260928, "earth_like")
	_expect(material.get_shader_parameter("detail_seed_coast") == first,
		"the same world drew different detail")


func _compare(cell_of: Dictionary, reference: Image, drawn: Array[Image], names: Array) -> void:
	for channel in names.size():
		var totals := {}
		for world in drawn.size():
			for pixel in cell_of:
				var key := Vector2i(world, cell_of[pixel])
				if not totals.has(key):
					totals[key] = Vector3.ZERO
				totals[key] += Vector3(_decode(reference.get_pixel(pixel.x, pixel.y), channel),
					_decode(drawn[world].get_pixel(pixel.x, pixel.y), channel), 1.0)
		var reference_sum := 0.0
		var drawn_sum := 0.0
		var errors := PackedFloat32Array()
		for cell in totals:
			var total: Vector3 = totals[cell]
			reference_sum += total.x
			drawn_sum += total.y
			if total.z < MIN_CELL_PIXELS:
				continue
			errors.append(abs(total.y - total.x) / total.z)
		var pixels := float(cell_of.size() * drawn.size())
		var bias := (drawn_sum - reference_sum) / pixels
		errors.sort()
		var measured := errors.size()
		var mean_error := 0.0
		for error in errors:
			mean_error += error / max(measured, 1)
		var p95: float = errors[int(0.95 * (measured - 1))] if measured > 0 else 0.0
		var worst: float = errors[measured - 1] if measured > 0 else 0.0
		print("%s: bias %+.4f, cell error mean %.4f p95 %.4f worst %.4f over %d cells in %d worlds"
			% [names[channel], bias, mean_error, p95, worst, measured / drawn.size(), drawn.size()])
		_expect(measured > 100 * drawn.size(), "%s: too few cells measured (%d)" % [names[channel], measured])
		_expect(abs(bias) <= MAX_BIAS, "%s: detail biases the channel by %+.4f" % [names[channel], bias])
		_expect(mean_error <= MAX_MEAN_CELL_ERROR,
			"%s: mean cell error %.4f exceeds %.2f" % [names[channel], mean_error, MAX_MEAN_CELL_ERROR])
		_expect(p95 <= MAX_P95_CELL_ERROR,
			"%s: 5%% of cells' detail contradicts its channel by over %.4f" % [names[channel], p95])
		_expect(worst <= MAX_CELL_ERROR,
			"%s: a cell's detail contradicts its channel by %.4f" % [names[channel], worst])


## Pixel -> the cell under it, inside the limb.
func _cells_under_pixels() -> Dictionary:
	var result := {}
	for y in IMAGE_SIZE:
		for x in IMAGE_SIZE:
			var u := (x + 0.5) / IMAGE_SIZE * 2.0 - 1.0
			var v := 1.0 - (y + 0.5) / IMAGE_SIZE * 2.0
			var r2 := u * u + v * v
			if r2 <= LIMB * LIMB:
				result[Vector2i(x, y)] = _planet.find_cell(Vector3(u, v, sqrt(1.0 - r2)))
	return result


func _build_scene(cells: int) -> void:
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

	# Channel values that differ in every cell and corner texel, some at
	# exactly 0 or 1, fixed by the generator's seed.
	var random := RandomNumberGenerator.new()
	random.seed = 31
	var texels := 2 * cells - 4 + cells
	var rows := int(ceil(float(texels) / 2048.0))
	var fractions := Image.create(2048, rows, false, Image.FORMAT_RGBA8)
	var classes := Image.create(2048, rows, false, Image.FORMAT_RGBA8)
	for texel in texels:
		var f := Color(_fraction(random), _fraction(random), _fraction(random), 1.0)
		var w := Color(random.randf(), random.randf(), random.randf(), random.randf())
		fractions.set_pixel(texel % 2048, texel / 2048, f)
		classes.set_pixel(texel % 2048, texel / 2048, w)

	var original: ShaderMaterial = _planet.material_override
	_material = ShaderMaterial.new()
	_material.shader = load("res://tests/data/detail_coverage_probe.gdshader")
	for parameter in ["cell_angular_size", "detail_seed_coast", "detail_seed_land_mix",
			"detail_seed_snow", "detail_seed_sea_ice"]:
		_material.set_shader_parameter(parameter, original.get_shader_parameter(parameter))
	_material.set_shader_parameter("fraction_map", ImageTexture.create_from_image(fractions))
	_material.set_shader_parameter("class_map", ImageTexture.create_from_image(classes))
	_viewport.add_child(_planet)
	_planet.material_override = _material


## The probe's 16-bit code (probe_encode_16).
func _decode_16(cell_of: Dictionary, image: Image) -> PackedFloat32Array:
	var values := PackedFloat32Array()
	for pixel in cell_of:
		var colour := image.get_pixel(pixel.x, pixel.y)
		var high := roundf(colour.r * 255.0) - 64.0
		var low := (colour.g * 255.0 - 64.0) / 127.0
		values.append((high + low) / 127.0)
	return values


## The probe's affine code (probe_encode) of one channel.
func _decode(colour: Color, channel: int) -> float:
	return (colour[channel] - 0.25) / 0.75


func _fraction(random: RandomNumberGenerator) -> float:
	var draw := random.randf()
	if draw < 0.15:
		return 0.0
	if draw < 0.25:
		return 1.0
	return random.randf()


func _expect(condition: bool, message: String) -> void:
	if not condition:
		push_error(message)
		_failures += 1


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
