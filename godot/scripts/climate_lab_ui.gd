extends CanvasLayer

## The climate lab's optional right drawer. It only sends requests to Main;
## it never owns simulation state or reads solver arrays.
signal action(kind: String, value: Variant)

const MAX_LOG := 200
const DRAWER_WIDTH := 370.0
const CATEGORIES := ["All", "Action", "Simulation", "Error"]

var _drawer: PanelContainer
var _tabs: TabContainer
var _status: Label
var _log_text: RichTextLabel
var _log_filter: OptionButton
var _entries: Array[Dictionary] = []
var _controls := {}
var _buttons := {}


func _ready() -> void:
	var canvas := Control.new()
	canvas.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	canvas.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(canvas)
	var strip := PanelContainer.new()
	strip.position = Vector2(12, 10)
	strip.mouse_filter = Control.MOUSE_FILTER_IGNORE
	canvas.add_child(strip)
	_status = Label.new()
	_status.add_theme_color_override("font_color", Color(0.92, 0.94, 0.97))
	strip.add_child(_status)
	var buttons := HBoxContainer.new()
	buttons.anchor_left = 1.0
	buttons.anchor_right = 1.0
	buttons.offset_top = 10.0
	buttons.offset_bottom = 44.0
	canvas.add_child(buttons)
	for tab in ["Help", "Controls", "Inspect", "Log"]:
		var button := Button.new()
		button.text = tab
		button.toggle_mode = true
		button.pressed.connect(toggle_tab.bind(tab))
		buttons.add_child(button)
		_buttons[tab] = button
	_drawer = PanelContainer.new()
	_drawer.anchor_left = 1.0
	_drawer.anchor_right = 1.0
	_drawer.anchor_bottom = 1.0
	_drawer.offset_top = 52.0
	_drawer.offset_right = -12.0
	_drawer.offset_bottom = -12.0
	canvas.add_child(_drawer)
	_tabs = TabContainer.new()
	_tabs.tabs_visible = false
	_drawer.add_child(_tabs)
	_make_help()
	_make_controls()
	_make_inspector()
	_make_log()
	_drawer.visible = false
	get_viewport().size_changed.connect(_resize)
	_resize()


func _resize() -> void:
	var width := minf(DRAWER_WIDTH, get_viewport().get_visible_rect().size.x - 24.0)
	_drawer.offset_left = -maxf(24.0, width + 12.0)
	var buttons: HBoxContainer = _buttons["Help"].get_parent()
	buttons.offset_left = -minf(300.0, width)
	buttons.offset_right = -12.0


func toggle_tab(tab: String) -> void:
	if _drawer.visible and _tabs.get_tab_title(_tabs.current_tab) == tab:
		_drawer.visible = false
	else:
		for index in _tabs.get_tab_count():
			if _tabs.get_tab_title(index) == tab:
				_tabs.current_tab = index
		_drawer.visible = true
	for name in _buttons:
		_buttons[name].button_pressed = _drawer.visible and name == tab


func close_drawer() -> void:
	_drawer.visible = false
	for button in _buttons.values():
		button.button_pressed = false


func is_drawer_open() -> bool:
	return _drawer.visible


func set_status(text: String) -> void:
	_status.text = "  " + text + "  "


func set_help(text: String) -> void:
	_controls["help"].text = text


func _make_help() -> void:
	var help := RichTextLabel.new()
	help.name = "Help"
	help.bbcode_enabled = false
	help.scroll_active = true
	_tabs.add_child(help)
	_controls["help"] = help


func _row(parent: VBoxContainer, title: String, control: Control) -> void:
	var row := HBoxContainer.new()
	parent.add_child(row)
	var label := Label.new()
	label.text = title
	label.custom_minimum_size.x = 105
	row.add_child(label)
	control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(control)


