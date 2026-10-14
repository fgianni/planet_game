extends SceneTree

## R1-04 step 0: the precision spike (throwaway; see the task's report).
##
## A flat-shaded 600 m patch of about 1 m relief on an Earth-radius sphere,
## with bright 2 m markers and a strip 0.1 m above the ground. A camera
## flies from 20 000 km to 100 m, then holds still, turns, and creeps at
## walking pace. For every frame at 100 m each marker is projected in 64-bit
## arithmetic (GDScript floats) and compared with its centroid in the
## rendered frame; jitter is the frame-to-frame part of that offset.
##
##   --mode=absolute   world coordinates as they are (in a precision=double
##                     build this is ADR-0022 option b, large-world coordinates)
##   --mode=relative   camera at the origin; the patch is placed at
##                     origin - camera, computed in double (option a)
##   --output=DIR      report.json and sample frames
##
## Run at 1080p: --resolution 1920x1080.

const R := 6_371_000.0 # m
const PATCH_HALF := 300.0 # m
const GRID := 2.0 # m
const MARKER_EVERY := 10 # grid cells between markers
const ROAD_Z := -40.0 # m, along x, in front of the hold position
const ROAD_HALF_WIDTH := 1.5
const ROAD_LIFT := 0.1 # m above the terrain
const HOLD_ALTITUDE := 100.0
const HOLD_BACK := 120.0 # m behind the patch centre
const PITCH := -0.45 # rad, looking down
const FOV := 60.0
const MAX_DEPTH := 150.0 # m; farther markers are too few pixels to centre
const MIN_BLOB_PIXELS := 40

var _mode := "relative"
var _msaa := 0
var _output := ""
var _camera: Camera3D
var _patch: MeshInstance3D
var _road: MeshInstance3D
var _road_material: ShaderMaterial
var _earth: MeshInstance3D
var _sun: DirectionalLight3D
# The tile sits at a generic place, so every world coordinate is large
# (about 4e6 m); its local frame has x east, y up (radial), z south.
var _up := _normalized([0.48, 0.62, 0.62])
var _east := _normalized(_cross([0.0, 1.0, 0.0], _up))
var _south := _cross(_east, _up)
var _origin := [_up[0] * R, _up[1] * R, _up[2] * R] # in double
var _camera_local := [0.0, 2.0e7, 0.0] # camera in the tile's frame, double
var _camera_position := [0.0, 0.0, 0.0] # in the world, double
var _yaw := 0.0
var _pitch := -1.5
var _markers: Array = [] # local [x, y, z] of each marker centre
var _gpu_ms: Array = []
var _cpu_ms: Array = []


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--mode="):
			_mode = argument.trim_prefix("--mode=")
		elif argument.begins_with("--msaa="):
			_msaa = argument.trim_prefix("--msaa=").to_int()
		elif argument.begins_with("--output="):
			_output = argument.trim_prefix("--output=")
	if not _output.is_empty():
		DirAccess.make_dir_recursive_absolute(_output)
	_build()
	root.msaa_3d = {0: Viewport.MSAA_DISABLED, 2: Viewport.MSAA_2X, 4: Viewport.MSAA_4X, 8: Viewport.MSAA_8X}[_msaa]
	RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(), true)

	# The flight: 20 000 km down to 100 m, exponentially, in 240 frames.
	var flight_frames := 240
	for frame in flight_frames + 1:
		var t := float(frame) / flight_frames
		var altitude := exp(lerp(log(2.0e7), log(HOLD_ALTITUDE), t))
		var back := HOLD_BACK * smoothstep(0.6, 1.0, t)
		_pitch = lerp(-1.5, PITCH, smoothstep(0.5, 1.0, t))
		_camera_local = [0.0, altitude, back]
		_place()
		await process_frame
		if frame in [0, 160, 240]:
			_save(root.get_texture().get_image(), "flight_%03d" % frame)

	var report := {
		"mode": _mode,
		"msaa": _msaa,
		"godot": Engine.get_version_info().string,
		"precision_double": OS.has_feature("double"),
		"renderer": RenderingServer.get_video_adapter_name(),
		"resolution": [root.size.x, root.size.y],
		"near_m": _camera.near,
		"far_m": _camera.far,
	}
	_gpu_ms.clear()
	_cpu_ms.clear()

	# Z-fighting: the strip's footprint with the depth test off, against the
	# strip as drawn.
	await _settle()
	var drawn := root.get_texture().get_image()
	var footprint_material := ShaderMaterial.new()
	var footprint_shader := Shader.new()
	footprint_shader.code = "shader_type spatial;\nrender_mode unshaded, cull_disabled, depth_test_disabled;\nvoid fragment(){ ALBEDO = vec3(1.0, 0.0, 0.0); }"
	footprint_material.shader = footprint_shader
	_road.material_override = footprint_material
	await _settle()
	var footprint := root.get_texture().get_image()
	_road.material_override = _road_material
	await _settle()
	report["z_fighting"] = _z_fighting(footprint, drawn)
	_save(footprint, "strip_footprint")
	_save(drawn, "hold_still")

	# Jitter while holding: still, turning, creeping.
	report["still"] = await _hold(30, 0.0, 0.0, "still")
	report["turning"] = await _hold(120, deg_to_rad(0.25), 0.0, "turning")
	report["creeping"] = await _hold(120, 0.0, 0.01, "creeping")
	report["gpu_frame_ms"] = _percentiles(_gpu_ms)
	report["cpu_frame_ms"] = _percentiles(_cpu_ms)

	print(JSON.stringify(report, "  "))
	if not _output.is_empty():
		var file := FileAccess.open(_output.path_join("report_%s_msaa%d.json" % [_mode, _msaa]), FileAccess.WRITE)
		file.store_string(JSON.stringify(report, "  ") + "\n")
		file.close()
	quit(0)


