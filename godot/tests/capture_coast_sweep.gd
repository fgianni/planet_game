extends SceneTree

## R1-05's rendered gates (ADR-0030 Amendment 1): a zoom sweep over a fixed
## coastal focus, the surface drawn as its water alone (`coast_debug`). At
## each altitude, from the highest down, the view moves to the new pose and
## is captured twice: on arrival, still drawn with the previous pose's tiles
## and pages ("arrive"), and once the tiles and pages it needs have arrived
## ("settled", at most a minute). Each frame is saved as a PNG with its exact
## camera, so tools/render/coast_render_reference.cpp can cast the same rays
## at the canonical water field.
##
## Arguments: --focus=x,y,z (a direction), --altitudes=km,km,...,
## --output=DIR and optionally --exaggeration=X (the relief's); and the main scene's --subdivision=N, --seed=N and
## --style=map|stylised.


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var output := ""
	var focus := Vector3(1.0, 0.0, 0.0)
	var altitudes: Array = [12000.0, 4000.0, 1000.0, 250.0, 50.0, 10.0, 2.0]
	var exaggeration := -1.0 # the scene's own unless given
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--output="):
			output = argument.trim_prefix("--output=")
		elif argument.begins_with("--focus="):
			var parts := argument.trim_prefix("--focus=").split(",")
			focus = Vector3(parts[0].to_float(), parts[1].to_float(), parts[2].to_float()).normalized()
		elif argument.begins_with("--exaggeration="):
			exaggeration = argument.trim_prefix("--exaggeration=").to_float()
		elif argument.begins_with("--altitudes="):
			altitudes = []
			for part in argument.trim_prefix("--altitudes=").split(","):
				altitudes.append(part.to_float())
	DirAccess.make_dir_recursive_absolute(output)
	var main: Node3D = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await process_frame
	var planet: PlanetMeshNode = main.get_node("Planet")
	var globe: Node3D = main.get_node("CameraPivot")
	var camera: Camera3D = main.get_node("CameraPivot/Camera3D")
	main.get_node("Hud").visible = false
	planet.set_day_night_shading(false)
	planet.set_clouds_visible(false)
	if exaggeration >= 0.0:
		planet.set_relief_exaggeration(exaggeration)
	main._paused = true
	globe.set_process(false)
	var material: ShaderMaterial = planet.material_override
	material.set_shader_parameter("coast_debug", true)

	var index := 0
	for altitude_km in altitudes:
		globe.look_at_direction(focus, globe.range_for_altitude(altitude_km * 1000.0), 0.0)
		await process_frame
		await process_frame
		_save(output, "%02d_arrive" % index, altitude_km, globe, camera, planet)
		var start := Time.get_ticks_msec()
		while Time.get_ticks_msec() - start < 60_000:
			globe.look_at_direction(focus, globe.range_for_altitude(altitude_km * 1000.0), 0.0)
			await process_frame
			var stats: Dictionary = planet.get_terrain_tile_stats()
			if int(stats.get("pending", 0)) == 0 and int(stats.get("coast_pages_pending", 0)) == 0 \
					and int(stats.get("coast_remakes", 0)) == 0:
				break
		await process_frame
		await process_frame
		_save(output, "%02d_settled" % index, altitude_km, globe, camera, planet)
		index += 1
	main.free()
	quit(0)


## The frame and its camera: planet-centred position (radii, from the globe's
## doubles), the camera's axes, vertical field of view and the image size.
## The rays are Godot's own (checked against project_ray_normal).
func _save(output: String, name: String, altitude_km: float, globe: Node3D, camera: Camera3D,
		planet: PlanetMeshNode) -> void:
	var image := root.get_texture().get_image()
	image.save_png(output.path_join(name + ".png"))
	var size := Vector2(image.get_width(), image.get_height())
	var basis := camera.global_transform.basis
	var tangent := tan(deg_to_rad(camera.fov) * 0.5)
	# The ray formula the reference uses, against Godot's at a corner pixel.
	var pixel := Vector2(3.0, size.y - 5.0)
	var ndc := Vector2(2.0 * (pixel.x + 0.5) / size.x - 1.0, 1.0 - 2.0 * (pixel.y + 0.5) / size.y)
	var mine := (basis.x * ndc.x * tangent * size.x / size.y + basis.y * ndc.y * tangent - basis.z).normalized()
	var godot := camera.project_ray_normal(pixel + Vector2(0.5, 0.5))
	if mine.distance_to(godot) > 1e-5:
		push_error("ray formula differs from Godot's: %s %s" % [mine, godot])
	var position: Array = globe._camera_position
	var stats: Dictionary = planet.get_terrain_tile_stats()
	var lines := PackedStringArray([
		"altitude_km %f" % altitude_km,
		"band %s" % globe.get_current_band(),
		"width %d" % image.get_width(),
		"height %d" % image.get_height(),
		"position " + _numbers([position[0], position[1], position[2]]),
		"right " + _numbers([basis.x.x, basis.x.y, basis.x.z]),
		"up " + _numbers([basis.y.x, basis.y.y, basis.y.z]),
		"back " + _numbers([basis.z.x, basis.z.y, basis.z.z]),
		"tangent " + _numbers([tangent]),
		"drawn %d" % int(stats.get("drawn", 0)),
		"deepest_level %d" % int(stats.get("deepest_level", 0)),
		"coast_pages_pending %d" % int(stats.get("coast_pages_pending", 0)),
		"coast_from_ancestor %d" % int(stats.get("coast_from_ancestor", 0)),
		"coast_layers %d" % int(stats.get("coast_layers", 0)),
		"coast_pages %d" % int(stats.get("coast_pages", 0)),
		"coast_without_layer %d" % int(stats.get("coast_without_layer", 0)),
	])
	var file := FileAccess.open(output.path_join(name + ".txt"), FileAccess.WRITE)
	file.store_string("\n".join(lines) + "\n")
	file.close()
	print("captured %s at %.0f km (%s), %d tiles drawn" % [name, altitude_km, globe.get_current_band(),
		int(stats.get("drawn", 0))])


## Numbers at full double precision, space-separated.
func _numbers(values: Array) -> String:
	var parts := PackedStringArray()
	for value in values:
		parts.append(String.num(float(value), 17))
	return " ".join(parts)
