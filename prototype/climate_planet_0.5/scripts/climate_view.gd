class_name ClimateView
extends Node3D
## Turns ClimateModel fields into visuals:
##  - a small equirectangular data texture the planet shaders read (ice, rain, temperature, clouds),
##  - a colour overlay (temperature, warming, rain, rain change) on the model grid,
##  - animated wind streaks that follow the model's winds.
## Attach it under the planet's rotating body so everything turns with the planet.

const TEX_W := 256
const TEX_H := 128
const OVERLAY_R := 1.092

enum Overlay { OFF, TEMPERATURE, WARMING, RAIN, RAIN_CHANGE, CLOUDS, CLOUD_CHANGE }

var model: ClimateModel
var texture: ImageTexture
var wind_texture: ImageTexture
var _wind_img: Image
var overlay_mode := Overlay.OFF
var show_wind := true

var _img: Image
var _pix_cells := PackedInt32Array()      # 3 cells per pixel
var _pix_w := PackedFloat32Array()        # 3 weights per pixel
var _overlay: MeshInstance3D
var _overlay_mesh: ArrayMesh
var _overlay_arrays := []
var _wind_mm: MultiMesh
var _wind_mmi: MultiMeshInstance3D
var _p_dir := PackedVector3Array()
var _p_age := PackedFloat32Array()
var _p_cell := PackedInt32Array()
var _rng := RandomNumberGenerator.new()
# snapshot of the fields (the model runs in a worker thread)
var T := PackedFloat32Array()
var P := PackedFloat32Array()
var U := PackedFloat32Array()
var V := PackedFloat32Array()
var ICE := PackedFloat32Array()
var W := PackedFloat32Array()
var CL := PackedFloat32Array()
var CU := PackedFloat32Array()
var CV := PackedFloat32Array()
var show_currents := true

const N_PARTICLES := 1500
const PARTICLE_LIFE := 4.0
const WIND_SPEED := 0.008       # radians per second per m/s of wind (visual speed-up; matches the cloud flow)


## Heavy, node-free preparation: safe to call from a worker thread.
func prepare(m: ClimateModel) -> void:
	model = m
	_build_lookup()


## Creates the nodes; call on the main thread after prepare().
func build_nodes() -> void:
	_rng.seed = 5
	take_snapshot()
	_img = Image.create(TEX_W, TEX_H, false, Image.FORMAT_RGBA8)
	texture = ImageTexture.create_from_image(_img)
	_wind_img = Image.create(TEX_W, TEX_H, false, Image.FORMAT_RGBA8)
	wind_texture = ImageTexture.create_from_image(_wind_img)
	_build_overlay()
	_build_wind()
	_build_currents()
	apply_bytes(compute_bytes(model.T, model.P, model.ice, model.cloud, model.u, model.v))


## Copy the model fields (call when the worker thread is idle).
func take_snapshot() -> void:
	T = model.T.duplicate(); P = model.P.duplicate(); U = model.u.duplicate(); V = model.v.duplicate()
	ICE = model.ice.duplicate(); W = model.w_up.duplicate(); CL = model.cloud.duplicate()
	CU = model.cu.duplicate(); CV = model.cv.duplicate()


func _build_lookup() -> void:
	_pix_cells.resize(TEX_W * TEX_H * 3)
	_pix_w.resize(TEX_W * TEX_H * 3)
	var hint := 0
	for py in TEX_H:
		var la := PI * 0.5 - (py + 0.5) / TEX_H * PI
		for px in TEX_W:
			var lo := (px + 0.5) / TEX_W * TAU - PI
			var d := Vector3(cos(la) * sin(lo), sin(la), cos(la) * cos(lo))
			var c := model.nearest_cell(d, hint)
			hint = c
			# the two best neighbours complete a small triangle for smooth interpolation
			var best := [-1, -1]
			var bd := [-2.0, -2.0]
			for k in range(model.nb_start[c], model.nb_start[c + 1]):
				var j := model.nb[k]
				var dd := model.pos[j].dot(d)
				if dd > bd[0]:
					bd[1] = bd[0]; best[1] = best[0]; bd[0] = dd; best[0] = j
				elif dd > bd[1]:
					bd[1] = dd; best[1] = j
			var cells := [c, best[0], best[1]]
			var ws := []
			var tot := 0.0
			for cc in cells:
				var ang := maxf(1e-4, model.pos[cc].angle_to(d))
				var w := 1.0 / (ang * ang)
				ws.append(w)
				tot += w
			var base := (py * TEX_W + px) * 3
			for e in 3:
				_pix_cells[base + e] = cells[e]
				_pix_w[base + e] = ws[e] / tot


