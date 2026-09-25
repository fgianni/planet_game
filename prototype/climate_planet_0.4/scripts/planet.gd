class_name Planet
extends Node3D
## Builds a small stylised planet in code and updates its look from a ClimateLook dictionary.

@export var seed := 7
@export_range(3, 6) var subdivisions := 5
@export var land_bias := -0.05           # raise to get more land
@export var tree_count := 1400
@export var spin_speed := 0.05           # radians per second, 0 to stop

const BASE_SEA := 1.004
const CLOUD_R := 1.085
const ATMO_R := 1.1

var noise := FastNoiseLite.new()
var body: Node3D                          # rotating part: land, sea, trees, cities, events
var terrain_mat: ShaderMaterial
var ocean_mat: ShaderMaterial
var cloud_mat: ShaderMaterial
var atmo_mat: ShaderMaterial
var ocean: MeshInstance3D
var clouds: MeshInstance3D

var trees: Array = []                     # [{dir, r, thr, var, basis}]
var canopies: MultiMesh
var trunks: MultiMesh
var smokestacks: Array[CPUParticles3D] = []
var storms: Array[MeshInstance3D] = []
var fires: Array[Node3D] = []
var look: Dictionary = {}
var tree_health := PackedFloat32Array()   # optional per-tree health from the climate model (empty = global)
var tree_cells := PackedInt32Array()
var wind_fn := Callable()                # dir -> tangent wind (m/s); set by the sandbox
var sst_fn := Callable()                 # dir -> sea surface °C, NAN over land
var storm_dirs: Array[Vector3] = []
var storm_dying: Array[bool] = []
var storm_spin: Array[float] = []
var _storm_rng := RandomNumberGenerator.new()
var climate_mode := false                 # true once a climate texture drives the shaders
var _manual_storm := 0.0
var _manual_fire := 0.0


func _init() -> void:
	setup_noise()


## Configures the terrain noise; also usable without adding the planet to a scene (tests, model).
func setup_noise() -> void:
	noise.seed = seed
	noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	noise.frequency = 0.9
	noise.fractal_octaves = 5
	noise.fractal_gain = 0.5


func _ready() -> void:
	setup_noise()
	body = Node3D.new()
	body.name = "Body"
	add_child(body)
	_build_terrain()
	_build_ocean()
	_build_trees()
	_build_cities()
	_build_events()
	_build_sky_layers()
	apply_look(ClimateLook.at(0.0))


func _process(delta: float) -> void:
	body.rotate_y(spin_speed * delta)
	if climate_mode:
		clouds.rotation = body.rotation            # clouds follow the climate map
	else:
		clouds.rotate_y(spin_speed * delta * 0.6)
	_manual_storm = maxf(0.0, _manual_storm - delta * 0.12)
	_manual_fire = maxf(0.0, _manual_fire - delta * 0.12)
	for i in storms.size():
		var s := storms[i]
		if not climate_mode:
			s.rotate_object_local(Vector3.BACK, -delta * 0.8)
		var want := 1.0 if i < int(look.get("storms", 0)) else 0.0
		if i == storms.size() - 1:
			want = maxf(want, minf(1.0, _manual_storm * 3.0))
		var m := s.material_override as ShaderMaterial
		var cur: float = m.get_shader_parameter("strength")
		if climate_mode and wind_fn.is_valid():
			want = _steer_storm(i, delta, want, cur)
		m.set_shader_parameter("strength", move_toward(cur, want, delta * 0.8))
		s.visible = cur > 0.01 or want > 0.0
	for i in fires.size():
		var on := i < int(look.get("fires", 0)) or (i == fires.size() - 1 and _manual_fire > 0.0)
		for p in fires[i].get_children():
			(p as CPUParticles3D).emitting = on


