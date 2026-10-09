extends SceneTree

## Deterministic offscreen renderer for ADR-0018 V8.  Each image is driven
## only by the semantic textures visible to a style pack.

const IMAGE_SIZE := 128
const STYLES := ["stylised", "map"]

var _viewport: SubViewport
var _material: ShaderMaterial


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var output_dir := "/tmp/planetsim-readability"
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--output="):
			output_dir = argument.trim_prefix("--output=")
	DirAccess.make_dir_recursive_absolute(output_dir)

	_build_scene()
	var images: Array[Dictionary] = []
	for style in STYLES:
		_material.shader = load("res://styles/%s/surface.gdshader" % style)
		_material.set_shader_parameter("day_night", false)
		for signal_case in _signal_cases():
			var baseline_name := "%s_%s_baseline" % [style, signal_case.name]
			var signal_name := "%s_%s_signal" % [style, signal_case.name]
			_apply_channels(signal_case.baseline)
			await _capture(output_dir, baseline_name)
			_apply_channels(signal_case.signal)
			await _capture(output_dir, signal_name)
			images.append({
				"style": style,
				"signal": signal_case.name,
				"baseline": baseline_name + ".ppm",
				"changed": signal_name + ".ppm",
			})

	var manifest := {
		"schema": "PlanetSim.readability-input.v1",
		"width": IMAGE_SIZE,
		"height": IMAGE_SIZE,
		"crop_margin": 16,
		"images": images,
	}
	var file := FileAccess.open(output_dir.path_join("manifest.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify(manifest, "  ") + "\n")
	file.close()
	print("R1-03 rendered %d image pairs to %s" % [images.size(), output_dir])
	quit(0)


func _build_scene() -> void:
	_viewport = SubViewport.new()
	_viewport.size = Vector2i(IMAGE_SIZE, IMAGE_SIZE)
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_viewport.render_target_clear_mode = SubViewport.CLEAR_MODE_ALWAYS
	_viewport.transparent_bg = false
	_viewport.world_3d = World3D.new()
	root.add_child(_viewport)

	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.02, 0.02, 0.04)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color.WHITE
	environment.ambient_light_energy = 1.0
	var world_environment := WorldEnvironment.new()
	world_environment.environment = environment
	_viewport.add_child(world_environment)

	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 2.0
	camera.position = Vector3(0.0, 0.0, 2.0)
	camera.current = true
	_viewport.add_child(camera)

	var quad := QuadMesh.new()
	quad.size = Vector2(2.0, 2.0)
	_material = ShaderMaterial.new()
	var mesh := MeshInstance3D.new()
	mesh.mesh = quad
	mesh.material_override = _material
	_viewport.add_child(mesh)


func _signal_cases() -> Array[Dictionary]:
	var common := {
		"relief": 100.0,
		"daylight": 1.0,
		"known": 1.0,
		"guessed": 0.0,
		"knowledge_age": 0.0,
	}
	var warm := common.duplicate()
	warm.merge({"classes": Color(0.0, 0.0, 0.0, 1.0), "ice": 0.0,
		"snow": 0.0, "sea_ice": 0.0}, true)
	var cold := common.duplicate()
	cold.merge({"classes": Color(0.0, 0.0, 0.0, 0.45), "ice": 0.55,
		"snow": 1.0, "sea_ice": 0.0}, true)
	var ocean := common.duplicate()
	ocean.merge({"classes": Color(1.0, 0.0, 0.0, 0.0), "ice": 0.0,
		"snow": 0.0, "sea_ice": 0.0, "relief": -3000.0}, true)
	var land := common.duplicate()
	land.merge({"classes": Color(0.0, 0.0, 0.0, 1.0), "ice": 0.0,
		"snow": 0.0, "sea_ice": 0.0}, true)
	return [
		{"name": "cold_vs_warm", "baseline": warm, "signal": cold},
		{"name": "land_vs_ocean", "baseline": ocean, "signal": land},
	]


func _constant_texture(value: Color) -> ImageTexture:
	var image := Image.create(2, 2, false, Image.FORMAT_RGBAF)
	image.fill(value)
	return ImageTexture.create_from_image(image)


func _apply_channels(values: Dictionary) -> void:
	_material.set_shader_parameter("relief_map", _constant_texture(Color(values.relief, 0.0, 0.0, 1.0)))
	_material.set_shader_parameter("surface_class_map", _constant_texture(values.classes))
	_material.set_shader_parameter("surface_class_ice_map", _constant_texture(Color(values.ice, 0.0, 0.0, 1.0)))
	_material.set_shader_parameter("daylight_map", _constant_texture(Color(values.daylight, 0.0, 0.0, 1.0)))
	_material.set_shader_parameter("snow_cover_map", _constant_texture(Color(values.snow, 0.0, 0.0, 1.0)))
	_material.set_shader_parameter("sea_ice_map", _constant_texture(Color(values.sea_ice, 0.0, 0.0, 1.0)))
	_material.set_shader_parameter("known_map", _constant_texture(Color(values.known, 0.0, 0.0, 1.0)))
	_material.set_shader_parameter("guessed_map", _constant_texture(Color(values.guessed, 0.0, 0.0, 1.0)))
	_material.set_shader_parameter("knowledge_age_map", _constant_texture(Color(values.knowledge_age, 0.0, 0.0, 1.0)))


func _capture(output_dir: String, image_name: String) -> void:
	await process_frame
	await process_frame
	var image := _viewport.get_texture().get_image()
	if image == null:
		push_error("The active display driver did not produce an offscreen image")
		quit(1)
		return
	image.convert(Image.FORMAT_RGB8)
	image.save_png(output_dir.path_join(image_name + ".png"))
	var ppm := FileAccess.open(output_dir.path_join(image_name + ".ppm"), FileAccess.WRITE)
	ppm.store_string("P6\n%d %d\n255\n" % [image.get_width(), image.get_height()])
	ppm.store_buffer(image.get_data())
	ppm.close()