func _field(arr: PackedFloat32Array, base: int) -> float:
	return arr[_pix_cells[base]] * _pix_w[base] + arr[_pix_cells[base + 1]] * _pix_w[base + 1] + arr[_pix_cells[base + 2]] * _pix_w[base + 2]


## Data texture bytes from model fields. Worker-safe (touches no nodes).
## Returns [climate bytes, wind bytes].
func compute_bytes(t_arr: PackedFloat32Array, p_arr: PackedFloat32Array, ice_arr: PackedFloat32Array, cloud_arr: PackedFloat32Array,
		u_arr: PackedFloat32Array, v_arr: PackedFloat32Array) -> Array:
	var data := PackedByteArray()
	data.resize(TEX_W * TEX_H * 4)
	var wind := PackedByteArray()
	wind.resize(TEX_W * TEX_H * 4)
	for p in TEX_W * TEX_H:
		var b := p * 3
		var t := _field(t_arr, b)
		var rain := _field(p_arr, b)
		var ice := _field(ice_arr, b)
		var cloud := clampf(_field(cloud_arr, b), 0.0, 1.0)          # from the model (high + low clouds)
		data[p * 4] = int(clampf((t + 40.0) / 80.0, 0.0, 1.0) * 255.0)
		data[p * 4 + 1] = int(clampf(sqrt(maxf(rain, 0.0) / 4000.0), 0.0, 1.0) * 255.0)
		data[p * 4 + 2] = int(cloud * 255.0)
		data[p * 4 + 3] = int(clampf(ice, 0.0, 1.0) * 255.0)
		wind[p * 4] = int(clampf(_field(u_arr, b) / 30.0 + 0.5, 0.0, 1.0) * 255.0)
		wind[p * 4 + 1] = int(clampf(_field(v_arr, b) / 30.0 + 0.5, 0.0, 1.0) * 255.0)
		wind[p * 4 + 2] = 128
		wind[p * 4 + 3] = 255
	return [data, wind]


## Main thread: upload the texture and recolour the overlay from the latest snapshot.
func apply_bytes(result: Array) -> void:
	_img.set_data(TEX_W, TEX_H, false, Image.FORMAT_RGBA8, result[0])
	texture.update(_img)
	_wind_img.set_data(TEX_W, TEX_H, false, Image.FORMAT_RGBA8, result[1])
	wind_texture.update(_wind_img)
	_update_overlay()


func set_overlay(mode: int) -> void:
	overlay_mode = mode
	_update_overlay()


# ---------- colour overlay ----------
func _build_overlay() -> void:
	var ico: Array = Planet._icosphere(4)          # same grid as the model: one vertex per cell
	var verts: PackedVector3Array = ico[0]
	var normals := PackedVector3Array()
	for i in verts.size():
		normals.append(verts[i])
		verts[i] = verts[i] * OVERLAY_R
	var colors := PackedColorArray()
	colors.resize(verts.size())
	_overlay_arrays.resize(Mesh.ARRAY_MAX)
	_overlay_arrays[Mesh.ARRAY_VERTEX] = verts
	_overlay_arrays[Mesh.ARRAY_NORMAL] = normals
	_overlay_arrays[Mesh.ARRAY_COLOR] = colors
	_overlay_arrays[Mesh.ARRAY_INDEX] = ico[1]
	_overlay_mesh = ArrayMesh.new()
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.render_priority = 4
	_overlay = MeshInstance3D.new()
	_overlay.name = "DataOverlay"
	_overlay.mesh = _overlay_mesh
	_overlay.material_override = mat
	add_child(_overlay)


func _update_overlay() -> void:
	_overlay.visible = overlay_mode != Overlay.OFF
	if not _overlay.visible:
		return
	var colors: PackedColorArray = _overlay_arrays[Mesh.ARRAY_COLOR]
	for i in model.n:
		colors[i] = color_for(i)
	_overlay_arrays[Mesh.ARRAY_COLOR] = colors
	_overlay_mesh.clear_surfaces()
	_overlay_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, _overlay_arrays)