## Holds at 100 m for some frames, turning by yaw_step and moving by
## creep_step metres along x each frame; returns the jitter.
func _hold(frames: int, yaw_step: float, creep_step: float, label: String) -> Dictionary:
	var tracks := {} # marker index -> Array of residual Vector2
	for frame in frames:
		_yaw += yaw_step
		# Creep along a direction with no small world component.
		_camera_local[0] += creep_step * 0.8
		_camera_local[2] += creep_step * 0.6
		_place()
		await process_frame
		_gpu_ms.append(RenderingServer.viewport_get_measured_render_time_gpu(root.get_viewport_rid()))
		_cpu_ms.append(RenderingServer.viewport_get_measured_render_time_cpu(root.get_viewport_rid()))
		var image := root.get_texture().get_image()
		if frame == frames / 2:
			_save(image, "hold_" + label)
		for index in _markers.size():
			var predicted := _project(_markers[index])
			if predicted.x < 0.0:
				continue
			var found := _centroid(image, predicted)
			if found.x < 0.0:
				continue
			if not tracks.has(index):
				tracks[index] = []
			tracks[index].append(found - predicted)
	# Jitter: each residual against the mean of its neighbours (+-4 frames),
	# which removes the slow perspective bias of a centroid.
	var worst := 0.0
	var squares := 0.0
	var samples := 0
	for index in tracks:
		var track: Array = tracks[index]
		if track.size() < 9:
			continue
		for i in range(4, track.size() - 4):
			var mean := Vector2.ZERO
			for j in range(i - 4, i + 5):
				mean += track[j]
			mean /= 9.0
			var deviation: float = (track[i] - mean).length()
			worst = max(worst, deviation)
			squares += deviation * deviation
			samples += 1
	return {
		"frames": frames,
		"markers_tracked": tracks.size(),
		"jitter_max_px": worst,
		"jitter_rms_px": sqrt(squares / max(samples, 1)),
	}


func _settle() -> void:
	for frame in 3:
		await process_frame


