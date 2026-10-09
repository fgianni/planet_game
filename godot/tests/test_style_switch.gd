extends SceneTree


func _initialize() -> void:
	var planet := PlanetMeshNode.new()
	var arguments := OS.get_cmdline_user_args()
	planet.rebuild(2 if not arguments.is_empty() else 0, 6_371_000.0, 1, "earth_like")
	if not arguments.is_empty():
		planet.load_presentation_record(arguments[0])
	var geometry_revision: int = planet.get_geometry_revision()
	var frame_count: int = planet.get_presentation_frame_count()
	planet.set_style("map")
	planet.set_style("stylised")
	assert(planet.get_geometry_revision() == geometry_revision,
		"style switching rebuilt the planet geometry")
	assert(planet.get_presentation_frame_count() == frame_count,
		"style switching re-read or discarded the presentation record")
	var error: String = planet.validate_style_manifest(
		"res://tests/data/incomplete_style.tres")
	assert(error.contains("surface_class"),
		"an incomplete style was not rejected with the missing channel name: " + error)
	print("R1-02 V7/V9 pass: ", error)
	planet.free()
	quit(0)