func color_for(i: int) -> Color:
	var c: Color
	match overlay_mode:
		Overlay.TEMPERATURE:
			c = ramp(inverse_lerp(-30.0, 32.0, T[i]), TEMP_RAMP)
		Overlay.WARMING:
			c = ramp(inverse_lerp(-1.0, 6.0, T[i] - model.baseline_T[i]), WARM_RAMP)
		Overlay.RAIN:
			c = ramp(sqrt(clampf(P[i] / 3000.0, 0.0, 1.0)), RAIN_RAMP)
		Overlay.RAIN_CHANGE:
			var base := maxf(model.baseline_P[i], 50.0)
			c = ramp(inverse_lerp(-0.6, 0.6, (P[i] - base) / base), CHANGE_RAMP)
		Overlay.CLOUDS:
			c = ramp(CL[i], CLOUD_RAMP)
		Overlay.CLOUD_CHANGE:
			c = ramp(inverse_lerp(-0.25, 0.25, CL[i] - model.cloud_base[i]), CHANGE_RAMP)
		_:
			c = Color.TRANSPARENT
	c.a = 0.78
	return c


## Legend for the UI: [label, low text, high text, ramp]
func legend() -> Array:
	match overlay_mode:
		Overlay.TEMPERATURE: return ["Temperature", "−30 °C", "+32 °C", TEMP_RAMP]
		Overlay.WARMING: return ["Warming since 1850", "−1 °C", "+6 °C", WARM_RAMP]
		Overlay.RAIN: return ["Rainfall", "0", "3000 mm/yr", RAIN_RAMP]
		Overlay.RAIN_CHANGE: return ["Rain change since 1850", "−60 %", "+60 %", CHANGE_RAMP]
		Overlay.CLOUDS: return ["Cloud cover", "clear", "overcast", CLOUD_RAMP]
		Overlay.CLOUD_CHANGE: return ["Cloud change since 1850", "−25 pts", "+25 pts", CHANGE_RAMP]
	return []


const TEMP_RAMP := [Color("#3b2c86"), Color("#2c7bb6"), Color("#abd9e9"), Color("#ffffbf"), Color("#fdae61"), Color("#d7191c"), Color("#7a0010")]
const WARM_RAMP := [Color("#2166ac"), Color("#f7f7f7"), Color("#fddbc7"), Color("#f4a582"), Color("#d6604d"), Color("#b2182b"), Color("#67001f")]
const RAIN_RAMP := [Color("#a0522d"), Color("#e6c27a"), Color("#f7f4c8"), Color("#9bd48c"), Color("#2e9c6a"), Color("#1b6fa8"), Color("#23307a")]
const CLOUD_RAMP := [Color("#0b2447"), Color("#19376d"), Color("#576cbc"), Color("#a5b4e6"), Color("#f2f4fb")]
const CHANGE_RAMP := [Color("#8c510a"), Color("#d8b365"), Color("#f6e8c3"), Color("#f5f5f5"), Color("#c7eae5"), Color("#5ab4ac"), Color("#01665e")]


static func ramp(t: float, stops: Array) -> Color:
	t = clampf(t, 0.0, 1.0) * (stops.size() - 1)
	var i := mini(int(t), stops.size() - 2)
	return (stops[i] as Color).lerp(stops[i + 1], t - i)


# ---------- wind streaks ----------
func _build_wind() -> void:
	var quad := QuadMesh.new()
	quad.size = Vector2(1.0, 1.0)
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mat.render_priority = 5
	quad.material = mat
	_wind_mm = MultiMesh.new()
	_wind_mm.transform_format = MultiMesh.TRANSFORM_3D
	_wind_mm.use_colors = true
	_wind_mm.mesh = quad
	_wind_mm.instance_count = N_PARTICLES
	_wind_mmi = MultiMeshInstance3D.new()
	_wind_mmi.name = "WindStreaks"
	_wind_mmi.multimesh = _wind_mm
	add_child(_wind_mmi)
	_p_dir.resize(N_PARTICLES); _p_age.resize(N_PARTICLES); _p_cell.resize(N_PARTICLES)
	for i in N_PARTICLES:
		_respawn(i)
		_p_age[i] = _rng.randf() * PARTICLE_LIFE


func _respawn(i: int) -> void:
	var d := Vector3(_rng.randfn(), _rng.randfn(), _rng.randfn()).normalized()
	_p_dir[i] = d
	_p_age[i] = 0.0
	_p_cell[i] = model.nearest_cell(d, _p_cell[i] if _p_cell[i] < model.n else 0)


