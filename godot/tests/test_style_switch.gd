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
	_check_style_directories(planet, geometry_revision)
	planet.free()
	quit(0)


## Survey step 2 (ADR-0018 §4.5): a style pack is a directory.  A new
## godot/styles/<name>/ appears on reload without C++; a broken one is
## refused while the others still load.
func _check_style_directories(planet: PlanetMeshNode, geometry_revision: int) -> void:
	var added := "res://styles/ci_added_pack"
	var broken := "res://styles/ci_broken_pack"
	var stylised := FileAccess.get_file_as_string("res://styles/stylised/style.tres")
	_write_pack(added, stylised.replace('style_name = "stylised"', 'style_name = "ci-added"'))
	_write_pack(broken, FileAccess.get_file_as_string("res://tests/data/incomplete_style.tres"))
	planet.reload_styles()
	var available: PackedStringArray = planet.get_available_styles()
	_remove_pack(added)
	_remove_pack(broken)
	assert(available.has("ci-added"), "a new style directory did not load: %s" % [available])
	assert(available.has("stylised") and available.has("map"),
		"a broken style directory stopped the others loading: %s" % [available])
	assert(not available.has("incomplete-test-style"), "a broken style directory was not refused")
	assert(planet.get_style() == "stylised", "reloading styles changed the active style")
	planet.set_style("ci-added")
	assert(planet.get_style() == "ci-added", "the new style directory could not be selected")
	assert(planet.get_geometry_revision() == geometry_revision,
		"loading or switching to a new style rebuilt the planet geometry")
	planet.reload_styles()
	assert(not planet.get_available_styles().has("ci-added"), "a removed style directory stayed loaded")
	assert(planet.get_style() == "stylised", "removing the active style did not fall back to the default")
	print("style directories pass: ", available)


func _write_pack(directory: String, manifest: String) -> void:
	DirAccess.make_dir_recursive_absolute(directory)
	var file := FileAccess.open(directory.path_join("style.tres"), FileAccess.WRITE)
	file.store_string(manifest)
	file.close()


func _remove_pack(directory: String) -> void:
	for file_name in DirAccess.get_files_at(directory):
		DirAccess.remove_absolute(directory.path_join(file_name))
	DirAccess.remove_absolute(directory)
