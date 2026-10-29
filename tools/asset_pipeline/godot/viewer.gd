extends Node3D
## AssetForge viewer: the assembled plant and each asset on its own, for
## people (not a check). Built into a throwaway project by
## tools/asset_pipeline/cli/viewer.py.
##
## Tab / Shift+Tab: next / previous view. Left drag: orbit. Wheel: zoom.
## Space: pause the animations.

const PIPE_COLOURS := {"pipe": Color(0.55, 0.57, 0.6), "cable": Color(0.95, 0.45, 0.05)}

var _views: Array = []          # {"title": String, "build": Callable}
var _index := 0
var _content: Node3D
var _label: Label
var _camera: Camera3D
var _yaw := -0.6
var _pitch := 0.45
var _distance := 10.0
var _target := Vector3.ZERO
var _paused := false
var _players: Array = []


func _read_json(path: String) -> Variant:
	return JSON.parse_string(FileAccess.get_file_as_string(path))


func _ready() -> void:
	_build_stage()
	var catalogue: Dictionary = _read_json("res://catalogue.json")
	_views.append({"title": "Plant: %s" % catalogue["layout"]["blueprint"],
		"build": _build_plant.bind(catalogue)})
	for asset in catalogue["assets"]:
		_views.append({"title": "%s (%s)" % [asset["id"], asset["type"]],
			"build": _build_single.bind(asset, catalogue)})
	_show(0)
	# A headless self-test: build every view once, then quit.
	if "--selftest" in OS.get_cmdline_user_args():
		for i in range(_views.size()):
			_show(i)
			await get_tree().process_frame
			print("viewer view %d: %s" % [i, _views[i]["title"]])
		print("viewer selftest: %d views" % _views.size())
		get_tree().quit()


func _build_stage() -> void:
	var environment := Environment.new()
	environment.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	environment.sky = sky
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	environment.ambient_light_energy = 0.45
	environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	environment.tonemap_exposure = 0.8
	var world := WorldEnvironment.new()
	world.environment = environment
	add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation = Vector3(deg_to_rad(-50), deg_to_rad(-35), 0)
	sun.shadow_enabled = true
	sun.light_energy = 0.8
	add_child(sun)
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(80, 80)
	ground.mesh = plane
	var ground_material := StandardMaterial3D.new()
	ground_material.albedo_color = Color(0.36, 0.38, 0.34)
	ground.material_override = ground_material
	add_child(ground)
	_camera = Camera3D.new()
	add_child(_camera)
	var layer := CanvasLayer.new()
	add_child(layer)
	_label = Label.new()
	_label.position = Vector2(16, 12)
	_label.add_theme_font_size_override("font_size", 18)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 4)
	layer.add_child(_label)


func _show(index: int) -> void:
	_index = posmod(index, _views.size())
	if _content != null:
		_content.queue_free()
	_content = Node3D.new()
	add_child(_content)
	_players.clear()
	var detail: String = _views[_index]["build"].call()
	_frame()
	_label.text = "%s\n%s\n\n[%d/%d]  Tab: next view   drag: orbit   wheel: zoom   space: pause" % [
		_views[_index]["title"], detail, _index + 1, _views.size()]


func _instance(asset: String) -> Node3D:
	var packed := load("res://%s.glb" % asset) as PackedScene
	var node := packed.instantiate() as Node3D
	_content.add_child(node)
	return node


# Blender (x, y, z) is glTF and Godot (x, z, -y).
func _godot(v: Array) -> Vector3:
	return Vector3(v[0], v[2], -v[1])


