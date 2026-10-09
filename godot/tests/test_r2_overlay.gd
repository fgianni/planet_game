extends SceneTree


func _wait_for_live_frame(planet: PlanetMeshNode) -> bool:
	for unused in range(500):
		await create_timer(0.01).timeout
		if planet.poll_live_frame():
			return true
	return false


func _initialize() -> void:
	var arguments := OS.get_cmdline_user_args()
	assert(arguments.size() == 1, "expected the PFRAME01 test recording path")
	var planet := PlanetMeshNode.new()
	root.add_child(planet)
	planet.rebuild(2, 6_371_000.0, 1, "earth_like")
	assert(not planet.has_channel("temperature_anomaly"),
		"orbit-only preview must report the unavailable anomaly honestly")
	planet.load_presentation_record(arguments[0])
	assert(planet.has_channel("temperature_anomaly"),
		"recorded climate frame should expose temperature_anomaly")
	assert(planet.has_channel("precipitation"),
		"schema 5 recording should expose model precipitation")
	var anomaly: float = planet.get_channel_value(0, "temperature_anomaly")
	assert(is_finite(anomaly) and anomaly >= -1.0 and anomaly <= 1.0,
		"temperature anomaly readout must retain the channel's documented range")
	var recorded_precipitation: float = planet.get_channel_value(0, "precipitation")
	assert(is_finite(recorded_precipitation) and recorded_precipitation >= 0.0 \
		and recorded_precipitation <= 1.0,
		"recorded precipitation must retain the channel's documented range")
	assert(is_nan(planet.get_channel_value(-1, "temperature_anomaly")),
		"invalid cells must not manufacture a reading")
	var picked: int = planet.find_cell(Vector3(1.0, 0.0, 0.0))
	assert(picked >= 0 and picked < 162, "direction picking must resolve a mesh cell")
	var revision: int = planet.get_geometry_revision()
	planet.set_view_mode(6)
	assert(planet.get_view_mode_name().contains("temperature anomaly"))
	assert(planet.get_geometry_revision() == revision,
		"selecting the temperature overlay must not rebuild geometry")

	planet.start_live_run(1)
	assert(planet.has_live_run(), "live climate run should start explicitly")
	assert(planet.has_channel("snow_cover") and planet.has_channel("sea_ice"),
		"initial live frame must reset newly available smoothed channels")
	assert(not planet.has_channel("temperature_anomaly"),
		"live anomaly must remain absent until a complete reference year exists")
	var hash_before: String = planet.get_live_state_hash()
	var request_begin := Time.get_ticks_msec()
	planet.request_live_steps()
	assert(Time.get_ticks_msec() - request_begin < 50,
		"queuing a live step must not wait for the simulation")
	var received: bool = await _wait_for_live_frame(planet)
	assert(received, "live climate worker did not publish a frame")
	assert(planet.get_simulation_tick() > 0, "live frame must advance to a scheduler boundary")
	assert(planet.get_live_state_hash() != hash_before,
		"a climate step should change the slow-state hash")
	for unused in range(11):
		planet.request_live_steps()
		assert(await _wait_for_live_frame(planet),
			"live climate worker did not publish the reference year")
	assert(planet.has_channel("temperature_anomaly"),
		"a complete live year must freeze and expose the anomaly reference")
	var hash_after: String = planet.get_live_state_hash()
	planet.next_style()
	planet.set_view_mode(0)
	planet.set_view_mode(6)
	assert(planet.get_live_state_hash() == hash_after,
		"rendering and style operations must not change the live state")
	planet.stop_live_run()
	assert(not planet.has_live_run(), "live worker must stop cleanly")

	# The 36-band circulation is deliberately unresolved at L2. A single L4
	# scheduler step publishes its physical pressure and surface winds.
	planet.clear_presentation_record()
	planet.rebuild(4, 6_371_000.0, 1, "earth_like")
	planet.start_live_run(1, true)
	planet.request_live_steps()
	assert(await _wait_for_live_frame(planet),
		"L4 live climate worker did not publish atmosphere and wind")
	assert(planet.has_channel("atmosphere_density") and planet.has_channel("wind"),
		"resolved circulation must expose atmosphere and wind channels")
	assert(planet.has_channel("precipitation"),
		"live schema 5 frame must expose model precipitation")
	var atmosphere: float = planet.get_channel_value(0, "atmosphere_density")
	assert(is_finite(atmosphere) and atmosphere >= 0.0 and atmosphere <= 1.0,
		"atmosphere density must retain its documented normalized range")
	var wind: Vector2 = planet.get_vector_channel_value(0, "wind")
	assert(is_finite(wind.x) and is_finite(wind.y),
		"wind readout must retain finite signed east/north components")
	var precipitation: float = planet.get_channel_value(0, "precipitation")
	assert(is_finite(precipitation) and precipitation >= 0.0 and precipitation <= 1.0,
		"precipitation must retain its documented normalized range")
	var wettest: float = 0.0
	for cell in range(2562):
		wettest = maxf(wettest, planet.get_channel_value(cell, "precipitation"))
	assert(wettest > 0.0,
		"opt-in water cycle must publish at least one physically raining cell")
	revision = planet.get_geometry_revision()
	var r3_hash: String = planet.get_live_state_hash()
	planet.set_view_mode(7)
	assert(planet.get_view_mode_name().contains("wind"))
	assert(planet.get_wind_streak_count() > 0 and planet.get_wind_streak_count() <= 2048,
		"wind view must use a bounded non-empty streak set")
	assert(planet.get_geometry_revision() == revision,
		"selecting the wind overlay must not rebuild planet geometry")
	planet.set_view_mode(8)
	assert(planet.get_view_mode_name().contains("precipitation"))
	assert(planet.get_precipitation_streak_count() > 0 \
		and planet.get_precipitation_streak_count() <= 2048,
		"precipitation view must use a bounded non-empty physical streak set")
	assert(planet.get_geometry_revision() == revision,
		"selecting the precipitation overlay must not rebuild planet geometry")
	planet.next_style()
	assert(planet.get_live_state_hash() == r3_hash,
		"atmosphere rendering, wind rendering and style changes must not alter state")
	var streaks: int = planet.get_wind_streak_count()
	var rain_streaks: int = planet.get_precipitation_streak_count()
	planet.stop_live_run()
	planet.queue_free()
	print("R2 live/overlay pass: cell ", picked, " anomaly ", anomaly,
		" hash ", hash_after, "; R3 wind ", wind, " streaks ", streaks,
		"; R4 precipitation max ", wettest, " streaks ", rain_streaks)
	quit(0)
