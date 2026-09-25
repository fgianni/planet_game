extends Node3D
## The game scene: you govern one region of the planet. Click a cell of your region to build.
## Ending a turn advances the climate model five years; what you built changes the climate,
## and the local climate decides what your land produces.
##
## Command line:  godot --path . res://game.tscn -- --auto=12 --shot=out.png

const CACHE := "user://climate_baseline_seed%d.bin"
const PALETTE := ["farm", "agro", "house", "coal", "gas", "wind", "solar", "factory", "forest", "reserve"]

var planet: Planet
var model := ClimateModel.new()
var shadow := ClimateModel.new()          # the same planet where your region never industrialised
var view := ClimateView.new()
var game := GameState.new()
var cam: Camera3D
var yaw := 0.0
var pitch := 0.3
var dist := 2.9
var dragging := false
var drag_moved := 0.0

var ready_ := false
var busy := false
var tool_ := ""
var hover_cell := -1
var _task := -1
var _load_task := -1
var _pending_bytes: Array = []
var marks: MultiMesh
var build_nodes := {}
var _shot := ""
var _auto := 0
var _frames := 0

var ui_year: Label
var ui_stats: Label
var ui_hint: Label
var ui_paper_head: Label
var ui_paper_deck: Label
var ui_journal: VBoxContainer
var end_btn: Button
var palette_btns := {}


func _ready() -> void:
	_setup_world()
	planet = Planet.new()
	planet.spin_speed = 0.0
	add_child(planet)
	_build_ui()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shot="): _shot = arg.substr(7)
		elif arg.begins_with("--auto="): _auto = int(arg.substr(7))
	ui_hint.text = "Preparing the planet…"
	_load_task = WorkerThreadPool.add_task(func() -> void:
		model.setup(4, planet.radius_at)
		if not model.load_state(CACHE % planet.seed):
			model.spin_up(200)
			model.store_baseline()
			model.save_state(CACHE % planet.seed)
		model.co2 = 424.0
		for s in 40:
			model.step(0.5)                      # from 1850 to roughly today
		# the shadow planet starts identical; from here it never feels your region
		model.save_state("user://shadow_start.bin")
		shadow.setup(4, planet.radius_at)
		shadow.load_state("user://shadow_start.bin")
		shadow.co2 = model.co2
		view.prepare(model))


func _finish_loading() -> void:
	planet.body.add_child(view)
	view.build_nodes()
	view.show_wind = false
	view.show_currents = false
	planet.set_climate_texture(view.texture, view.wind_texture)
	var clouds := CloudSim.new()
	add_child(clouds)
	clouds.setup(view.texture, view.wind_texture, planet.cloud_material)
	var hint := 0
	for d in planet.tree_directions():
		hint = model.nearest_cell(d, hint)
		planet.tree_cells.append(hint)
	game.setup(model, planet.seed)
	game.shadow = shadow
	_build_marks()
	_look_at_region()
	_refresh_all()
	ready_ = true


func _look_at_region() -> void:
	var c := Vector3.ZERO
	for cell in game.region:
		c += model.pos[cell]
	c = c.normalized()
	yaw = atan2(c.x, c.z)
	pitch = asin(c.y)
	_update_camera()


# ---------- region markers and buildings ----------
func _build_marks() -> void:
	var disc := SphereMesh.new()
	disc.radius = 0.028
	disc.height = 0.006
	disc.radial_segments = 10
	disc.rings = 2
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	marks = MultiMesh.new()
	marks.transform_format = MultiMesh.TRANSFORM_3D
	marks.use_colors = true
	marks.mesh = disc
	marks.instance_count = game.region.size()
	var mmi := MultiMeshInstance3D.new()
	mmi.name = "RegionMarks"
	mmi.multimesh = marks
	mmi.material_override = mat
	planet.body.add_child(mmi)
	_update_marks()


func _update_marks() -> void:
	for i in game.region.size():
		var cell: int = game.region[i]
		var dir: Vector3 = model.pos[cell]
		var r: float = maxf(planet.radius_at(dir), ClimateModel.SEA_LEVEL_R + game.sea_rise) + 0.004
		marks.set_instance_transform(i, Transform3D(planet._basis_up(dir), dir * r))
		var col := Color(1, 1, 1, 0.22)
		if model.land[cell] == 0:
			col = Color(0.4, 0.75, 1.0, 0.1)
		if cell == hover_cell:
			col = Color(1, 1, 0.6, 0.5)
		marks.set_instance_color(i, col)


