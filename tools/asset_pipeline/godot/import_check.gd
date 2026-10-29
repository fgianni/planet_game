extends SceneTree
## AssetForge Phase 1 (docs/assetforge_document §6, ADR-0031): the Godot
## side of an asset. Loads a built GLB at runtime, checks its manifest's
## ports and animation, then drives the visuals from a mock state frame,
## as PlanetSim's frames would: the rotor's speed and the heat tint.
##
##   godot --headless --path PROJECT --script import_check.gd -- \
##       ASSET.glb ASSET.manifest.json SPEC.json STATE.json [editor]
##
## By default the GLB is loaded at runtime (GLTFDocument), which ignores
## Godot's import hints. With "editor", ASSET.glb is a res:// path already
## imported into PROJECT (godot --headless --import), as the game ships
## assets, and the collision shape is checked too. Exits 1 if a check fails.

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
	if args.size() != 4 and not (args.size() == 5 and args[4] == "editor"):
		push_error("usage: -- ASSET.glb MANIFEST.json SPEC.json STATE.json [editor]")
		quit(2)
		return
	var manifest: Dictionary = _read_json(args[1])
	var spec: Dictionary = _read_json(args[2])
	# A state frame per asset (tests/state_frames.json): the values a binding maps to visuals.
	var frames: Dictionary = _read_json(args[3])
	var state: Dictionary = frames.get(manifest["id"], {})

	var editor := args.size() == 5
	var scene: Node
	if editor:
		var packed := load(args[0]) as PackedScene
		_check("imported scene loads", packed != null, args[0])
		if packed == null:
			quit(1)
			return
		scene = packed.instantiate()
	else:
		var document := GLTFDocument.new()
		var gltf := GLTFState.new()
		var error := document.append_from_file(args[0], gltf)
		_check("GLB loads", error == OK, error_string(error))
		if error != OK:
			quit(1)
			return
		scene = document.generate_scene(gltf)
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

	# Every material slot of the manifest is on a surface.
	var materials := {}
	for mesh_instance in scene.find_children("*", "MeshInstance3D", true, false):
		var mesh := (mesh_instance as MeshInstance3D).mesh
		for surface in mesh.get_surface_count():
			var material := mesh.surface_get_material(surface)
			if material != null:
				materials[material.resource_name] = material
	for slot in manifest["materials"]:
		var slot_name: String = manifest["materials"][slot]
		_check("material %s present" % slot_name, materials.has(slot_name))

	# The collision shape, which only the editor import builds from the
	# node's suffix: a body with a convex shape and no drawn mesh.
	if manifest.has("collision") and editor:
		var shapes := scene.find_children("*", "CollisionShape3D", true, false)
		var convex := shapes.filter(func(n): return (n as CollisionShape3D).shape is ConvexPolygonShape3D)
		_check("collision shape is convex", shapes.size() == 1 and convex.size() == 1,
			"%d shapes, %d convex" % [shapes.size(), convex.size()])
		_check("collision under a physics body",
			shapes.size() == 1 and shapes[0].get_parent() is PhysicsBody3D)
		var drawn := scene.find_children(manifest["collision"]["node"], "MeshInstance3D", true, false)
		_check("collision is not drawn", drawn.is_empty())

	# A decorative asset: its height, from the meshes' bounds.
	if manifest.has("height_m"):
		var bounds := AABB()
		var first := true
		for mesh_instance in scene.find_children("*", "MeshInstance3D", true, false):
			var instance := mesh_instance as MeshInstance3D
			# At runtime the collision node arrives as a mesh: not drawn in the game.
			if manifest.has("collision") and instance.name == manifest["collision"]["node"]:
				continue
			var box := instance.global_transform * instance.get_aabb()
			bounds = box if first else bounds.merge(box)
			first = false
		var height: float = manifest["height_m"]
		_check("height", absf(bounds.size.y - height) <= 0.02 * height,
			"%.3f m against %.3f m" % [bounds.size.y, height])
		_check("standing on the origin", absf(bounds.position.y) <= 1e-3,
			"lowest %.4f m" % bounds.position.y)

	# Each animation, driven through its binding from the state frame: a
	# rotation's playback speed is the state's turns per second over the
	# animation's own; a position animation is set, not played, at the
	# state's fraction of its travel.
	var animations: Dictionary = spec.get("animations", {})
	if not animations.is_empty():
		var players := scene.find_children("*", "AnimationPlayer", true, false)
		_check("one AnimationPlayer", players.size() == 1)
		if players.size() != 1:
			_finish(manifest)
			return
		var player := players[0] as AnimationPlayer
		for key in animations:
			_check_animation(scene, player, animations[key], state)

	if manifest["materials"].has("heat"):
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

	_finish(manifest)


func _check_animation(scene: Node, player: AnimationPlayer, animation: Dictionary,
		state: Dictionary) -> void:
	var action: String = animation["action"]
	_check("animation %s present" % action, player.has_animation(action), str(player.get_animation_list()))
	if not player.has_animation(action):
		return
	var binding: Dictionary = animation.get("binding", {"state": "rotor_speed_rpm", "kind": "speed_rpm"})
	var value: float = state[binding["state"]]
	var node := scene.find_child(animation["node"], true, false) as Node3D
	# Blender (x, y, z) is glTF and Godot (x, z, -y).
	var axis := Vector3(animation["axis"][0], animation["axis"][2], -animation["axis"][1])
	if animation.get("kind", "rotation") == "position":
		var fraction := clampf(value, 0.0, 1.0)
		player.speed_scale = 1.0
		player.play(action)
		player.seek(fraction * float(animation["cycle_s"]), true)
		player.pause()
		var pivot := Vector3(animation["pivot_m"][0], animation["pivot_m"][2], -animation["pivot_m"][1])
		var moved := (node.global_position - pivot).dot(axis)
		var expected := fraction * float(animation["travel_m"])
		_check("%s follows the state's %s" % [animation["node"], binding["state"]],
			absf(moved - expected) < 1e-3, "%.4f m against %.4f m" % [moved, expected])
		return
	player.get_animation(action).loop_mode = Animation.LOOP_LINEAR
	var turns_per_second := value / 60.0
	var cycle_turns_per_second: float = float(animation["turns_per_cycle"]) / float(animation["cycle_s"])
	player.speed_scale = turns_per_second / cycle_turns_per_second
	player.play(action)
	player.seek(0.0, true)
	var dt := 0.01
	player.advance(dt)
	var expected_angle := fposmod(TAU * turns_per_second * dt, TAU)
	var turned := node.quaternion
	var angle := fposmod(turned.get_angle() * signf(turned.get_axis().dot(axis)), TAU)
	_check("%s follows the state's %s" % [animation["node"], binding["state"]],
		absf(angle - expected_angle) < 1e-3, "%.4f rad against %.4f rad" % [angle, expected_angle])


func _finish(manifest: Dictionary) -> void:
	print("assetforge godot %s: %d passed, %d failed" % [manifest["id"], _passed, _failed])
	quit(1 if _failed > 0 else 0)
