extends SceneTree

## Godot's Compatibility renderer shows dark colours darker than a shader
## writes them (see shaders/display_transfer.gdshaderinc).  An offscreen
## capture shows what the screen shows (checked against the X display on
## 2026-10-14), so this renders ramps of every 8-bit level and checks that
## display_unshaded (unshaded ALBEDO, EMISSION) and display_albedo (lit
## ALBEDO under white ambient light, as in the readability harness) bring
## the shown level back to the specified one.

const MAX_ERROR := 1 # levels of 255
const LIT_EXEMPT_BELOW := 6 # the final encoding after lighting is not corrected

const HEAD := "shader_type spatial;\n#include \"res://shaders/display_transfer.gdshaderinc\"\n"
const LEVEL := "float v = floor(FRAGCOORD.x / 4.0) / 255.0;\n"

var _failures := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var raw := await _ramp("render_mode unshaded;\nvoid fragment(){ " + LEVEL + "ALBEDO = vec3(v); }", false)
	print("renderer, uncorrected: 8 -> %d, 16 -> %d, 32 -> %d, 64 -> %d" % [raw[8], raw[16], raw[32], raw[64]])
	_check("unshaded", await _ramp(
		"render_mode unshaded;\nvoid fragment(){ " + LEVEL + "ALBEDO = display_unshaded(vec3(v)); }", false), 0)
	_check("emission", await _ramp(
		"void fragment(){ " + LEVEL + "ALBEDO = vec3(0.0); EMISSION = display_unshaded(vec3(v)); }", false), 0)
	_check("lit albedo", await _ramp(
		"void fragment(){ " + LEVEL + "ALBEDO = display_albedo(vec3(v)); ROUGHNESS = 1.0; SPECULAR = 0.0; }", true),
		LIT_EXEMPT_BELOW)
	if _failures > 0:
		quit(1)
		return
	print("display transfer pass")
	quit(0)


func _check(path: String, shown: PackedInt32Array, from_level: int) -> void:
	var worst := 0
	var worst_level := 0
	for level in range(from_level, 256):
		var error: int = abs(shown[level] - level)
		if error > worst:
			worst = error
			worst_level = level
	print("%s: largest error %d at level %d (from level %d)" % [path, worst, worst_level, from_level])
	if worst > MAX_ERROR:
		push_error("%s: level %d is shown as %d" % [path, worst_level, shown[worst_level]])
		_failures += 1


## Every level 0..255, four pixels each, as shown.
func _ramp(code: String, white_ambient: bool) -> PackedInt32Array:
	var viewport := SubViewport.new()
	viewport.size = Vector2i(1024, 4)
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport.world_3d = World3D.new()
	root.add_child(viewport)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color.BLACK
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color.WHITE if white_ambient else Color.BLACK
	environment.ambient_light_energy = 1.0
	var world_environment := WorldEnvironment.new()
	world_environment.environment = environment
	viewport.add_child(world_environment)
	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.keep_aspect = Camera3D.KEEP_WIDTH
	camera.size = 2.0
	camera.position = Vector3(0.0, 0.0, 2.0)
	viewport.add_child(camera)
	camera.current = true
	var quad := MeshInstance3D.new()
	var quad_mesh := QuadMesh.new()
	quad_mesh.size = Vector2(2.0, 2.0)
	quad.mesh = quad_mesh
	var material := ShaderMaterial.new()
	var shader := Shader.new()
	shader.code = HEAD + code
	material.shader = shader
	quad.material_override = material
	viewport.add_child(quad)
	for frame in 3:
		await process_frame
	var image := viewport.get_texture().get_image()
	var shown := PackedInt32Array()
	for level in 256:
		shown.append(roundi(image.get_pixel(4 * level + 2, 2).r * 255.0))
	viewport.queue_free()
	return shown
