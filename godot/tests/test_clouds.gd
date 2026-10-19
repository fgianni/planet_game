extends SceneTree

## The cloud layer (ADR-0023 §4.8, V12; task M8-06): a shell over the
## surface drawn by the style's cloud shader where the frame has clouds.
## Run with a presentation record whose frames carry the cloud channels.

const IMAGE_SIZE := 96

var _viewport: SubViewport
var _camera: Camera3D
var _planet: PlanetMeshNode


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	assert(not arguments.is_empty(), "test_clouds needs a presentation record")
	_build_scene()
	_planet.rebuild(2, 6_371_000.0, 1, "earth_like")
	_planet.load_presentation_record(arguments[0])
	_planet.set_presentation_frame(_planet.get_presentation_frame_count() - 1)
	_planet.set_day_night_shading(false)
	await process_frame

	# The contract.
	assert(_planet.is_cloud_layer_drawn(), "a frame with clouds drew no cloud layer")
	assert(_planet.get_cloud_radius() > 1.0, "the cloud shell is not above the surface")
	_planet.set_clouds_visible(false)
	assert(not _planet.is_cloud_layer_drawn(), "hiding the clouds left the layer drawn")
	_planet.set_clouds_visible(true)
	assert(_planet.is_cloud_layer_drawn(), "showing the clouds again did not draw them")
	_planet.set_view_mode(8)
	assert(not _planet.is_cloud_layer_drawn(), "a climate-lab view drew the style's clouds")
	_planet.set_view_mode(0)
	assert(_planet.is_cloud_layer_drawn(), "back in the style view the clouds did not return")
	_planet.set_style("map")
	assert(_planet.is_cloud_layer_drawn(), "the map style drew no cloud layer")
	_planet.set_style("stylised")

	# From orbit: the clouds brighten the planet.
	_camera.position = Vector3(0.0, 0.0, 3.2)
	_camera.look_at(Vector3.ZERO)
	var clear := await _mean_luminance(false)
	var clouded := await _mean_luminance(true)
	print("orbit: mean luminance clear %.4f, clouded %.4f" % [clear, clouded])
	assert(clouded > clear + 0.01, "from orbit the clouds did not brighten the planet")

	# From below: just above the ground under a cloudy cell, looking up.
	var direction := _cloudiest_direction()
	var ground := direction * (1.0 + 0.2 * (_planet.get_cloud_radius() - 1.0))
	_camera.position = ground
	_camera.look_at(ground + direction * 10.0, Vector3(direction.y, direction.z, direction.x).cross(direction).normalized())
	var sky_clear := await _mean_luminance(false)
	var sky_clouded := await _mean_luminance(true)
	print("ground, looking up: sky luminance clear %.4f, clouded %.4f" % [sky_clear, sky_clouded])
	assert(sky_clouded > sky_clear + 0.05, "from the ground the clouds were not seen overhead")
	print("M8-06 cloud layer pass")
	quit(0)


func _build_scene() -> void:
	_viewport = SubViewport.new()
	_viewport.size = Vector2i(IMAGE_SIZE, IMAGE_SIZE)
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_viewport.render_target_clear_mode = SubViewport.CLEAR_MODE_ALWAYS
	_viewport.world_3d = World3D.new()
	root.add_child(_viewport)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.02, 0.02, 0.04)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color.WHITE
	var world_environment := WorldEnvironment.new()
	world_environment.environment = environment
	_viewport.add_child(world_environment)
	_camera = Camera3D.new()
	_camera.near = 1e-5
	_camera.far = 10.0
	_camera.current = true
	_viewport.add_child(_camera)
	_planet = PlanetMeshNode.new()
	_viewport.add_child(_planet)


## The direction of the cell with the most cloud cover in the frame.
func _cloudiest_direction() -> Vector3:
	var cover: PackedFloat32Array = _planet.get_channel_values("cloud_cover")
	var best := 0
	for cell in range(cover.size()):
		if cover[cell] > cover[best]:
			best = cell
	assert(cover.size() > 0 and cover[best] > 0.5, "the frame has no cloudy cell")
	return _planet.get_cell_direction(best).normalized()


func _mean_luminance(clouds: bool) -> float:
	_planet.set_clouds_visible(clouds)
	await process_frame
	await process_frame
	await process_frame
	var image := _viewport.get_texture().get_image()
	image.convert(Image.FORMAT_RGB8)
	var total := 0.0
	for y in range(image.get_height()):
		for x in range(image.get_width()):
			total += image.get_pixel(x, y).get_luminance()
	return total / float(image.get_width() * image.get_height())