## Positions everything for the current double-precision camera.
func _place() -> void:
	_camera_position = _to_world(_camera_local)
	var forward := _forward()
	var right := _normalized(_cross(forward, _up))
	var up := _cross(right, forward)
	var basis := Basis(Vector3(right[0], right[1], right[2]), Vector3(up[0], up[1], up[2]),
		Vector3(-forward[0], -forward[1], -forward[2]))
	var altitude: float = _camera_local[1]
	_camera.near = clamp(altitude * 0.005, 0.1, 1.0e4)
	_camera.far = sqrt(2.0 * R * altitude + altitude * altitude) * 1.2 + 1000.0
	var tile_basis := Basis(_vector(_east), _vector(_up), _vector(_south))
	if _mode == "relative":
		_camera.transform = Transform3D(basis, Vector3.ZERO)
		_patch.transform = Transform3D(tile_basis, _vector(_minus(_origin, _camera_position)))
		_earth.position = _vector(_minus([0.0, 0.0, 0.0], _camera_position))
	else:
		_camera.transform = Transform3D(basis, _vector(_camera_position))
		_patch.transform = Transform3D(tile_basis, _vector(_origin))
		_earth.position = Vector3.ZERO
	_road.transform = _patch.transform


## Screen position of a patch-local point, in double, or (-1, -1).
func _project(local: Array) -> Vector2:
	var forward := _forward()
	var right := _normalized(_cross(forward, _up))
	var up := _cross(right, forward)
	var d := _minus(_to_world(local), _camera_position)
	var depth := _dot(d, forward)
	if depth < 10.0 or depth > MAX_DEPTH:
		return Vector2(-1.0, -1.0)
	var focal: float = root.size.y * 0.5 / tan(deg_to_rad(FOV) * 0.5)
	var screen := Vector2(root.size.x * 0.5 + _dot(d, right) / depth * focal,
		root.size.y * 0.5 - _dot(d, up) / depth * focal)
	if screen.x < 40.0 or screen.y < 40.0 or screen.x > root.size.x - 40.0 or screen.y > root.size.y - 40.0:
		return Vector2(-1.0, -1.0)
	return screen


## Centroid of marker-white pixels within 14 px of a point, or (-1, -1).
## Centroid of marker pixels within 16 px of a point, or (-1, -1) when the
## blob is small (aliasing dominates) or touches the window's edge (clipped,
## or a neighbour).
func _centroid(image: Image, around: Vector2) -> Vector2:
	var sum := Vector2.ZERO
	var weight := 0.0
	var pixels := 0
	var x0 := int(around.x) - 16
	var y0 := int(around.y) - 16
	for y in range(y0, y0 + 33):
		for x in range(x0, x0 + 33):
			# Blue rises from about 0.3 on the ground to 1.0 on a marker;
			# the ramp weights partly covered edge pixels.
			var w: float = clamp((image.get_pixel(x, y).b - 0.5) / 0.5, 0.0, 1.0)
			if w > 0.0:
				if x == x0 or y == y0 or x == x0 + 32 or y == y0 + 32:
					return Vector2(-1.0, -1.0)
				sum += Vector2(x + 0.5, y + 0.5) * w
				weight += w
				pixels += 1
	return sum / weight if pixels >= MIN_BLOB_PIXELS else Vector2(-1.0, -1.0)


func _z_fighting(footprint: Image, drawn: Image) -> Dictionary:
	var road := 0
	var lost := 0
	for y in range(0, footprint.get_height(), 2):
		for x in range(0, footprint.get_width(), 2):
			if _is_road(footprint.get_pixel(x, y)):
				road += 1
				if not _is_road(drawn.get_pixel(x, y)):
					lost += 1
	return {"strip_pixels": road, "lost_fraction": float(lost) / max(road, 1)}


func _is_road(colour: Color) -> bool:
	return colour.r > 0.8 and colour.g < 0.2 and colour.b < 0.2