func _rebuild_buildings() -> void:
	for cell in build_nodes.keys():
		if not game.buildings.has(cell):
			(build_nodes[cell] as Node).queue_free()
			build_nodes.erase(cell)
	for cell in game.buildings:
		if build_nodes.has(cell):
			continue
		var type: String = game.buildings[cell]["type"]
		var node := MeshInstance3D.new()
		var mesh: Mesh
		var col := Color.WHITE
		match type:
			"coal", "gas", "factory":
				var c := CylinderMesh.new(); c.top_radius = 0.004; c.bottom_radius = 0.006; c.height = 0.03
				mesh = c; col = Color("#8c7c78") if type != "factory" else Color("#9ad0f5")
			"wind":
				var c2 := CylinderMesh.new(); c2.top_radius = 0.001; c2.bottom_radius = 0.002; c2.height = 0.035
				mesh = c2; col = Color("#eaf6ff")
			"solar":
				var b := BoxMesh.new(); b.size = Vector3(0.022, 0.003, 0.022); mesh = b; col = Color("#2b3a6b")
			"farm", "agro":
				var b2 := BoxMesh.new(); b2.size = Vector3(0.026, 0.002, 0.026); mesh = b2
				col = Color("#e8c45c") if type == "farm" else Color("#8fbf4f")
			"forest", "reserve":
				var s := SphereMesh.new(); s.radius = 0.009; s.height = 0.018; mesh = s
				col = Color("#2f9e4f") if type == "forest" else Color("#7ad48f")
			_:
				var b3 := BoxMesh.new(); b3.size = Vector3(0.016, 0.014, 0.016); mesh = b3; col = Color("#f7a8b8")
		var mat := StandardMaterial3D.new()
		mat.albedo_color = col
		node.mesh = mesh
		node.material_override = mat
		var dir: Vector3 = model.pos[cell]
		node.transform = Transform3D(planet._basis_up(dir), dir * (planet.radius_at(dir) + 0.008))
		planet.body.add_child(node)
		build_nodes[cell] = node


# ---------- picking ----------
func _cell_under_mouse() -> int:
	var mp := get_viewport().get_mouse_position()
	var from := cam.project_ray_origin(mp)
	var dirv := cam.project_ray_normal(mp)
	var inv := planet.body.global_transform.affine_inverse()
	var o: Vector3 = inv * from
	var d: Vector3 = (inv.basis * dirv).normalized()
	var b := 2.0 * o.dot(d)
	var c := o.dot(o) - 1.0
	var disc := b * b - 4.0 * c
	if disc < 0.0:
		return -1
	var t := (-b - sqrt(disc)) * 0.5
	if t < 0.0:
		return -1
	return model.nearest_cell((o + d * t).normalized(), maxi(hover_cell, 0))


func _unhandled_input(event: InputEvent) -> void:
	if not ready_:
		return
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_LEFT:
			dragging = mb.pressed
			if mb.pressed:
				drag_moved = 0.0
			elif drag_moved < 6.0:
				_click_cell()
		elif mb.button_index == MOUSE_BUTTON_WHEEL_UP and mb.pressed:
			dist = maxf(1.35, dist * 0.92); _update_camera()
		elif mb.button_index == MOUSE_BUTTON_WHEEL_DOWN and mb.pressed:
			dist = minf(6.0, dist / 0.92); _update_camera()
	elif event is InputEventMouseMotion:
		var mm := event as InputEventMouseMotion
		if dragging:
			drag_moved += mm.relative.length()
			yaw -= mm.relative.x * 0.006
			pitch = clampf(pitch + mm.relative.y * 0.006, -1.3, 1.3)
			_update_camera()
		else:
			var c := _cell_under_mouse()
			if c != hover_cell:
				hover_cell = c
				_update_marks()
				_update_hint()