func _animate(node: Node3D, spec: Dictionary, state: Dictionary) -> String:
	var lines := PackedStringArray()
	var players := node.find_children("*", "AnimationPlayer", true, false)
	var animations: Dictionary = spec.get("animations", {})
	# One player per animation, so that a module's animations run together.
	var first := true
	for key in animations:
		var animation: Dictionary = animations[key]
		var binding: Dictionary = animation.get("binding", {"state": "rotor_speed_rpm", "kind": "speed_rpm"})
		var value: float = state.get(binding["state"], 0.0)
		var player: AnimationPlayer
		if first and players.size() > 0:
			player = players[0]
		else:
			player = (players[0] as AnimationPlayer).duplicate() as AnimationPlayer
			players[0].get_parent().add_child(player)
		first = false
		player.play(animation["action"])
		if binding["kind"] == "position":
			player.seek(clampf(value, 0.0, 1.0) * float(animation["cycle_s"]), true)
			player.pause()
		else:
			player.get_animation(animation["action"]).loop_mode = Animation.LOOP_LINEAR
			player.speed_scale = (value / 60.0) / (float(animation["turns_per_cycle"]) / float(animation["cycle_s"]))
			_players.append(player)
		lines.append("%s: %s = %s" % [key, binding["state"], value])
	# The heat tint, from the temperature.
	if state.has("temperature_K") and spec["materials"].has("heat"):
		var glow := clampf((state["temperature_K"] - 300.0) / 100.0, 0.0, 1.0)
		for mesh_instance in node.find_children("*", "MeshInstance3D", true, false):
			var mesh := (mesh_instance as MeshInstance3D).mesh
			for surface in mesh.get_surface_count():
				var material := mesh.surface_get_material(surface) as StandardMaterial3D
				if material != null and material.resource_name == spec["materials"]["heat"]:
					var tinted := material.duplicate() as StandardMaterial3D
					tinted.emission_enabled = true
					tinted.emission_energy_multiplier = glow * 2.0
					(mesh_instance as MeshInstance3D).set_surface_override_material(surface, tinted)
		lines.append("temperature %.0f K (glow %.2f)" % [state["temperature_K"], glow])
	return ", ".join(lines)


func _build_plant(catalogue: Dictionary) -> String:
	var layout: Dictionary = catalogue["layout"]
	var plant_state: Dictionary = catalogue["plant_state"]
	for module in layout["modules"]:
		var node := _instance(module["asset"])
		node.position = _godot(module["position_m"])
		node.rotation = Vector3(0.0, deg_to_rad(float(module["yaw_deg"])), 0.0)
		_animate(node, catalogue["specifications"][module["specification"]], plant_state.get(module["id"], {}))
	for route in layout["routes"]:
		if route["medium"] == "shaft":
			continue
		var material := StandardMaterial3D.new()
		material.albedo_color = PIPE_COLOURS.get(route["medium"], Color.GRAY)
		material.roughness = 0.5
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
			mesh.material = material
			var leg := MeshInstance3D.new()
			leg.mesh = mesh
			_content.add_child(leg)
			leg.global_transform = Transform3D(Basis(Quaternion(Vector3.UP, (b - a).normalized())), (a + b) / 2.0)
	return "%d modules, %d routes (pipes grey, cables orange); a mock PlanetSim state frame drives them" % [
		layout["modules"].size(), layout["routes"].size()]


func _build_single(asset: Dictionary, catalogue: Dictionary) -> String:
	var node := _instance(asset["id"])
	var detail: String = asset.get("note", "")
	if asset.has("specification"):
		var spec: Dictionary = catalogue["specifications"][asset["specification"]]
		var state: Dictionary = catalogue["asset_states"].get(asset["id"], {})
		var animated := _animate(node, spec, state)
		var ports := PackedStringArray()
		for port in spec.get("ports", []):
			ports.append("%s (%s)" % [port["id"], port["type"]])
		detail = "ports: %s\n%s" % [", ".join(ports) if ports.size() > 0 else "none", animated]
	return detail


func _frame() -> void:
	var bounds := AABB()
	var first := true
	for mesh_instance in _content.find_children("*", "MeshInstance3D", true, false):
		var instance := mesh_instance as MeshInstance3D
		if instance.name.ends_with("-convcolonly"):
			instance.visible = false
			continue
		var box := instance.global_transform * instance.get_aabb()
		bounds = box if first else bounds.merge(box)
		first = false
	_target = bounds.get_center()
	_distance = maxf(bounds.size.length() * 0.95, 2.0)
	_update_camera()


func _update_camera() -> void:
	var offset := Vector3(cos(_pitch) * sin(_yaw), sin(_pitch), cos(_pitch) * cos(_yaw)) * _distance
	_camera.look_at_from_position(_target + offset, _target, Vector3.UP)


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_TAB:
			_show(_index + (-1 if event.shift_pressed else 1))
		elif event.keycode == KEY_SPACE:
			_paused = not _paused
			for player in _players:
				if _paused:
					player.pause()
				else:
					player.play()
	elif event is InputEventMouseMotion and (event.button_mask & MOUSE_BUTTON_MASK_LEFT):
		_yaw -= event.relative.x * 0.008
		_pitch = clampf(_pitch + event.relative.y * 0.008, 0.05, 1.45)
		_update_camera()
	elif event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_distance = maxf(_distance * 0.9, 1.0)
			_update_camera()
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_distance *= 1.1
			_update_camera()
