extends SceneTree

## R1-04 step 0, depth check: from 100 m, two flat planes 0.1 m apart (red
## above green), seen with the spike's near and far planes. Reports, by
## distance band, the share of ground pixels where the upper plane wins;
## z-fighting shows as green. The upper plane is drawn first, so a depth
## tie is lost, not won by draw order.

const ALTITUDE := 100.0
const GAP := 0.1
const NEAR := 0.5
const FAR := 43_835.0
const BANDS := [100.0, 300.0, 1000.0, 3000.0, 10000.0, 30000.0]


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color.BLACK
	var world_environment := WorldEnvironment.new()
	world_environment.environment = environment
	root.add_child(world_environment)
	var camera := Camera3D.new()
	camera.fov = 60.0
	camera.near = NEAR
	camera.far = FAR
	root.add_child(camera)
	camera.current = true
	camera.transform = Transform3D(Basis.from_euler(Vector3(-0.35, 0.0, 0.0)), Vector3(0.0, ALTITUDE, 0.0))
	for layer in [[0.0, Color(0.0, 1.0, 0.0)], [GAP, Color(1.0, 0.0, 0.0)]]:
		var plane := MeshInstance3D.new()
		var mesh := PlaneMesh.new()
		mesh.size = Vector2(80_000.0, 80_000.0)
		mesh.subdivide_width = 200
		mesh.subdivide_depth = 200
		plane.mesh = mesh
		plane.position = Vector3(0.0, layer[0], 0.0)
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.albedo_color = layer[1]
		# The upper plane draws first, so where depth cannot separate the two
		# (a tie) the lower one wins and shows.
		material.render_priority = 1 if layer[0] == 0.0 else -1
		plane.material_override = material
		root.add_child(plane)
	for frame in 4:
		await process_frame
	var image := root.get_texture().get_image()
	var red := {}
	var total := {}
	for y in range(0, image.get_height(), 2):
		for x in range(0, image.get_width(), 4):
			var colour := image.get_pixel(x, y)
			if colour.r < 0.5 and colour.g < 0.5:
				continue
			var hit := camera.project_ray_normal(Vector2(x, y))
			if hit.y >= 0.0:
				continue
			var distance := ALTITUDE / -hit.y
			for band in BANDS:
				if distance < band:
					total[band] = total.get(band, 0) + 1
					red[band] = red.get(band, 0) + (1 if colour.r > 0.5 else 0)
					break
	var report := PackedStringArray()
	for band in BANDS:
		if total.has(band):
			report.append("<%d m: %.4f" % [band, float(red[band]) / total[band]])
	print("depth probe: upper plane wins %s" % ", ".join(report))
	quit(0)