## Terrain radius for a unit direction. Low land stays flat so rising seas visibly drown coasts.
func radius_at(dir: Vector3) -> float:
	var e := noise.get_noise_3dv(dir * 1.6) + land_bias
	if e < 0.0:
		return BASE_SEA - 0.006 + e * 0.08       # sea floor
	if e < 0.05:
		return BASE_SEA + e * 0.2                # flat coastal lowlands: these flood first
	return BASE_SEA + 0.01 + (e - 0.05) * 0.12   # hills and mountains


func _build_terrain() -> void:
	var ico := _icosphere(subdivisions)
	var verts: PackedVector3Array = ico[0]
	var idx: PackedInt32Array = ico[1]
	for i in verts.size():
		verts[i] = verts[i] * radius_at(verts[i])
	var normals := PackedVector3Array()
	normals.resize(verts.size())
	for f in range(0, idx.size(), 3):
		var a := verts[idx[f]]
		var b := verts[idx[f + 1]]
		var c := verts[idx[f + 2]]
		var fn := (b - a).cross(c - a)
		for j in 3:
			normals[idx[f + j]] += fn
	for i in normals.size():
		normals[i] = normals[i].normalized()
		if normals[i].dot(verts[i]) < 0.0:
			normals[i] = -normals[i]
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	terrain_mat = ShaderMaterial.new()
	terrain_mat.shader = load("res://shaders/terrain.gdshader")
	terrain_mat.set_shader_parameter("base_sea", BASE_SEA)
	terrain_mat.set_shader_parameter("rock_height", BASE_SEA + 0.035)
	var mi := MeshInstance3D.new()
	mi.name = "Terrain"
	mi.mesh = mesh
	mi.material_override = terrain_mat
	body.add_child(mi)


func _build_ocean() -> void:
	var sm := SphereMesh.new()
	sm.radius = 1.0
	sm.height = 2.0
	sm.radial_segments = 96
	sm.rings = 48
	ocean_mat = ShaderMaterial.new()
	ocean_mat.shader = load("res://shaders/ocean.gdshader")
	ocean_mat.render_priority = 0
	ocean = MeshInstance3D.new()
	ocean.name = "Ocean"
	ocean.mesh = sm
	ocean.material_override = ocean_mat
	body.add_child(ocean)


func _build_sky_layers() -> void:
	var cm := SphereMesh.new()
	cm.radius = CLOUD_R
	cm.height = CLOUD_R * 2.0
	cm.radial_segments = 96
	cm.rings = 48
	cloud_mat = ShaderMaterial.new()
	cloud_mat.shader = load("res://shaders/clouds.gdshader")
	cloud_mat.render_priority = 1
	clouds = MeshInstance3D.new()
	clouds.name = "Clouds"
	clouds.mesh = cm
	clouds.material_override = cloud_mat
	add_child(clouds)
	var am := SphereMesh.new()
	am.radius = ATMO_R
	am.height = ATMO_R * 2.0
	am.radial_segments = 96
	am.rings = 48
	atmo_mat = ShaderMaterial.new()
	atmo_mat.shader = load("res://shaders/atmosphere.gdshader")
	atmo_mat.render_priority = 2
	var atmo := MeshInstance3D.new()
	atmo.name = "Atmosphere"
	atmo.mesh = am
	atmo.material_override = atmo_mat
	add_child(atmo)


# ---------- Trees: lollipop trees in two MultiMeshes (trunks + canopies) ----------
func _build_trees() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = seed * 13 + 1
	var tries := 0
	while trees.size() < tree_count and tries < tree_count * 60:
		tries += 1
		var dir := Vector3(rng.randfn(), rng.randfn(), rng.randfn()).normalized()
		var r := radius_at(dir)
		if r < BASE_SEA + 0.003 or r > BASE_SEA + 0.03 or absf(dir.y) > 0.7:
			continue
		var basis := _basis_up(dir).rotated(dir, rng.randf() * TAU)
		trees.append({ "dir": dir, "r": r, "thr": rng.randf(), "var": rng.randf(), "basis": basis, "s": rng.randf_range(0.8, 1.25) })
	var canopy := SphereMesh.new()
	canopy.radius = 0.0075
	canopy.height = 0.015
	canopy.radial_segments = 8
	canopy.rings = 4
	var trunk := CylinderMesh.new()
	trunk.top_radius = 0.0016
	trunk.bottom_radius = 0.002
	trunk.height = 0.008
	trunk.radial_segments = 5
	trunk.rings = 1
	canopies = _multimesh(canopy, trees.size(), true, Color.WHITE, "Canopies")
	trunks = _multimesh(trunk, trees.size(), false, Color("#7a4f2e"), "Trunks")