func _make_controls() -> void:
	var scroll := ScrollContainer.new()
	scroll.name = "Controls"
	_tabs.add_child(scroll)
	var box := VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(box)
	var view := Label.new()
	view.text = "View — does not change the planet"
	box.add_child(view)
	var style := OptionButton.new()
	for name in ["stylised", "map"]:
		style.add_item(name)
	style.item_selected.connect(func(index: int) -> void: action.emit("style", style.get_item_text(index)))
	_row(box, "Style", style)
	_controls["style"] = style
	for kind in ["clouds", "day_night", "pause", "live"]:
		var toggle := CheckButton.new()
		toggle.toggled.connect(func(enabled: bool) -> void: action.emit(kind, enabled))
		_row(box, {"clouds": "Clouds", "day_night": "Day/night", "pause": "Paused", "live": "Live climate"}[kind], toggle)
		_controls[kind] = toggle
	var relief := HSlider.new()
	relief.min_value = 0.0
	relief.max_value = 100.0
	relief.step = 1.0
	relief.value_changed.connect(func(value: float) -> void: action.emit("relief", value))
	_row(box, "Relief ×", relief)
	_controls["relief"] = relief
	var speed := SpinBox.new()
	speed.min_value = 0.125
	speed.max_value = 16.0
	speed.step = 0.125
	speed.value_changed.connect(func(value: float) -> void: action.emit("speed", value))
	_row(box, "Playback speed", speed)
	_controls["speed"] = speed
	var capture := Button.new()
	capture.text = "Save clear view (PNG)"
	capture.tooltip_text = "Hide the drawer for one frame and save the current rendered view"
	capture.pressed.connect(func() -> void: action.emit("capture", null))
	box.add_child(capture)
	var series := Button.new()
	series.text = "Capture orbit / coast / local"
	series.tooltip_text = "Same focus and simulation instant in three altitude bands"
	series.pressed.connect(func() -> void: action.emit("capture_series", null))
	box.add_child(series)
	var progress := Label.new()
	progress.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	progress.text = ""
	box.add_child(progress)
	_controls["capture_progress"] = progress
	var cancel := Button.new()
	cancel.text = "Cancel capture series"
	cancel.visible = false
	cancel.pressed.connect(func() -> void: action.emit("cancel_capture_series", null))
	box.add_child(cancel)
	_controls["capture_cancel"] = cancel
	box.add_child(HSeparator.new())
	var setup := Label.new()
	setup.text = "New planet — apply to regenerate"
	box.add_child(setup)
	var seed := LineEdit.new()
	seed.placeholder_text = "World seed"
	_row(box, "Seed", seed)
	_controls["seed"] = seed
	var preset := OptionButton.new()
	for name in ["earth_like", "aqua_planet", "dead_rock"]:
		preset.add_item(name)
	_row(box, "Preset", preset)
	_controls["preset"] = preset
	var level := SpinBox.new()
	level.min_value = 2.0
	level.max_value = 7.0
	level.step = 1.0
	_row(box, "Subdivision", level)
	_controls["level"] = level
	var regenerate := Button.new()
	regenerate.text = "Regenerate planet"
	regenerate.pressed.connect(func() -> void: action.emit("regenerate", {
		"seed": seed.text, "preset": preset.get_item_text(preset.selected), "level": int(level.value)}))
	box.add_child(regenerate)
	box.add_child(HSeparator.new())
	var forcing := Label.new()
	forcing.text = "Live simulation — takes effect at next step"
	box.add_child(forcing)
	var solar := SpinBox.new()
	solar.min_value = 0.5
	solar.max_value = 1.5
	solar.step = 0.01
	solar.value = 1.0
	_row(box, "Solar factor", solar)
	_controls["solar"] = solar
	var apply := Button.new()
	apply.text = "Apply solar factor"
	apply.pressed.connect(func() -> void: action.emit("solar", solar.value))
	box.add_child(apply)
	_controls["solar_apply"] = apply


func _make_inspector() -> void:
	var box := VBoxContainer.new()
	box.name = "Inspect"
	_tabs.add_child(box)
	var title := Label.new()
	title.text = "Right-click the planet to inspect a cell"
	box.add_child(title)
	var selection := RichTextLabel.new()
	selection.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_child(selection)
	_controls["selection"] = selection
	var copy := Button.new()
	copy.text = "Copy readings"
	copy.pressed.connect(func() -> void: DisplayServer.clipboard_set(selection.text))
	box.add_child(copy)


func update_state(state: Dictionary) -> void:
	for kind in ["clouds", "day_night", "pause", "live", "relief", "speed"]:
		var control: Control = _controls[kind]
		control.set_block_signals(true)
		if kind in ["relief", "speed"]:
			control.value = state[kind]
		else:
			control.button_pressed = state[kind]
		control.set_block_signals(false)
	var style: OptionButton = _controls["style"]
	style.set_block_signals(true)
	style.select(0 if state.style == "stylised" else 1)
	style.set_block_signals(false)
	_controls["solar"].editable = state.live
	_controls["solar_apply"].disabled = not state.live
	_controls["selection"].text = state.selection


func set_planet_setup(seed: int, preset: String, level: int) -> void:
	_controls["seed"].text = str(seed)
	var selector: OptionButton = _controls["preset"]
	for index in selector.item_count:
		if selector.get_item_text(index) == preset:
			selector.select(index)
	_controls["level"].value = level
	_controls["solar"].value = 1.0


func set_capture_progress(text: String, active: bool) -> void:
	_controls["capture_progress"].text = text
	_controls["capture_cancel"].visible = active


func _make_log() -> void:
	var box := VBoxContainer.new()
	box.name = "Log"
	_tabs.add_child(box)
	var tools := HBoxContainer.new()
	box.add_child(tools)
	_log_filter = OptionButton.new()
	for category in CATEGORIES:
		_log_filter.add_item(category)
	_log_filter.item_selected.connect(func(_index: int) -> void: _refresh_log())
	tools.add_child(_log_filter)
	var copy := Button.new()
	copy.text = "Copy"
	copy.pressed.connect(func() -> void: DisplayServer.clipboard_set(_visible_log()))
	tools.add_child(copy)
	var save := Button.new()
	save.text = "Save"
	save.pressed.connect(_save_log)
	tools.add_child(save)
	_log_text = RichTextLabel.new()
	_log_text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_child(_log_text)


func log_event(category: String, message: String) -> void:
	_entries.append({"category": category, "text": "[%s] %s: %s" % [
		Time.get_datetime_string_from_system(), category, message]})
	if _entries.size() > MAX_LOG:
		_entries.pop_front()
	_refresh_log()


func _visible_log() -> String:
	var lines := PackedStringArray()
	var selected: String = CATEGORIES[_log_filter.selected]
	for entry in _entries:
		if selected == "All" or entry.category == selected:
			lines.append(entry.text)
	return "\n".join(lines)


func _refresh_log() -> void:
	_log_text.text = _visible_log()


func _save_log() -> void:
	var path := "user://planet_log_%d.txt" % int(Time.get_unix_time_from_system())
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		log_event("Error", "Could not save log: %s" % error_string(FileAccess.get_open_error()))
		return
	file.store_string(_visible_log() + "\n")
	log_event("Action", "Log saved to %s" % ProjectSettings.globalize_path(path))