func _process(delta: float) -> void:
	_process_currents(delta)
	_wind_mmi.visible = show_wind
	if not show_wind or U.is_empty():
		return
	for i in N_PARTICLES:
		_p_age[i] += delta
		if _p_age[i] > PARTICLE_LIFE:
			_respawn(i)
		var d := _p_dir[i]
		var c := model.nearest_cell(d, _p_cell[i])
		_p_cell[i] = c
		var vel := model.east[c] * U[c] + model.north[c] * V[c]    # m/s, tangent
		var speed := vel.length()
		d = (d + vel * WIND_SPEED * delta).normalized()
		_p_dir[i] = d
		var along := vel / maxf(speed, 0.01)
		var up := d
		var side := up.cross(along).normalized()
		along = side.cross(up)
		var length := 0.012 + speed * 0.0035
		var basis := Basis(along * length, side * 0.0035, up)
		_wind_mm.set_instance_transform(i, Transform3D(basis, d * (OVERLAY_R + 0.004)))
		var fade := sin(PI * _p_age[i] / PARTICLE_LIFE)
		_wind_mm.set_instance_color(i, Color(1.0, 1.0, 1.0, 0.85 * fade * clampf(speed / 4.0, 0.25, 1.0)))


## Wind at a unit direction, as a tangent velocity in m/s (used to steer hurricanes).
func wind_at(dir: Vector3) -> Vector3:
	if U.is_empty():
		return Vector3.ZERO
	var c := model.nearest_cell(dir, _wind_hint)
	_wind_hint = c
	return model.east[c] * U[c] + model.north[c] * V[c]


## Sea surface temperature at a direction (NAN over land): hurricanes need warm water.
func sst_at(dir: Vector3) -> float:
	var c := model.nearest_cell(dir, _wind_hint)
	_wind_hint = c
	return T[c] if model.land[c] == 0 else NAN

var _wind_hint := 0


# ---------- ocean current streaks (coloured by water temperature) ----------
const N_CURRENT := 1100
const CURRENT_LIFE := 6.0
const CURRENT_VIS := 0.06        # radians per second per m/s (currents are slow; this exaggerates them)
const CURRENT_R := 1.019
var _c_mm: MultiMesh
var _c_mmi: MultiMeshInstance3D
var _c_dir := PackedVector3Array()
var _c_age := PackedFloat32Array()
var _c_cell := PackedInt32Array()


func _build_currents() -> void:
	var quad := QuadMesh.new()
	quad.size = Vector2(1.0, 1.0)
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mat.render_priority = 3
	quad.material = mat
	_c_mm = MultiMesh.new()
	_c_mm.transform_format = MultiMesh.TRANSFORM_3D
	_c_mm.use_colors = true
	_c_mm.mesh = quad
	_c_mm.instance_count = N_CURRENT
	_c_mmi = MultiMeshInstance3D.new()
	_c_mmi.name = "CurrentStreaks"
	_c_mmi.multimesh = _c_mm
	add_child(_c_mmi)
	_c_dir.resize(N_CURRENT); _c_age.resize(N_CURRENT); _c_cell.resize(N_CURRENT)
	for i in N_CURRENT:
		_respawn_current(i)
		_c_age[i] = _rng.randf() * CURRENT_LIFE


func _respawn_current(i: int) -> void:
	for attempt in 30:
		var d := Vector3(_rng.randfn(), _rng.randfn(), _rng.randfn()).normalized()
		var c := model.nearest_cell(d, _c_cell[i] if _c_cell[i] < model.n else 0)
		if model.land[c] == 0:
			_c_dir[i] = d
			_c_cell[i] = c
			break
	_c_age[i] = 0.0


func _process_currents(delta: float) -> void:
	if _c_mmi == null:
		return
	_c_mmi.visible = show_currents
	if not show_currents or CU.is_empty():
		return
	for i in N_CURRENT:
		_c_age[i] += delta
		var d := _c_dir[i]
		var c := model.nearest_cell(d, _c_cell[i])
		_c_cell[i] = c
		if _c_age[i] > CURRENT_LIFE or model.land[c] == 1:
			_respawn_current(i)
			d = _c_dir[i]
			c = _c_cell[i]
		var vel := model.east[c] * CU[c] + model.north[c] * CV[c]
		var speed := vel.length()
		d = (d + vel * CURRENT_VIS * delta).normalized()
		_c_dir[i] = d
		var along := vel / maxf(speed, 1e-4)
		var side := d.cross(along).normalized()
		along = side.cross(d)
		var length := 0.014 + speed * 0.05
		_c_mm.set_instance_transform(i, Transform3D(Basis(along * length, side * 0.0075, d), d * CURRENT_R))
		var col := ramp(inverse_lerp(-2.0, 30.0, T[c]), TEMP_RAMP)
		col.a = 0.95 * sin(PI * _c_age[i] / CURRENT_LIFE) * clampf(speed / 0.12, 0.35, 1.0)
		_c_mm.set_instance_color(i, col)