func _multimesh(mesh: Mesh, count: int, colors: bool, base: Color, node_name: String) -> MultiMesh:
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_colors = colors
	mm.mesh = mesh
	mm.instance_count = count
	var mat := StandardMaterial3D.new()
	mat.albedo_color = base
	mat.vertex_color_use_as_albedo = colors
	mat.roughness = 0.85
	var mmi := MultiMeshInstance3D.new()
	mmi.name = node_name
	mmi.multimesh = mm
	mmi.material_override = mat
	body.add_child(mmi)
	return mm


func _update_trees() -> void:
	var sea: float = BASE_SEA + look["sea_rise"]
	var health: float = look["forest"]
	var ice: float = look["ice_lat"]
	var green := Color("#2f9e4f")
	var sick := Color("#a88a3b")
	var dead := Color("#8d8577")
	for i in trees.size():
		var tr: Dictionary = trees[i]
		var dir: Vector3 = tr["dir"]
		var h := health if tree_health.is_empty() else tree_health[i]
		var ice_ok := absf(dir.y) < ice - 0.03 if tree_health.is_empty() else true
		var alive: bool = tr["thr"] < h and tr["r"] > sea + 0.0015 and ice_ok
		var s: float = tr["s"] if alive else 0.0001
		var b: Basis = (tr["basis"] as Basis).scaled(Vector3(s, s, s))
		var base_pos: Vector3 = dir * tr["r"]
		trunks.set_instance_transform(i, Transform3D(b, base_pos + dir * 0.004 * s))
		canopies.set_instance_transform(i, Transform3D(b, base_pos + dir * 0.0115 * s))
		var stress := clampf((1.0 - h) * 1.4 + (tr["var"] - 0.5) * 0.5, 0.0, 1.0)
		var c := green.lerp(sick, stress)
		if stress > 0.85:
			c = sick.lerp(dead, (stress - 0.85) / 0.15)
		canopies.set_instance_color(i, c.lightened((tr["var"] - 0.5) * 0.15))


# ---------- Cities: pastel blocks; some sit on low coasts and will flood ----------
func _build_cities() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = seed * 31 + 5
	var sites: Array = []
	var want := [[BASE_SEA + 0.001, BASE_SEA + 0.006], [BASE_SEA + 0.001, BASE_SEA + 0.008],
			[BASE_SEA + 0.01, BASE_SEA + 0.03], [BASE_SEA + 0.01, BASE_SEA + 0.03], [BASE_SEA + 0.006, BASE_SEA + 0.03]]
	for w in want:
		for attempt in 4000:
			var dir := Vector3(rng.randfn(), rng.randfn() * 0.6, rng.randfn()).normalized()
			var r := radius_at(dir)
			if r < w[0] or r > w[1] or absf(dir.y) > 0.6:
				continue
			var ok := true
			for s in sites:
				if (s as Vector3).distance_to(dir) < 0.35:
					ok = false
			if ok:
				sites.append(dir)
				break
	var box := BoxMesh.new()
	box.size = Vector3(0.008, 1.0, 0.008)
	var n_blocks := sites.size() * 9
	var mm := _multimesh(box, n_blocks, true, Color.WHITE, "Buildings")
	var pastel := [Color("#f7a8b8"), Color("#9ad0f5"), Color("#ffd27d"), Color("#c7b3f5"), Color("#a6e3b6"), Color("#ffb38a")]
	var k := 0
	for si in sites.size():
		var dir: Vector3 = sites[si]
		var basis := _basis_up(dir)
		for j in 9:
			var off := basis.x * rng.randf_range(-0.022, 0.022) + basis.z * rng.randf_range(-0.022, 0.022)
			var d := (dir + off).normalized()
			var h := rng.randf_range(0.006, 0.02)
			var bu := _basis_up(d)
			var b := Basis(bu.x, bu.y * h, bu.z)
			mm.set_instance_transform(k, Transform3D(b, d * (radius_at(d) + h * 0.5 - 0.001)))
			mm.set_instance_color(k, pastel[rng.randi() % pastel.size()])
			k += 1
		if si >= 2 and si <= 3:
			_add_factory(dir + basis.x * 0.03, rng)
	# hide unused slots, if any
	for j in range(k, n_blocks):
		mm.set_instance_transform(j, Transform3D(Basis().scaled(Vector3.ONE * 0.0001), Vector3.ZERO))


