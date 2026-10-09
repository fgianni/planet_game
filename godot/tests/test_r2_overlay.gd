extends SceneTree


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
	var anomaly: float = planet.get_channel_value(0, "temperature_anomaly")
	assert(is_finite(anomaly) and anomaly >= -1.0 and anomaly <= 1.0,
		"temperature anomaly readout must retain the channel's documented range")
	assert(is_nan(planet.get_channel_value(-1, "temperature_anomaly")),
		"invalid cells must not manufacture a reading")
	var picked: int = planet.find_cell(Vector3(1.0, 0.0, 0.0))
	assert(picked >= 0 and picked < 162, "direction picking must resolve a mesh cell")
	var revision: int = planet.get_geometry_revision()
	planet.set_view_mode(6)
	assert(planet.get_view_mode_name().contains("temperature anomaly"))
	assert(planet.get_geometry_revision() == revision,
		"selecting the temperature overlay must not rebuild geometry")
	planet.queue_free()
	print("R2-01 overlay/readout pass: cell ", picked, " anomaly ", anomaly)
	quit(0)
