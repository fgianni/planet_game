extends SceneTree

## The drawer is presentation-only; solar requests cross the live worker's
## command boundary and cannot be sent to an orbit preview.
func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var main: Node3D = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await process_frame
	var ui: CanvasLayer = main.get_node("Hud")
	var planet: PlanetMeshNode = main.get_node("Planet")
	main._paused = true
	assert(not ui.is_drawer_open())
	ui.toggle_tab("Controls")
	assert(ui.is_drawer_open())
	ui.toggle_tab("Inspect")
	main._selected_cell = 0
	main._update_hud()
	assert(ui._controls["selection"].text.contains("Cell 0"),
		"selected-cell readings belong in the inspector")
	assert(ui._controls["selection"].text.contains("mean elevation"))
	ui.close_drawer()
	assert(not ui.is_drawer_open())
	assert(not planet.queue_live_solar_factor(1.02), "preview cannot accept a forcing command")
	var original_seed: int = planet.get_seed()
	assert(planet.find_coastal_cell(Vector3(0, 0, 1)) >= 0,
		"Earth-like terrain needs a coastal focus for capture presets")
	assert(abs(planet.get_coastal_focus(Vector3(0, 0, 1)).length() - 1.0) < 1e-5,
		"coastal focus must remain a unit direction")
	main._ui_action("style", "map")
	assert(planet.get_seed() == original_seed and planet.get_style() == "map")
	main._ui_action("live", true)
	assert(planet.has_live_run())
	main.set_process(false) # the test, not Main, consumes the worker's frame/events
	var initial_hash: String = planet.get_live_state_hash()
	ui.toggle_tab("Log")
	main._ui_action("clouds", false)
	main._ui_action("relief", 4.0)
	assert(planet.get_live_state_hash() == initial_hash,
		"drawer and presentation controls must not mutate the live simulation")
	ui.close_drawer()
	assert(not planet.queue_live_solar_factor(0.0))
	assert(planet.queue_live_solar_factor(1.02))
	planet.request_live_steps()
	var received := false
	for unused in 500:
		await create_timer(0.01).timeout
		if planet.poll_live_frame():
			received = true
			break
	assert(received, "live worker did not advance")
	var events: PackedStringArray = planet.take_live_events()
	assert(events.size() == 1 and events[0].contains("solar factor 1.02 applied at tick"),
		"command must be acknowledged after its step: %s" % str(events))
	assert(planet.get_simulation_tick() > 0)
	var control := PlanetMeshNode.new()
	root.add_child(control)
	control.rebuild(2, 6_371_000.0, original_seed, "earth_like")
	control.start_live_run(1)
	control.request_live_steps()
	received = false
	for unused in 500:
		await create_timer(0.01).timeout
		if control.poll_live_frame():
			received = true
			break
	assert(received and planet.get_live_state_hash() != control.get_live_state_hash(),
		"solar command must change the live state relative to an unforced run")
	control.stop_live_run()
	control.free()
	planet.stop_live_run()
	main.free()
	print("climate lab UI and solar command pass")
	quit(0)
