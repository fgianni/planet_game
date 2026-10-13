extends SceneTree

## Land rendering survey step 4: with the planet lit by its own sun, "land
## and ocean contrast holds at planet and region scale".  Runs the real main
## scene (pass --subdivision=4) on the day side it opens on, in both styles,
## at planet distance and close in.  Each sampled pixel is classed as land
## or ocean by the relief channel of the cell under it; on the day side
## (daylight above one half) the test checks that land and ocean are each
## bright enough to read and that they differ by a clear CIELAB ΔE.

const STYLES := ["stylised", "map"]
const SCALES := {"planet": 3.2, "region": 1.35}
const STEP := 4 # sample every fourth pixel in each direction
const MIN_SAMPLES := 200
const MIN_LAND_LIGHTNESS := 35.0 # L*, day-side mean
const MIN_OCEAN_LIGHTNESS := 15.0
const MIN_LAND_OCEAN_DELTA_E := 20.0 # CIE76 between the means

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

	var main: Node3D = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await process_frame
	var planet: PlanetMeshNode = main.get_node("Planet")
	var camera: Camera3D = main.get_node("CameraPivot/Camera3D")
	main.get_node("Hud").visible = false
	_expect(planet.get_day_night_shading(), "the scene does not open with day/night shading")
	_expect(planet.get_node_or_null("Sun") is DirectionalLight3D and planet.get_node("Sun").visible,
		"the planet has no visible sun light")
	var sun: Vector3 = planet.get_sun_direction()
	var view: Vector3 = camera.global_position.normalized()
	_expect(view.dot(sun) > 0.5, "the scene does not open on the day side (view·sun %.2f)" % view.dot(sun))

	for style in STYLES:
		planet.set_style(style)
		for scale in SCALES:
			main._distance = SCALES[scale]
			main._update_camera()
			for frame in 4:
				await process_frame
			var image := root.get_texture().get_image()
			image.convert(Image.FORMAT_RGB8)
			if not output_dir.is_empty():
				image.save_png(output_dir.path_join("lighting_%s_%s.png" % [style, scale]))
			_measure(style, scale, image, camera, planet)

	main.free()
	if _failures > 0:
		quit(1)
		return
	print("scene lighting pass")
	quit(0)


func _measure(style: String, scale: String, image: Image, camera: Camera3D,
		planet: PlanetMeshNode) -> void:
	var land := Vector4.ZERO # summed linear-light-free sRGB and count
	var ocean := Vector4.ZERO
	for y in range(STEP / 2, image.get_height(), STEP):
		for x in range(STEP / 2, image.get_width(), STEP):
			var origin := camera.project_ray_origin(Vector2(x, y))
			var direction := camera.project_ray_normal(Vector2(x, y))
			var hit := _sphere_hit(origin, direction)
			if hit == Vector3.ZERO:
				continue
			var cell: int = planet.find_cell(hit)
			if planet.get_channel_value(cell, "daylight") <= 0.5:
				continue
			var colour := image.get_pixel(x, y)
			var sample := Vector4(colour.r, colour.g, colour.b, 1.0)
			if planet.get_channel_value(cell, "relief") > 0.0:
				land += sample
			else:
				ocean += sample
	var name := "%s %s" % [style, scale]
	if land.w < MIN_SAMPLES or ocean.w < MIN_SAMPLES:
		_expect(false, "%s: too few day-side samples (land %d, ocean %d)" % [name, land.w, ocean.w])
		return
	var land_lab := _lab(Color(land.x / land.w, land.y / land.w, land.z / land.w))
	var ocean_lab := _lab(Color(ocean.x / ocean.w, ocean.y / ocean.w, ocean.z / ocean.w))
	var delta_e := land_lab.distance_to(ocean_lab)
	print("%s: land L* %.1f, ocean L* %.1f, land-ocean ΔE %.1f (%d / %d samples)"
		% [name, land_lab.x, ocean_lab.x, delta_e, land.w, ocean.w])
	_expect(land_lab.x >= MIN_LAND_LIGHTNESS, "%s: land too dark (L* %.1f)" % [name, land_lab.x])
	_expect(ocean_lab.x >= MIN_OCEAN_LIGHTNESS, "%s: ocean too dark (L* %.1f)" % [name, ocean_lab.x])
	_expect(delta_e >= MIN_LAND_OCEAN_DELTA_E,
		"%s: land and ocean too alike (ΔE %.1f)" % [name, delta_e])


## Where a camera ray first meets the unit sphere, or ZERO.
func _sphere_hit(origin: Vector3, direction: Vector3) -> Vector3:
	var b := origin.dot(direction)
	var c := origin.length_squared() - 1.0
	var discriminant := b * b - c
	if discriminant < 0.0:
		return Vector3.ZERO
	var t := -b - sqrt(discriminant)
	return origin + direction * t if t > 0.0 else Vector3.ZERO


## sRGB (D65) to CIELAB, as (L*, a*, b*).
func _lab(colour: Color) -> Vector3:
	var linear := Vector3(_to_linear(colour.r), _to_linear(colour.g), _to_linear(colour.b))
	var xyz := Vector3(
		0.4124 * linear.x + 0.3576 * linear.y + 0.1805 * linear.z,
		0.2126 * linear.x + 0.7152 * linear.y + 0.0722 * linear.z,
		0.0193 * linear.x + 0.1192 * linear.y + 0.9505 * linear.z)
	var f := Vector3(_lab_f(xyz.x / 0.95047), _lab_f(xyz.y), _lab_f(xyz.z / 1.08883))
	return Vector3(116.0 * f.y - 16.0, 500.0 * (f.x - f.y), 200.0 * (f.y - f.z))


func _to_linear(c: float) -> float:
	return c / 12.92 if c <= 0.04045 else pow((c + 0.055) / 1.055, 2.4)


func _lab_f(t: float) -> float:
	return pow(t, 1.0 / 3.0) if t > 0.008856 else 7.787 * t + 16.0 / 116.0


func _expect(condition: bool, message: String) -> void:
	if not condition:
		push_error(message)
		_failures += 1