func _click_cell() -> void:
	var cell := _cell_under_mouse()
	if cell < 0 or busy:
		return
	hover_cell = cell
	if tool_ == "":
		_update_hint()
		return
	var err := ""
	if tool_ == "remove":
		err = game.demolish(cell)
	else:
		err = game.place(cell, tool_)
	if err != "":
		ui_hint.text = err + "."
	else:
		if tool_ != "remove" and game.money < GameState.BUILD[tool_]["cost"]:
			tool_ = ""
		_refresh_all()
	_update_marks()


# ---------- turns ----------
func _end_turn() -> void:
	if busy or game.over or not ready_:
		return
	busy = true
	end_btn.disabled = true
	ui_hint.text = "Five years pass…"
	var ppm := game.emissions_ppm()
	var world_ppm := game.world_co2_per_turn      # the shadow world is an average one, unmoved by your example
	_task = WorkerThreadPool.add_task(func() -> void:
		model.co2 += ppm
		shadow.co2 += world_ppm                  # the rest of the world keeps going without you
		for s in 10:
			model.step(0.5)
			shadow.step(0.5)
		_pending_bytes = view.compute_bytes(model.T, model.P, model.ice, model.cloud, model.u, model.v))


func _process(delta: float) -> void:
	if not ready_:
		if _load_task >= 0 and WorkerThreadPool.is_task_completed(_load_task):
			WorkerThreadPool.wait_for_task_completion(_load_task)
			_load_task = -1
			_finish_loading()
			if _auto > 0:
				_auto_play()
		return
	if busy and _task >= 0 and WorkerThreadPool.is_task_completed(_task):
		WorkerThreadPool.wait_for_task_completion(_task)
		_task = -1
		view.take_snapshot()
		view.apply_bytes(_pending_bytes)
		game.resolve_turn()
		busy = false
		end_btn.disabled = game.over
		_refresh_all()
	if _shot != "":
		_frames += 1
		if _frames == 150:
			get_viewport().get_texture().get_image().save_png(_shot)
			get_tree().quit()


func _auto_play() -> void:
	# headless helper: plays a few turns with a simple strategy so screenshots show a lived-in region
	for t in _auto:
		var f := game.flows()
		var wants: Array = []
		if f["cap"] - game.pop < 5.0: wants.append("house")
		if f["energy_in"] - f["energy_out"] < 3.0: wants.append("wind")
		if f["food"] < f["food_need"] + 4.0: wants.append("agro")
		if game.money > 60.0: wants.append("factory")
		for w in wants:
			for cell in game.region:
				if game.can_place(cell, w) == "":
					game.place(cell, w)
					break
		var w := game.world_co2_per_turn
		model.co2 += game.emissions_ppm()
		shadow.co2 += w
		for s in 10:
			model.step(0.5)
			shadow.step(0.5)
		game.resolve_turn()
	view.take_snapshot()
	view.apply_bytes(view.compute_bytes(model.T, model.P, model.ice, model.cloud, model.u, model.v))
	_refresh_all()


# ---------- UI ----------
func _refresh_all() -> void:
	_rebuild_buildings()
	_update_marks()
	var f := game.flows()
	ui_year.text = str(game.year)
	var warming: float = model.global_T - model.ref_global_T
	var fp := game.fingerprint()
	ui_stats.text = "population %.0fk of %.0fk homes   ·   credits %.0f   ·   mood %.0f%%   ·   climate concern %.0f%%\nfood %.0f of %.0f needed   ·   energy %.0f of %.0f needed   ·   your emissions %.1f\nyour region: %.1f °C, %.0f mm rain/yr   ·   world +%.2f °C   ·   ocean overturning %.0f%%\nyour fingerprint: %+.2f °C and %+.0f mm/yr here, %+.3f °C worldwide — versus a planet where your region never industrialised" % [
		game.pop, f["cap"], game.money, game.happy, game.aware,
		f["food"], f["food_need"], f["energy_in"], f["energy_out"], f["co2"],
		game.last.get("temp", 0.0), game.last.get("rain", _region_rain()),
		warming, 100.0 * model.amoc_strength, fp.x, fp.y, fp.z]
	var paper: Dictionary = game.papers.back()
	ui_paper_head.text = paper["head"]
	ui_paper_deck.text = paper["deck"] + ("   ·   " + ", ".join(paper["also"]) if not paper["also"].is_empty() else "")
	for c in ui_journal.get_children():
		c.queue_free()
	for entry in game.log.slice(0, 7):
		var l := Label.new()
		l.text = "%d  %s" % [entry["year"], entry["text"]]
		l.add_theme_font_size_override("font_size", 14)
		l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		if entry["kind"] == "event": l.add_theme_color_override("font_color", Color("#ff9a8a"))
		elif entry["kind"] == "build": l.add_theme_color_override("font_color", Color("#a8e6a3"))
		ui_journal.add_child(l)
	for k in palette_btns:
		var b: Button = palette_btns[k]
		b.button_pressed = tool_ == k
		if k != "remove":
			b.disabled = game.money < GameState.BUILD[k]["cost"] or game.over
	if game.over:
		ui_hint.text = "%s  Score %d." % [game.result["text"], game.result["score"]]
	else:
		_update_hint()


