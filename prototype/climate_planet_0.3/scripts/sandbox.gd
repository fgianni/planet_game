extends Node3D
## Climate sandbox: the planet driven by ClimateModel. Change CO₂, watch temperature, ice, rain,
## deserts, clouds and winds respond. Overlays show the model's fields.
##
## Command line:  godot --path . res://sandbox.tscn -- --years=175 --hist --overlay=warming --shot=out.png

const HIST_END := 2025.0
const CACHE := "user://climate_baseline_seed%d.bin"

var planet: Planet
var model := ClimateModel.new()
var view := ClimateView.new()
var cam: Camera3D
var yaw := -0.5
var pitch := 0.25
var dist := 4.3
var dragging := false

var ready_ := false
var playing := false
var years_per_second := 1.0
var historical := true
var target_co2 := 280.0
var deep_base := 0.0
var _task := -1
var _pending_bytes: Array = []
var _budget := 0.0
var _load_task := -1

# UI
var ui_title: Label
var ui_stats: Label
var ui_status: Label
var co2_slider: HSlider
var co2_label: Label
var play_btn: Button
var hist_btn: Button
var legend_box: VBoxContainer
var legend_rect: TextureRect
var legend_lo: Label
var legend_hi: Label
var legend_title: Label

# command line
var _shot := ""
var _shot_years := 0.0
var _shot_overlay := -1
var _frames := 0
var _shot_frame := 90


func _ready() -> void:
	_setup_world()
	planet = Planet.new()
	planet.name = "Planet"
	add_child(planet)
	_build_ui()
	_update_camera()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shot="): _shot = arg.substr(7)
		elif arg.begins_with("--years="): _shot_years = float(arg.substr(8))
		elif arg.begins_with("--co2="): target_co2 = float(arg.substr(6)); historical = false
		elif arg == "--hist": historical = true
		elif arg.begins_with("--yaw="): yaw = float(arg.substr(6))
		elif arg.begins_with("--pitch="): pitch = float(arg.substr(8))
		elif arg.begins_with("--dist="): dist = float(arg.substr(7))
		elif arg.begins_with("--overlay="):
			_shot_overlay = ["off", "temperature", "warming", "rain", "rainchange"].find(arg.substr(10))
		elif arg == "--nowind": view.show_wind = false
		elif arg.begins_with("--frame="): _shot_frame = int(arg.substr(8))
		elif arg == "--nospin": planet.spin_speed = 0.0
	_update_camera()
	ui_status.text = "Preparing the climate… (first launch takes about 15 seconds)"
	var seed_: int = planet.seed
	_load_task = WorkerThreadPool.add_task(func() -> void:
		model.setup(4, planet.radius_at)
		if not model.load_state(CACHE % seed_):
			model.spin_up(200)
			model.store_baseline()
			model.save_state(CACHE % seed_)
		view.prepare(model))


func _finish_loading() -> void:
	deep_base = model.T_deep
	planet.body.add_child(view)
	view.build_nodes()
	planet.set_climate_texture(view.texture, view.wind_texture)
	planet.wind_fn = view.wind_at
	planet.sst_fn = view.sst_at
	planet.tree_cells = PackedInt32Array()
	var hint := 0
	for d in planet.tree_directions():
		hint = model.nearest_cell(d, hint)
		planet.tree_cells.append(hint)
	ready_ = true
	ui_status.text = ""
	if _shot_overlay >= 0:
		view.set_overlay(_shot_overlay)
	_apply_to_planet()
	_update_ui()
	if _shot != "":
		# headless screenshot run: simulate synchronously, then capture
		var steps := int(_shot_years * 4.0)
		for s in steps:
			_set_co2_for_year()
			model.step(0.25)
		view.take_snapshot()
		view.apply_bytes(view.compute_bytes(model.T, model.P, model.ice, model.w_up, model.u, model.v))
		_apply_to_planet()
		_update_ui()


func _process(delta: float) -> void:
	if not ready_:
		if _load_task >= 0 and WorkerThreadPool.is_task_completed(_load_task):
			WorkerThreadPool.wait_for_task_completion(_load_task)
			_load_task = -1
			_finish_loading()
		return
	if _shot != "":
		_frames += 1
		if _frames == _shot_frame:
			get_viewport().get_texture().get_image().save_png(_shot)
			get_tree().quit()
		return
	if _task >= 0 and WorkerThreadPool.is_task_completed(_task):
		WorkerThreadPool.wait_for_task_completion(_task)
		_task = -1
		view.take_snapshot()
		view.apply_bytes(_pending_bytes)
		_apply_to_planet()
		_update_ui()
	if playing:
		_budget = minf(_budget + delta * years_per_second, 1.0)
		if _task < 0 and _budget >= 0.25:
			_budget -= 0.25
			_set_co2_for_year()
			_task = WorkerThreadPool.add_task(func() -> void:
				model.step(0.25)
				_pending_bytes = view.compute_bytes(model.T, model.P, model.ice, model.w_up, model.u, model.v))