func _add_factory(at: Vector3, _rng: RandomNumberGenerator) -> void:
	var dir := at.normalized()
	var node := Node3D.new()
	node.name = "Factory"
	node.transform = Transform3D(_basis_up(dir), dir * radius_at(dir))
	body.add_child(node)
	var stack := MeshInstance3D.new()
	var cyl := CylinderMesh.new()
	cyl.top_radius = 0.0025
	cyl.bottom_radius = 0.003
	cyl.height = 0.03
	stack.mesh = cyl
	stack.position = Vector3(0, 0.015, 0)
	var sm := StandardMaterial3D.new()
	sm.albedo_color = Color("#8c7c78")
	stack.material_override = sm
	node.add_child(stack)
	var smoke := _particles(Color(0.55, 0.52, 0.5, 0.75), Color(0.45, 0.42, 0.42, 0.0), 0.01, 0.022, 2.6, 18)
	smoke.position = Vector3(0, 0.032, 0)
	node.add_child(smoke)
	smokestacks.append(smoke)


func _particles(c0: Color, c1: Color, size0: float, size1: float, life: float, amount: int) -> CPUParticles3D:
	var p := CPUParticles3D.new()
	p.local_coords = true
	p.amount = amount
	p.lifetime = life
	p.direction = Vector3.UP
	p.spread = 12.0
	p.gravity = Vector3(0.0, 0.004, 0.0)
	p.initial_velocity_min = 0.012
	p.initial_velocity_max = 0.02
	var mesh := SphereMesh.new()
	mesh.radius = 0.5
	mesh.height = 1.0
	mesh.radial_segments = 6
	mesh.rings = 3
	var mat := StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mesh.material = mat
	p.mesh = mesh
	var grad := Gradient.new()
	grad.set_color(0, c0)
	grad.set_color(1, c1)
	p.color_ramp = grad
	var curve := Curve.new()
	curve.add_point(Vector2(0.0, size0 / size1))
	curve.add_point(Vector2(1.0, 1.0))
	p.scale_amount_curve = curve
	p.scale_amount_min = size1
	p.scale_amount_max = size1
	return p


