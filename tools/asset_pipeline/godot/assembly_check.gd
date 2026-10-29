extends SceneTree
## AssetForge Phase 3 (docs/assetforge_document §6, §9; ADR-0031): a layout
## assembled in Godot. Instantiates each module from its editor-imported GLB,
## places it, builds its pipes and cables, checks that every route starts and
## ends on the imported ports and that shafts mate, drives every animation
## from one plant state frame, and prints a digest of the assembly.
##
##   godot --headless --path PROJECT --script assembly_check.gd -- \
##       LAYOUT.json SPECIFICATIONS_DIR PLANT_STATE.json
##
## PROJECT holds the modules' GLBs as res://<asset>.glb, already imported.
## Exits 1 if a check fails.

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


# Blender (x, y, z) is glTF and Godot (x, z, -y).
func _godot(v: Array) -> Vector3:
	return Vector3(v[0], v[2], -v[1])


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3:
		push_error("usage: -- LAYOUT.json SPECIFICATIONS_DIR PLANT_STATE.json")
		quit(2)
		return
	var layout: Dictionary = _read_json(args[0])
	var plant_state: Dictionary = _read_json(args[2])
	_check("the layout is feasible", layout["feasible"], str(layout["errors"]))

	var plant := Node3D.new()
	plant.name = layout["blueprint"]
	root.add_child(plant)
	var instances := {}
	var specs := {}
	for module in layout["modules"]:
		var packed := load("res://%s.glb" % module["asset"]) as PackedScene
		_check("module %s imported" % module["id"], packed != null)
		if packed == null:
			continue
		var instance := packed.instantiate() as Node3D
		instance.name = module["id"]
		plant.add_child(instance)
		instance.position = _godot(module["position_m"])
		# A Blender turn about +Z is the same turn about Godot's +Y.
		instance.rotation = Vector3(0.0, deg_to_rad(float(module["yaw_deg"])), 0.0)
		instances[module["id"]] = instance
		specs[module["id"]] = _read_json("%s/%s.json" % [args[1], module["specification"]])
	await process_frame

	# Pipes and cables, one cylinder per leg.
	var legs := 0
	for route in layout["routes"]:
		var points: Array = route["points"]
		for i in range(points.size() - 1):
			var a := _godot(points[i])
			var b := _godot(points[i + 1])
			if a.distance_to(b) < 1e-6:
				continue
			var mesh := CylinderMesh.new()
			mesh.top_radius = route["radius_m"]
			mesh.bottom_radius = route["radius_m"]
			mesh.height = a.distance_to(b)
			var leg := MeshInstance3D.new()
			leg.mesh = mesh
			plant.add_child(leg)
			leg.global_transform = Transform3D(Basis(Quaternion(Vector3.UP, (b - a).normalized())), (a + b) / 2.0)
			legs += 1
	_check("pipes and cables built", legs > 0, "%d legs" % legs)

	# Every route starts and ends on its imported ports; shafts mate.
	var digest := PackedStringArray()
	for route in layout["routes"]:
		var ends: Array = route["connection"].split(" -> ")
		var ports := []
		for end in ends:
			var parts: PackedStringArray = end.split(".")
			var node_name := ""
			for port in specs[parts[0]]["ports"]:
				if port["id"] == parts[1]:
					node_name = port["node"]
			ports.append(instances[parts[0]].find_child(node_name, true, false) as Node3D)
		if ports.has(null):
			_check("%s: ports found" % route["connection"], false)
			continue
		var points: Array = route["points"]
		if route["medium"] == "shaft":
			var gap: float = ports[0].global_position.distance_to(ports[1].global_position)
			var facing: float = ports[0].global_basis.y.normalized().dot(ports[1].global_basis.y.normalized())
			_check("%s: mated" % route["connection"], gap < 1e-3 and facing < -0.999,
				"gap %.2f mm, facing %.4f" % [gap * 1000.0, facing])
		else:
			var start_error: float = ports[0].global_position.distance_to(_godot(points[0]))
			var end_error: float = ports[1].global_position.distance_to(_godot(points[-1]))
			_check("%s: on its ports" % route["connection"], start_error < 1e-3 and end_error < 1e-3,
				"%.2f and %.2f mm" % [start_error * 1000.0, end_error * 1000.0])
		for port in ports:
			var p: Vector3 = port.global_position
			digest.append("%s %.4f %.4f %.4f" % [port.get_path(), p.x, p.y, p.z])

	# Every animation from the plant's state frame.
	for id in instances:
		var state: Dictionary = plant_state.get(id, {})
		var players: Array = (instances[id] as Node).find_children("*", "AnimationPlayer", true, false)
		var animations: Dictionary = specs[id].get("animations", {})
		if animations.is_empty():
			continue
		var player := players[0] as AnimationPlayer
		for key in animations:
			var animation: Dictionary = animations[key]
			var binding: Dictionary = animation.get("binding", {"state": "rotor_speed_rpm", "kind": "speed_rpm"})
			var value: float = state.get(binding["state"], 0.0)
			player.play(animation["action"])
			if binding["kind"] == "position":
				player.speed_scale = 1.0
				player.seek(clampf(value, 0.0, 1.0) * float(animation["cycle_s"]), true)
				player.pause()
			else:
				player.speed_scale = (value / 60.0) / (float(animation["turns_per_cycle"]) / float(animation["cycle_s"]))
				player.advance(0.01)
			_check("%s.%s driven by %s" % [id, key, binding["state"]], player.has_animation(animation["action"]),
				"%s = %s" % [binding["state"], value])

	print("assetforge assembly digest %s" % str(hash("\n".join(digest))))
	print("assetforge assembly %s: %d passed, %d failed" % [layout["blueprint"], _passed, _failed])
	quit(1 if _failed > 0 else 0)