func _set_co2_for_year() -> void:
	if historical and model.year < HIST_END:
		var x := clampf((model.year - 1850.0) / (HIST_END - 1850.0), 0.0, 1.0)
		model.co2 = 280.0 + 145.0 * pow(x, 3.2)          # rough fit of the observed curve (1850–2025)
		target_co2 = model.co2
		co2_slider.set_value_no_signal(target_co2)
	else:
		# CO₂ moves towards the slider gradually (at most 4 ppm a year)
		model.co2 = move_toward(model.co2, target_co2, 1.0)


func _apply_to_planet() -> void:
	var anomaly := model.global_T - model.ref_global_T
	var look := ClimateLook.at(clampf(anomaly / 3.4, 0.0, 1.0))
	look["sea_rise"] = clampf(0.004 * (model.T_deep - deep_base), 0.0, 0.02)   # thermal expansion lags with the deep ocean
	# storms need warm tropical seas; fires need heat and drought
	var hot := 0
	var dry := 0
	for i in model.n:
		var la := absf(rad_to_deg(model.lat[i]))
		if model.land[i] == 0 and la > 6.0 and la < 26.0 and view.T[i] > 27.5:
			hot += 1
		elif model.land[i] == 1 and view.T[i] > 22.0 and view.P[i] < 450.0 and model.veg[i] > 0.1:
			dry += 1
	look["storms"] = 0 if hot < 25 else (1 if hot < 90 else 2)
	look["fires"] = mini(3, dry / 40)
	var th := PackedFloat32Array()
	for c in planet.tree_cells:
		var h := model.veg[c] * (1.0 - view.ICE[c]) * clampf((34.0 - view.T[c]) / 6.0, 0.0, 1.0)
		th.append(clampf(h * 1.15, 0.0, 1.0))
	planet.tree_health = th
	planet.apply_look(look)


# ---------- world, camera ----------
func _setup_world() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color("#13132e")
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color("#6a6aa8")
	env.ambient_light_energy = 0.55
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	var sun := DirectionalLight3D.new()
	sun.light_energy = 1.25
	sun.light_color = Color("#fff4e0")
	sun.rotation = Vector3(deg_to_rad(-35.0), deg_to_rad(-40.0), 0.0)
	add_child(sun)
	cam = Camera3D.new()
	cam.fov = 40.0
	add_child(cam)
	var rng := RandomNumberGenerator.new()
	rng.seed = 99
	var quad := QuadMesh.new()
	quad.size = Vector2(0.12, 0.12)
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.billboard_mode = BaseMaterial3D.BILLBOARD_ENABLED
	mat.vertex_color_use_as_albedo = true
	quad.material = mat
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_colors = true
	mm.mesh = quad
	mm.instance_count = 700
	for i in mm.instance_count:
		var d := Vector3(rng.randfn(), rng.randfn(), rng.randfn()).normalized()
		mm.set_instance_transform(i, Transform3D(Basis().scaled(Vector3.ONE * rng.randf_range(0.5, 1.6)), d * 60.0))
		var b := rng.randf_range(0.35, 1.0)
		mm.set_instance_color(i, Color(b, b, b * 1.05))
	var mmi := MultiMeshInstance3D.new()
	mmi.multimesh = mm
	add_child(mmi)


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_LEFT:
			dragging = mb.pressed
		elif mb.button_index == MOUSE_BUTTON_WHEEL_UP and mb.pressed:
			dist = maxf(1.6, dist * 0.92)
		elif mb.button_index == MOUSE_BUTTON_WHEEL_DOWN and mb.pressed:
			dist = minf(7.0, dist / 0.92)
		_update_camera()
	elif event is InputEventMouseMotion and dragging:
		var mm := event as InputEventMouseMotion
		yaw -= mm.relative.x * 0.006
		pitch = clampf(pitch + mm.relative.y * 0.006, -1.3, 1.3)
		_update_camera()


func _update_camera() -> void:
	if cam == null:
		return
	cam.position = Vector3(cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw)) * dist
	cam.look_at(Vector3.ZERO, Vector3.UP)


