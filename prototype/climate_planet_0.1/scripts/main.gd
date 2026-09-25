extends Node3D
## Look-dev scene: the planet, a starry sky, an orbit camera and a small control panel.
## Drag with the mouse to turn the view, scroll to zoom. The slider plays the years 2025 → 2140.
##
## Command line (handy for screenshots):  godot --path . -- --t=0.5 --shot=out.png

var planet: Planet
var cam: Camera3D
var yaw := -0.5
var pitch := 0.25
var dist := 4.3
var dragging := false
var t := 0.0
var playing := false

var year_label: Label
var temp_label: Label
var info_label: Label
var slider: HSlider
var play_button: Button

var _shot_path := ""
var _shot_frames := 0


func _ready() -> void:
	_setup_world()
	planet = Planet.new()
	planet.name = "Planet"
	add_child(planet)
	_build_ui()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--t="):
			t = float(arg.substr(4))
		elif arg.begins_with("--shot="):
			_shot_path = arg.substr(7)
		elif arg.begins_with("--yaw="):
			yaw = float(arg.substr(6))
		elif arg.begins_with("--dist="):
			dist = float(arg.substr(7))
		elif arg.begins_with("--pitch="):
			pitch = float(arg.substr(8))
		elif arg == "--fire":
			planet.trigger("fire")
		elif arg == "--storm":
			planet.trigger("storm")
	slider.value = t
	_set_t(t)
	_update_camera()


func _setup_world() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color("#13132e")
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color("#6a6aa8")
	env.ambient_light_energy = 0.55
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	var sun := DirectionalLight3D.new()
	sun.name = "Sun"
	sun.light_energy = 1.25
	sun.light_color = Color("#fff4e0")
	sun.rotation = Vector3(deg_to_rad(-35.0), deg_to_rad(-40.0), 0.0)
	add_child(sun)
	cam = Camera3D.new()
	cam.fov = 40.0
	add_child(cam)
	_build_stars()


func _build_stars() -> void:
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
		var s := rng.randf_range(0.5, 1.6)
		mm.set_instance_transform(i, Transform3D(Basis().scaled(Vector3.ONE * s), d * 60.0))
		var b := rng.randf_range(0.35, 1.0)
		mm.set_instance_color(i, Color(b, b, b * 1.05))
	var mmi := MultiMeshInstance3D.new()
	mmi.name = "Stars"
	mmi.multimesh = mm
	add_child(mmi)


func _build_ui() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(root)

	var top := VBoxContainer.new()
	top.position = Vector2(36, 28)
	top.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(top)
	year_label = Label.new()
	year_label.add_theme_font_size_override("font_size", 72)
	top.add_child(year_label)
	temp_label = Label.new()
	temp_label.add_theme_font_size_override("font_size", 26)
	top.add_child(temp_label)
	info_label = Label.new()
	info_label.add_theme_font_size_override("font_size", 16)
	info_label.add_theme_color_override("font_color", Color("#c9c7e8"))
	top.add_child(info_label)

	var panel := PanelContainer.new()
	panel.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	panel.offset_left = 36
	panel.offset_right = -36
	panel.offset_top = -84
	panel.offset_bottom = -24
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.1, 0.1, 0.24, 0.85)
	sb.set_corner_radius_all(10)
	sb.content_margin_left = 16
	sb.content_margin_right = 16
	sb.content_margin_top = 10
	sb.content_margin_bottom = 10
	panel.add_theme_stylebox_override("panel", sb)
	root.add_child(panel)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 14)
	panel.add_child(row)
	play_button = _button(row, "Play", func() -> void:
		playing = not playing
		if playing and t >= 1.0:
			t = 0.0
		play_button.text = "Pause" if playing else "Play")
	var lab := Label.new()
	lab.text = "Warming"
	row.add_child(lab)
	slider = HSlider.new()
	slider.min_value = 0.0
	slider.max_value = 1.0
	slider.step = 0.001
	slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	slider.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	slider.value_changed.connect(func(v: float) -> void: _set_t(v))
	row.add_child(slider)
	_button(row, "Hurricane", func() -> void: planet.trigger("storm"))
	_button(row, "Wildfire", func() -> void: planet.trigger("fire"))
	_button(row, "Spin on/off", func() -> void: planet.spin_speed = 0.0 if planet.spin_speed > 0.0 else 0.05)


func _button(parent: Control, text: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = Vector2(0, 44)
	b.pressed.connect(cb)
	parent.add_child(b)
	return b


func _set_t(v: float) -> void:
	t = v
	var l := ClimateLook.at(t)
	planet.apply_look(l)
	year_label.text = str(int(round(l["year"] / 5.0) * 5.0))
	temp_label.text = "+%.1f °C" % l["temp"]
	temp_label.add_theme_color_override("font_color", Color("#9fe0ff").lerp(Color("#ff9a6b"), t))
	info_label.text = "Sea level +%.1f m (drawn ~50× larger)   Forests %d%%" % [l["sea_rise"] / 0.012 * 1.9, int(l["forest"] * 100.0)]


func _process(delta: float) -> void:
	if playing:
		var nt := minf(1.0, t + delta / 30.0)       # 30 s from 2025 to 2140
		slider.set_value_no_signal(nt)
		_set_t(nt)
		if nt >= 1.0:
			playing = false
			play_button.text = "Play"
	if _shot_path != "":
		_shot_frames += 1
		if _shot_frames == 90:
			get_viewport().get_texture().get_image().save_png(_shot_path)
			get_tree().quit()


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
	var off := Vector3(cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw)) * dist
	cam.position = off
	cam.look_at(Vector3.ZERO, Vector3.UP)