# ---------- Events: storms over the sea, fires in forests ----------
func _build_events() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = seed * 7 + 3
	var quad := QuadMesh.new()
	quad.size = Vector2(0.3, 0.3)
	var storm_shader := load("res://shaders/storm.gdshader")
	for i in 3:
		var dir := _random_dir_where(rng, func(r: float, d: Vector3) -> bool: return r < 0.99 and absf(d.y) < 0.45)
		var mi := MeshInstance3D.new()
		mi.name = "Storm%d" % i
		mi.mesh = quad
		var m := ShaderMaterial.new()
		m.shader = storm_shader
		m.render_priority = 3
		m.set_shader_parameter("strength", 0.0)
		mi.material_override = m
		mi.transform = Transform3D(_basis_out(dir), dir * (CLOUD_R + 0.004))
		mi.visible = false
		body.add_child(mi)
		storms.append(mi)
	for i in 4:
		var dir := _random_dir_where(rng, func(r: float, d: Vector3) -> bool: return r > BASE_SEA + 0.006 and r < BASE_SEA + 0.028 and absf(d.y) < 0.55)
		var node := Node3D.new()
		node.name = "Fire%d" % i
		node.transform = Transform3D(_basis_up(dir), dir * radius_at(dir))
		body.add_child(node)
		var flames := _particles(Color("#ffd34d"), Color(1.0, 0.3, 0.1, 0.0), 0.012, 0.02, 0.7, 24)
		flames.emission_shape = CPUParticles3D.EMISSION_SHAPE_SPHERE
		flames.emission_sphere_radius = 0.012
		var smoke := _particles(Color(0.35, 0.3, 0.32, 0.7), Color(0.3, 0.28, 0.3, 0.0), 0.015, 0.045, 3.0, 20)
		smoke.position = Vector3(0, 0.01, 0)
		node.add_child(flames)
		node.add_child(smoke)
		flames.emitting = false
		smoke.emitting = false
		fires.append(node)


func _random_dir_where(rng: RandomNumberGenerator, ok: Callable) -> Vector3:
	for attempt in 5000:
		var d := Vector3(rng.randfn(), rng.randfn(), rng.randfn()).normalized()
		if ok.call(radius_at(d), d):
			return d
	return Vector3.UP


## Hurricanes ride the trade winds, drift poleward (as real ones do) and die outside the tropics
## or over land; a new one then forms over warm tropical water.
func _steer_storm(i: int, delta: float, want: float, cur: float) -> float:
	if storm_dirs.size() != storms.size():
		for s in storms:
			storm_dirs.append((s.position as Vector3).normalized())
			storm_dying.append(false)
			storm_spin.append(0.0)
		_storm_rng.seed = seed * 17
	var d: Vector3 = storm_dirs[i]
	var vel: Vector3 = wind_fn.call(d)
	var north := d.cross(Vector3.UP.cross(d).normalized())       # local north on the surface
	vel += north * signf(d.y) * 1.5                               # poleward drift (beta drift)
	d = (d + vel * 0.012 * delta).normalized()
	storm_dirs[i] = d
	var sst: float = sst_fn.call(d) if sst_fn.is_valid() else 28.0
	if absf(d.y) > 0.6 or is_nan(sst):
		storm_dying[i] = true                                     # left the tropics or made landfall
	if storm_dying[i]:
		if cur < 0.02:
			storm_dying[i] = false
			storm_dirs[i] = _random_dir_where(_storm_rng, func(r: float, dd: Vector3) -> bool:
				return r < 0.99 and absf(dd.y) > 0.12 and absf(dd.y) < 0.4)
		want = 0.0
	storm_spin[i] += delta * 0.8
	storms[i].transform = Transform3D(_basis_out(storm_dirs[i]).rotated(storm_dirs[i], -storm_spin[i]), storm_dirs[i] * (CLOUD_R + 0.004))
	return want


## Trigger a one-off storm or fire from the UI.
func trigger(kind: String) -> void:
	if kind == "storm":
		_manual_storm = 1.0
	elif kind == "fire":
		_manual_fire = 1.0


## Switch the shaders to read ice, rain, temperature and clouds from a climate texture (see ClimateView).
func set_climate_texture(tex: Texture2D, wind_tex: Texture2D = null) -> void:
	climate_mode = tex != null
	for m in [terrain_mat, ocean_mat, cloud_mat]:
		(m as ShaderMaterial).set_shader_parameter("use_climate", tex != null)
		(m as ShaderMaterial).set_shader_parameter("climate_tex", tex)
	cloud_mat.set_shader_parameter("wind_tex", wind_tex)
	clouds.rotation = Vector3.ZERO


func tree_directions() -> PackedVector3Array:
	var out := PackedVector3Array()
	for tr in trees:
		out.append(tr["dir"])
	return out