# ---------- UI ----------
func _build_ui() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(root)

	var top := VBoxContainer.new()
	top.position = Vector2(32, 24)
	top.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(top)
	ui_title = Label.new()
	ui_title.add_theme_font_size_override("font_size", 60)
	top.add_child(ui_title)
	ui_stats = Label.new()
	ui_stats.add_theme_font_size_override("font_size", 16)
	ui_stats.add_theme_color_override("font_color", Color("#dcdaf2"))
	top.add_child(ui_stats)
	ui_status = Label.new()
	ui_status.add_theme_font_size_override("font_size", 18)
	ui_status.add_theme_color_override("font_color", Color("#ffd27d"))
	top.add_child(ui_status)

	# legend (bottom right, above the panel)
	legend_box = VBoxContainer.new()
	legend_box.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	legend_box.offset_left = -300
	legend_box.offset_right = -36
	legend_box.offset_top = -190
	legend_box.offset_bottom = -110
	legend_box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(legend_box)
	legend_title = Label.new()
	legend_box.add_child(legend_title)
	legend_rect = TextureRect.new()
	legend_rect.custom_minimum_size = Vector2(264, 14)
	legend_rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	legend_rect.stretch_mode = TextureRect.STRETCH_SCALE
	legend_box.add_child(legend_rect)
	var lh := HBoxContainer.new()
	legend_lo = Label.new()
	legend_hi = Label.new()
	legend_lo.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	legend_hi.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	lh.add_child(legend_lo)
	lh.add_child(legend_hi)
	legend_box.add_child(lh)

	var panel := PanelContainer.new()
	panel.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	panel.offset_left = 32
	panel.offset_right = -32
	panel.offset_top = -100
	panel.offset_bottom = -20
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.1, 0.1, 0.24, 0.88)
	sb.set_corner_radius_all(10)
	sb.content_margin_left = 14; sb.content_margin_right = 14; sb.content_margin_top = 8; sb.content_margin_bottom = 8
	panel.add_theme_stylebox_override("panel", sb)
	root.add_child(panel)
	var col := VBoxContainer.new()
	panel.add_child(col)
	var row1 := HBoxContainer.new()
	row1.add_theme_constant_override("separation", 10)
	col.add_child(row1)
	play_btn = _btn(row1, "Play", func() -> void:
		playing = not playing
		play_btn.text = "Pause" if playing else "Play")
	_btn(row1, "Speed ×", func() -> void:
		years_per_second = {0.5: 1.0, 1.0: 2.0, 2.0: 4.0, 4.0: 0.5}[years_per_second]
		_update_ui())
	hist_btn = _btn(row1, "Historical CO₂ to 2025: on", func() -> void:
		historical = not historical
		_update_ui())
	var l := Label.new()
	l.text = "CO₂"
	row1.add_child(l)
	co2_slider = HSlider.new()
	co2_slider.min_value = 180.0
	co2_slider.max_value = 1200.0
	co2_slider.step = 5.0
	co2_slider.value = 280.0
	co2_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	co2_slider.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	co2_slider.value_changed.connect(func(v: float) -> void:
		target_co2 = v
		if model.year < HIST_END:
			historical = false
		_update_ui())
	row1.add_child(co2_slider)
	co2_label = Label.new()
	co2_label.custom_minimum_size = Vector2(150, 0)
	row1.add_child(co2_label)
	var row2 := HBoxContainer.new()
	row2.add_theme_constant_override("separation", 8)
	col.add_child(row2)
	var ol := Label.new()
	ol.text = "Map:"
	row2.add_child(ol)
	var names := ["Planet", "Temperature", "Warming", "Rainfall", "Rain change"]
	for i in names.size():
		var mode := i
		_btn(row2, names[i], func() -> void:
			if ready_:
				view.set_overlay(mode)
				_update_ui())
	_btn(row2, "Winds on/off", func() -> void: view.show_wind = not view.show_wind)
	_btn(row2, "Spin on/off", func() -> void: planet.spin_speed = 0.0 if planet.spin_speed > 0.0 else 0.05)


func _btn(parent: Control, text: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = Vector2(0, 36)
	b.pressed.connect(cb)
	parent.add_child(b)
	return b


func _update_ui() -> void:
	co2_label.text = "target %d ppm" % int(target_co2)
	hist_btn.text = "Historical CO₂ to 2025: %s" % ("on" if historical else "off")
	play_btn.text = "Pause" if playing else "Play"
	if not ready_:
		return
	ui_title.text = str(int(model.year))
	var base := model.ref_global_T
	var pole := 0.0; var trop := 0.0; var np := 0; var nt := 0
	var ice := 0.0; var desert := 0; var landn := 0
	for i in model.n:
		var la := absf(rad_to_deg(model.lat[i]))
		var d := view.T[i] - model.baseline_T[i]
		if la > 60.0: pole += d; np += 1
		elif la < 30.0: trop += d; nt += 1
		ice += view.ICE[i]
		if model.land[i] == 1 and view.ICE[i] < 0.5:
			landn += 1
			if view.P[i] < 250.0: desert += 1
	ui_stats.text = "CO₂ %d ppm   ·   global %.1f °C (%+.2f since 1850)\npoles %+.1f °C   tropics %+.1f °C   ·   ice %.0f %% of the planet   ·   deserts %.0f %% of land\nrain %.0f mm/yr   ·   subtropical dry belt edge %.1f°   ·   %.1f years/second" % [
		int(model.co2), model.global_T, model.global_T - base, pole / maxf(1, np), trop / maxf(1, nt),
		100.0 * ice / model.n, 100.0 * desert / maxf(1, landn), model.global_E, model.hadley_edge, years_per_second]
	var lg := view.legend()
	legend_box.visible = not lg.is_empty()
	if not lg.is_empty():
		legend_title.text = lg[0]
		legend_lo.text = lg[1]
		legend_hi.text = lg[2]
		var g := Gradient.new()
		var stops: Array = lg[3]
		g.offsets = PackedFloat32Array(range(stops.size()).map(func(k): return float(k) / (stops.size() - 1)))
		g.colors = PackedColorArray(stops)
		var gt := GradientTexture2D.new()
		gt.gradient = g
		gt.width = 256
		gt.height = 8
		legend_rect.texture = gt
