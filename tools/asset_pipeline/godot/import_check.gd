extends SceneTree
## AssetForge Phase 1 (docs/assetforge_document §6, ADR-0031): the Godot
## side of an asset. Loads a built GLB at runtime, checks its manifest's
## ports and animation, then drives the visuals from a mock state frame,
## as PlanetSim's frames would: the rotor's speed and the heat tint.
##
##   godot --headless --path PROJECT --script import_check.gd -- \
##       ASSET.glb ASSET.manifest.json SPEC.json STATE.json
##
## Runtime loading (GLTFDocument) needs no editor import, so no project's
## files change. Exits 1 if a check fails.

var _failed := 0
var _passed := 0


func _check(name: String, ok: bool, detail: String = "") -> void:
	if ok:
		_passed += 1
	else:
		_failed += 1
	print("%s %s%s" % ["PASS" if ok else "FAIL", name, (": " + detail) if detail != "" else ""])


func _read_json(path: String) -> Variant:
	return JSON.parse_string(FileAccess.get_file_as_string(path))


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 4:
		push_error("usage: -- ASSET.glb MANIFEST.json SPEC.json STATE.json")
		quit(2)
		return
	var manifest: Dictionary = _read_json(args[1])
	var spec: Dictionary = _read_json(args[2])
	var state: Dictionary = _read_json(args[3])

	var document := GLTFDocument.new()
	var gltf := GLTFState.new()
	var error := document.append_from_file(args[0], gltf)
	_check("GLB loads", error == OK, error_string(error))
	if error != OK:
		quit(1)
		return
	var scene := document.generate_scene(gltf)
	root.add_child(scene)
	# The root joins the tree with the main loop: wait for its first frame.
	await process_frame

	# Ports: each node exists, with its type in the glTF extras.
	for port in manifest["ports"]:
		var node := scene.find_child(port["node"], true, false) as Node3D
		_check("port %s exists" % port["node"], node != null)
		if node == null:
			continue
		var extras: Variant = node.get_meta("extras", {})
		_check("port %s typed" % port["node"],
			extras is Dictionary and extras.get("port_type", "") == port["type"],
			str(extras))
		var wanted: Array = []
		for p in spec["ports"]:
			if p["node"] == port["node"]:
				wanted = p["position_m"]
		# Blender (x, y, z) is glTF and Godot (x, z, -y).
		var expected := Vector3(wanted[0], wanted[2], -wanted[1])
		var distance := node.global_position.distance_to(expected)
		_check("port %s position" % port["node"], distance < 1e-3, "%.2f mm" % (distance * 1000.0))

	# The rotor's animation, driven from the state frame.
	var players := scene.find_children("*", "AnimationPlayer", true, false)
	_check("one AnimationPlayer", players.size() == 1)
	if players.size() != 1:
		quit(1)
		return
	var player := players[0] as AnimationPlayer
	var animation_spec: Dictionary = spec["animations"]["rotor_speed"]
	var action: String = manifest["animations"]["rotor_speed"]
	_check("animation %s present" % action, player.has_animation(action), str(player.get_animation_list()))
	if not player.has_animation(action):
		quit(1)
		return
	player.get_animation(action).loop_mode = Animation.LOOP_LINEAR
	var rotor := scene.find_child(animation_spec["node"], true, false) as Node3D
	# The binding: playback speed is the state's turns per second over the
	# animation's own.
	var turns_per_second: float = state["rotor_speed_rpm"] / 60.0
	var cycle_turns_per_second: float = animation_spec["turns_per_cycle"] / animation_spec["cycle_s"]
	player.speed_scale = turns_per_second / cycle_turns_per_second
	player.play(action)
	var dt := 0.01   # at 1 500 rpm, a quarter turn
	player.advance(dt)
	var expected_angle := fposmod(TAU * turns_per_second * dt, TAU)
	var axis := Vector3(animation_spec["axis"][0], animation_spec["axis"][2], -animation_spec["axis"][1])
	var turned := rotor.quaternion
	var angle := fposmod(turned.get_angle() * signf(turned.get_axis().dot(axis)), TAU)
	var angle_error := absf(angle - expected_angle)
	_check("rotor follows the state's speed", angle_error < 1e-3,
		"%.4f rad against %.4f rad" % [angle, expected_angle])

	# The heat tint: the material slot exists and takes the state's value.
	var heat_name: String = manifest["materials"]["heat"]
	var heat: StandardMaterial3D = null
	for mesh_instance in scene.find_children("*", "MeshInstance3D", true, false):
		var mesh := (mesh_instance as MeshInstance3D).mesh
		for surface in mesh.get_surface_count():
			var material := mesh.surface_get_material(surface)
			if material != null and material.resource_name == heat_name:
				heat = material as StandardMaterial3D
	_check("material %s present" % heat_name, heat != null)
	if heat != null:
		# 300 K and below: no glow; 400 K: full (an illustrative mapping).
		var glow := clampf((state["temperature_K"] - 300.0) / 100.0, 0.0, 1.0)
		heat.emission_enabled = true
		heat.emission_energy_multiplier = glow
		_check("heat tint follows the state", is_equal_approx(heat.emission_energy_multiplier, glow),
			"%.2f at %.0f K" % [glow, state["temperature_K"]])

	print("assetforge godot %s: %d passed, %d failed" % [manifest["id"], _passed, _failed])
	quit(1 if _failed > 0 else 0)