func apply_look(l: Dictionary) -> void:
	look = l
	var sea: float = BASE_SEA + l["sea_rise"]
	ocean.scale = Vector3.ONE * sea
	terrain_mat.set_shader_parameter("sea_level", sea)
	terrain_mat.set_shader_parameter("ice_lat", l["ice_lat"])
	terrain_mat.set_shader_parameter("snow_height", l["snow_height"])
	terrain_mat.set_shader_parameter("desert", l["desert"])
	terrain_mat.set_shader_parameter("grass", l["grass"])
	terrain_mat.set_shader_parameter("dry", l["dry"])
	ocean_mat.set_shader_parameter("ice_lat", l["ice_lat"])
	ocean_mat.set_shader_parameter("deep", l["deep"])
	ocean_mat.set_shader_parameter("shallow", l["shallow"])
	cloud_mat.set_shader_parameter("cloud_color", l["cloud"])
	cloud_mat.set_shader_parameter("cover", l["cover"])
	cloud_mat.set_shader_parameter("haze", l["haze"])
	cloud_mat.set_shader_parameter("haze_color", l["haze_color"])
	atmo_mat.set_shader_parameter("glow", l["glow"])
	for s in smokestacks:
		s.scale_amount_min = 0.012 + 0.03 * l["smoke"]
		s.scale_amount_max = s.scale_amount_min
		s.color_ramp.set_color(0, Color(0.6, 0.57, 0.55, 0.4 + 0.5 * l["smoke"]).darkened(0.4 * l["smoke"]))
	_update_trees()


# ---------- helpers ----------
func _basis_up(dir: Vector3) -> Basis:
	## Basis whose Y axis points along dir (things "stand" on the surface).
	var up := dir.normalized()
	var ref := Vector3.FORWARD if absf(up.dot(Vector3.UP)) > 0.95 else Vector3.UP
	var x := ref.cross(up).normalized()
	var z := x.cross(up).normalized()
	return Basis(x, up, z)


func _basis_out(dir: Vector3) -> Basis:
	## Basis whose Z axis points along dir (a quad lying flat on the surface, facing outward).
	var b := _basis_up(dir)
	return Basis(b.x, b.z, b.y)


static func _icosphere(level: int) -> Array:
	var t := (1.0 + sqrt(5.0)) / 2.0
	var verts: Array[Vector3] = []
	for v in [Vector3(-1, t, 0), Vector3(1, t, 0), Vector3(-1, -t, 0), Vector3(1, -t, 0),
			Vector3(0, -1, t), Vector3(0, 1, t), Vector3(0, -1, -t), Vector3(0, 1, -t),
			Vector3(t, 0, -1), Vector3(t, 0, 1), Vector3(-t, 0, -1), Vector3(-t, 0, 1)]:
		verts.append((v as Vector3).normalized())
	var faces: Array = [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2],
		[10, 7, 6], [7, 1, 8], [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]]
	for l in level:
		var cache := {}
		var nf: Array = []
		for f in faces:
			var a := _mid(f[0], f[1], verts, cache)
			var b := _mid(f[1], f[2], verts, cache)
			var c := _mid(f[2], f[0], verts, cache)
			nf.append([f[0], a, c])
			nf.append([f[1], b, a])
			nf.append([f[2], c, b])
			nf.append([a, b, c])
		faces = nf
	var pv := PackedVector3Array(verts)
	var idx := PackedInt32Array()
	for f in faces:
		# Godot treats clockwise triangles as front-facing
		idx.append(f[0])
		idx.append(f[2])
		idx.append(f[1])
	return [pv, idx]


static func _mid(i: int, j: int, verts: Array[Vector3], cache: Dictionary) -> int:
	var key := mini(i, j) * 1000000 + maxi(i, j)
	if cache.has(key):
		return cache[key]
	verts.append(((verts[i] + verts[j]) * 0.5).normalized())
	cache[key] = verts.size() - 1
	return verts.size() - 1