func _build() -> void:
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.02, 0.02, 0.05)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.4, 0.42, 0.45)
	var world_environment := WorldEnvironment.new()
	world_environment.environment = environment
	root.add_child(world_environment)

	_camera = Camera3D.new()
	_camera.fov = FOV
	root.add_child(_camera)
	_camera.current = true

	_sun = DirectionalLight3D.new()
	_sun.shadow_enabled = true
	_sun.light_energy = 1.2
	_sun.transform = Transform3D(Basis.looking_at(Vector3(0.6, -0.35, -0.5).normalized(), Vector3.UP))
	root.add_child(_sun)

	_earth = MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = R - 30.0
	sphere.height = 2.0 * (R - 30.0)
	sphere.radial_segments = 128
	sphere.rings = 64
	_earth.mesh = sphere
	var earth_material := StandardMaterial3D.new()
	earth_material.albedo_color = Color(0.15, 0.3, 0.45)
	_earth.material_override = earth_material
	root.add_child(_earth)

	_patch = MeshInstance3D.new()
	_patch.mesh = _terrain_mesh(0.0, true)
	var patch_material := StandardMaterial3D.new()
	patch_material.vertex_color_use_as_albedo = true
	patch_material.emission_enabled = false
	_patch.material_override = patch_material
	root.add_child(_patch)

	var marker_material := ShaderMaterial.new()
	var marker_shader := Shader.new()
	marker_shader.code = "shader_type spatial;\nrender_mode unshaded, cull_disabled;\nvoid fragment(){ ALBEDO = COLOR.rgb; }"
	marker_material.shader = marker_shader
	var markers := MeshInstance3D.new()
	markers.mesh = _marker_mesh()
	markers.material_override = marker_material
	_patch.add_child(markers)

	_road = MeshInstance3D.new()
	_road.mesh = _road_mesh()
	_road_material = ShaderMaterial.new()
	var road_shader := Shader.new()
	road_shader.code = "shader_type spatial;\nrender_mode unshaded, cull_disabled;\nvoid fragment(){ ALBEDO = vec3(1.0, 0.0, 0.0); }"
	_road_material.shader = road_shader
	_road.material_override = _road_material
	root.add_child(_road)


func _height(x: float, z: float) -> float:
	# About 1 m of relief, with the sphere's curvature.
	return 0.5 * sin(x / 7.0) * cos(z / 9.0) + 0.3 * sin((x + 2.0 * z) / 3.1) \
		- (x * x + z * z) / (2.0 * R)


## Flat-shaded terrain: every triangle has its own vertices and face normal.
func _terrain_mesh(lift: float, with_colour: bool) -> ArrayMesh:
	var tool := SurfaceTool.new()
	tool.begin(Mesh.PRIMITIVE_TRIANGLES)
	var cells := int(2.0 * PATCH_HALF / GRID)
	for j in cells:
		for i in cells:
			var x0 := -PATCH_HALF + i * GRID
			var z0 := -PATCH_HALF + j * GRID
			var corners := [
				Vector3(x0, _height(x0, z0) + lift, z0),
				Vector3(x0 + GRID, _height(x0 + GRID, z0) + lift, z0),
				Vector3(x0 + GRID, _height(x0 + GRID, z0 + GRID) + lift, z0 + GRID),
				Vector3(x0, _height(x0, z0 + GRID) + lift, z0 + GRID),
			]
			var colour := Color(0.42, 0.40, 0.33) if (i + j) % 2 == 0 else Color(0.36, 0.38, 0.30)
			# Clockwise seen from above: Godot's front faces.
			for triangle in [[0, 1, 2], [0, 2, 3]]:
				var a: Vector3 = corners[triangle[0]]
				var b: Vector3 = corners[triangle[1]]
				var c: Vector3 = corners[triangle[2]]
				var normal := (b - a).cross(c - a).normalized()
				if normal.y < 0.0:
					normal = -normal
				for v in [a, b, c]:
					tool.set_normal(normal)
					if with_colour:
						tool.set_color(colour)
					tool.add_vertex(v)
	return tool.commit()