func _region_rain() -> float:
	var s := 0.0
	for cell in game.region:
		s += model.P[cell]
	return s / game.region.size()


func _update_hint() -> void:
	if game.over or busy:
		return
	if hover_cell < 0:
		ui_hint.text = "Drag to turn the planet. Pick a building, then click a cell of your region."
		return
	var where := "your region" if game.region.find(hover_cell) >= 0 else "outside your region"
	var kind := "sea" if model.land[hover_cell] == 0 else "land"
	var txt := "%s %s · %.0f mm rain/yr · %.1f °C · wind %.1f m/s" % [where, kind, model.P[hover_cell], model.T[hover_cell],
		Vector2(model.u[hover_cell], model.v[hover_cell]).length()]
	if game.buildings.has(hover_cell):
		txt += " · " + GameState.BUILD[game.buildings[hover_cell]["type"]]["name"]
	if tool_ != "" and tool_ != "remove":
		var err := game.can_place(hover_cell, tool_)
		txt += "  →  " + (err if err != "" else "click to build %s" % GameState.BUILD[tool_]["name"].to_lower())
	ui_hint.text = txt


func _build_ui() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(root)
	var top_panel := PanelContainer.new()
	top_panel.position = Vector2(20, 16)
	var sbt := StyleBoxFlat.new()
	sbt.bg_color = Color(0.07, 0.07, 0.18, 0.72)
	sbt.set_corner_radius_all(8)
	sbt.content_margin_left = 14; sbt.content_margin_right = 16; sbt.content_margin_top = 6; sbt.content_margin_bottom = 10
	top_panel.add_theme_stylebox_override("panel", sbt)
	root.add_child(top_panel)
	var top := VBoxContainer.new()
	top_panel.add_child(top)
	ui_year = Label.new()
	ui_year.add_theme_font_size_override("font_size", 56)
	top.add_child(ui_year)
	ui_stats = Label.new()
	ui_stats.add_theme_font_size_override("font_size", 15)
	ui_stats.add_theme_color_override("font_color", Color("#dcdaf2"))
	top.add_child(ui_stats)
	# newspaper, top right
	var paper := PanelContainer.new()
	paper.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	paper.offset_left = -430
	paper.offset_right = -24
	paper.offset_top = 20
	var sbp := StyleBoxFlat.new()
	sbp.bg_color = Color("#f2f2ee")
	sbp.set_corner_radius_all(4)
	sbp.content_margin_left = 14; sbp.content_margin_right = 14; sbp.content_margin_top = 10; sbp.content_margin_bottom = 12
	paper.add_theme_stylebox_override("panel", sbp)
	root.add_child(paper)
	var pv := VBoxContainer.new()
	paper.add_child(pv)
	var mast := Label.new()
	mast.text = "The Valley Courier"
	mast.add_theme_font_size_override("font_size", 15)
	mast.add_theme_color_override("font_color", Color("#3a3a38"))
	pv.add_child(mast)
	ui_paper_head = Label.new()
	ui_paper_head.add_theme_font_size_override("font_size", 22)
	ui_paper_head.add_theme_color_override("font_color", Color("#161a1b"))
	ui_paper_head.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	ui_paper_head.custom_minimum_size = Vector2(390, 0)
	pv.add_child(ui_paper_head)
	ui_paper_deck = Label.new()
	ui_paper_deck.add_theme_font_size_override("font_size", 14)
	ui_paper_deck.add_theme_color_override("font_color", Color("#33393a"))
	ui_paper_deck.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	ui_paper_deck.custom_minimum_size = Vector2(390, 0)
	pv.add_child(ui_paper_deck)
	var jpanel := PanelContainer.new()
	jpanel.set_anchors_preset(Control.PRESET_CENTER_RIGHT)
	jpanel.offset_left = -430
	jpanel.offset_right = -24
	jpanel.offset_top = -90
	jpanel.offset_bottom = 90
	var sbj := StyleBoxFlat.new()
	sbj.bg_color = Color(0.07, 0.07, 0.18, 0.66)
	sbj.set_corner_radius_all(8)
	sbj.content_margin_left = 12; sbj.content_margin_right = 12; sbj.content_margin_top = 8; sbj.content_margin_bottom = 8
	jpanel.add_theme_stylebox_override("panel", sbj)
	root.add_child(jpanel)
	ui_journal = VBoxContainer.new()
	jpanel.add_child(ui_journal)
	# bottom bar: palette + end turn
	var panel := PanelContainer.new()
	panel.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	panel.offset_left = 24; panel.offset_right = -24; panel.offset_top = -132; panel.offset_bottom = -18
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.1, 0.1, 0.24, 0.9)
	sb.set_corner_radius_all(10)
	sb.content_margin_left = 14; sb.content_margin_right = 14; sb.content_margin_top = 10; sb.content_margin_bottom = 10
	panel.add_theme_stylebox_override("panel", sb)
	root.add_child(panel)
	var col := VBoxContainer.new()
	panel.add_child(col)
	ui_hint = Label.new()
	ui_hint.add_theme_font_size_override("font_size", 15)
	ui_hint.add_theme_color_override("font_color", Color("#ffd27d"))
	col.add_child(ui_hint)
	var row := HFlowContainer.new()
	row.add_theme_constant_override("h_separation", 6)
	row.add_theme_constant_override("v_separation", 6)
	col.add_child(row)
	for k in PALETTE:
		var d: Dictionary = GameState.BUILD[k]
		var b := Button.new()
		b.text = "%s %s  %d" % [d["icon"], d["name"], d["cost"]]
		b.toggle_mode = true
		b.tooltip_text = "%s\nemissions %.1f · %s%s%s" % [d["desc"], d.get("co2", 0.0),
			("warms its own ground %+.0f W/m²" % d["heat"]) if d.get("heat", 0.0) > 0.0 else "no local heat",
			("" if d.get("veg", 1.0) >= 0.95 else " · dries the land here (%d%% of natural cover)" % int(d["veg"] * 100.0)),
			(" · greens the land (%d%%)" % int(d["veg"] * 100.0)) if d.get("veg", 1.0) > 1.0 else ""]
		b.custom_minimum_size = Vector2(0, 34)
		b.pressed.connect(func() -> void:
			tool_ = "" if tool_ == k else k
			_refresh_all())
		row.add_child(b)
		palette_btns[k] = b
	var wb := Button.new()
	wb.text = "☁ Weather"
	wb.toggle_mode = true
	wb.button_pressed = true
	wb.tooltip_text = "Show or hide the clouds over your region"
	wb.custom_minimum_size = Vector2(0, 34)
	wb.pressed.connect(func() -> void:
		planet.clouds.visible = not planet.clouds.visible)
	row.add_child(wb)
	var rm := Button.new()
	rm.text = "✕ Remove  5"
	rm.toggle_mode = true
	rm.custom_minimum_size = Vector2(0, 34)
	rm.pressed.connect(func() -> void:
		tool_ = "" if tool_ == "remove" else "remove"
		_refresh_all())
	row.add_child(rm)
	palette_btns["remove"] = rm
	end_btn = Button.new()
	end_btn.text = "End turn  ▸  5 years"
	end_btn.custom_minimum_size = Vector2(190, 34)
	end_btn.pressed.connect(_end_turn)
	row.add_child(end_btn)


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
	_update_camera()


func _update_camera() -> void:
	if cam == null:
		return
	cam.position = Vector3(cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw)) * dist
	cam.look_at(Vector3.ZERO, Vector3.UP)