## White 2 m squares, 5 cm above the ground, every MARKER_EVERY cells.
func _marker_mesh() -> ArrayMesh:
	var tool := SurfaceTool.new()
	tool.begin(Mesh.PRIMITIVE_TRIANGLES)
	var cells := int(2.0 * PATCH_HALF / GRID)
	for j in range(0, cells, MARKER_EVERY):
		for i in range(0, cells, MARKER_EVERY):
			var x0 := -PATCH_HALF + i * GRID
			var z0 := -PATCH_HALF + j * GRID
			if abs(z0 - ROAD_Z) < 6.0:
				continue
			var cx := x0 + GRID * 0.5
			var cz := z0 + GRID * 0.5
			_markers.append([cx, _height(cx, cz) + 0.05, cz])
			var corners := [
				Vector3(x0, _height(x0, z0) + 0.05, z0),
				Vector3(x0 + GRID, _height(x0 + GRID, z0) + 0.05, z0),
				Vector3(x0 + GRID, _height(x0 + GRID, z0 + GRID) + 0.05, z0 + GRID),
				Vector3(x0, _height(x0, z0 + GRID) + 0.05, z0 + GRID),
			]
			for v in [corners[0], corners[2], corners[1], corners[0], corners[3], corners[2]]:
				tool.set_color(Color(0.3, 0.9, 1.0))
				tool.add_vertex(v)
	return tool.commit()


## A strip ROAD_LIFT above the terrain, following its triangles.
func _road_mesh() -> ArrayMesh:
	var tool := SurfaceTool.new()
	tool.begin(Mesh.PRIMITIVE_TRIANGLES)
	var x := -PATCH_HALF
	while x < PATCH_HALF:
		var z0 := ROAD_Z - ROAD_HALF_WIDTH
		var z1 := ROAD_Z + ROAD_HALF_WIDTH
		var a := Vector3(x, _height(x, z0) + ROAD_LIFT, z0)
		var b := Vector3(x + GRID, _height(x + GRID, z0) + ROAD_LIFT, z0)
		var c := Vector3(x + GRID, _height(x + GRID, z1) + ROAD_LIFT, z1)
		var d := Vector3(x, _height(x, z1) + ROAD_LIFT, z1)
		for v in [a, c, b, a, d, c]:
			tool.add_vertex(v)
		x += GRID
	return tool.commit()


## The camera's forward, in the world: yaw about the tile's up, pitch from
## its horizon, looking towards local -z at yaw 0.
func _forward() -> Array:
	return _rotate([-sin(_yaw) * cos(_pitch), sin(_pitch), -cos(_yaw) * cos(_pitch)])


## A tile-local direction in the world.
func _rotate(v: Array) -> Array:
	return [_east[0] * v[0] + _up[0] * v[1] + _south[0] * v[2],
		_east[1] * v[0] + _up[1] * v[1] + _south[1] * v[2],
		_east[2] * v[0] + _up[2] * v[1] + _south[2] * v[2]]


## A tile-local point in the world, in double.
func _to_world(local: Array) -> Array:
	var v := _rotate(local)
	return [_origin[0] + v[0], _origin[1] + v[1], _origin[2] + v[2]]


func _minus(a: Array, b: Array) -> Array:
	return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


func _dot(a: Array, b: Array) -> float:
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


func _cross(a: Array, b: Array) -> Array:
	return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


func _normalized(a: Array) -> Array:
	var length := sqrt(_dot(a, a))
	return [a[0] / length, a[1] / length, a[2] / length]


func _vector(a: Array) -> Vector3:
	return Vector3(a[0], a[1], a[2])


func _percentiles(values: Array) -> Dictionary:
	var sorted := values.duplicate()
	sorted.sort()
	if sorted.is_empty():
		return {}
	return {
		"p50": sorted[int(0.5 * (sorted.size() - 1))],
		"p95": sorted[int(0.95 * (sorted.size() - 1))],
		"max": sorted[sorted.size() - 1],
	}


func _save(image: Image, name: String) -> void:
	if not _output.is_empty():
		image.save_png(_output.path_join("%s_msaa%d_%s.png" % [_mode, _msaa, name]))
