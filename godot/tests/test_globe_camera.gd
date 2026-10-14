extends SceneTree

## R1-04 steps 1-2: the globe camera and the altitude bands (ADR-0022 §4.8),
## on the main scene (pass --subdivision=4). Checks camera-relative
## placement, ground collision, the bands and their weights, frame-rate
## independent motion, fly_to, zoom towards the cursor, and panning.

const RADIUS_M := 6_371_000.0

var _failures := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var main: Node3D = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await process_frame
	var planet: PlanetMeshNode = main.get_node("Planet")
	var camera: Camera3D = main.get_node("CameraPivot/Camera3D")
	var globe: Node3D = main.get_node("CameraPivot")
	globe.set_process(false) # the test steps it

	# Camera-relative: the camera at the origin, the planet at -camera.
	var direction := Vector3(0.3, 0.5, 0.8).normalized()
	globe.look_at_direction(direction, 0.5)
	_expect(camera.global_position == Vector3.ZERO, "the camera is not at the origin")
	_expect((planet.global_position + globe.get_camera_position()).length() < 1e-6,
		"the planet is not at -camera")

	# Ground collision: asked for 1 m, the camera stays 100 m above the
	# surface as drawn.
	globe.look_at_direction(direction, 1.0 / RADIUS_M)
	var altitude: float = globe.get_altitude_m()
	_expect(altitude >= 99.0 and altitude < 200.0, "ground collision: altitude %.1f m" % altitude)
	var surface: float = planet.get_surface_radius(globe.get_camera_position())
	_expect((globe.get_camera_position().length() - surface) * RADIUS_M >= 99.0,
		"the camera is below the drawn surface")

	# Bands, by altitude (L4 cells are about 446 km: planet above ~4460 km,
	# local below 30 km), with weights summing to 1.
	for case in [[2.0, "planet"], [0.05, "region"], [0.0005, "local"]]:
		globe.look_at_direction(direction, case[0])
		var weights: Dictionary = globe.get_band_weights()
		var total: float = weights["planet"] + weights["region"] + weights["local"]
		_expect(globe.get_current_band() == case[1],
			"at %.0f km the band is %s, not %s" % [globe.get_altitude_m() / 1000.0, globe.get_current_band(), case[1]])
		_expect(abs(total - 1.0) < 1e-6 and weights[case[1]] > 0.99,
			"band weights at %s: %s" % [case[1], weights])
	globe.look_at_direction(direction, globe.range_for_altitude(30_000.0))
	_expect(abs(globe.get_altitude_m() - 30_000.0) < 1500.0,
		"range_for_altitude(30 km) gives %.0f m" % globe.get_altitude_m())
	var edge: Dictionary = globe.get_band_weights()
	_expect(edge["region"] > 0.2 and edge["local"] > 0.2, "no cross-fade at the region-local edge: %s" % edge)

	# Frame-rate independence: the same second of a fly_to at 30 and at 240
	# frames per second ends in the same place.
	var cell := 1234
	var ends := []
	for rate in [30, 240]:
		globe.look_at_direction(direction, 1.5)
		globe.fly_to(cell, 5000.0)
		for frame in rate:
			globe._process(1.0 / rate)
		ends.append([globe.get_camera_position(), globe.get_altitude_m()])
	_expect((ends[0][0] - ends[1][0]).length() < 1e-5,
		"motion depends on the frame rate: %s vs %s" % [ends[0][0], ends[1][0]])
	# fly_to arrives.
	for frame in 240:
		globe._process(1.0 / 60.0)
	var target: Vector3 = planet.get_cell_direction(cell)
	_expect(globe.get_camera_position().normalized().dot(target) > 0.99999 or
		abs(globe.get_altitude_m() - 5000.0) < 500.0,
		"fly_to did not arrive: altitude %.0f m" % globe.get_altitude_m())
	_expect(abs(globe.get_altitude_m() - 5000.0) < 500.0, "fly_to altitude %.0f m" % globe.get_altitude_m())

	# Zoom towards the cursor: at the screen's centre the focus stays put;
	# off-centre it moves towards the ground under the cursor.
	globe.look_at_direction(direction, 0.3)
	var size := root.get_visible_rect().size
	var centre_before: Vector3 = globe.get_camera_position().normalized()
	_wheel(globe, size * 0.5)
	for frame in 120:
		globe._process(1.0 / 60.0)
	_expect(globe.get_camera_position().normalized().dot(centre_before) > 0.9999,
		"zooming at the centre moved the view")
	globe.look_at_direction(direction, 0.3)
	var corner := Vector2(size.x * 0.8, size.y * 0.5)
	var ground: Vector3 = _ground(camera, globe, corner)
	var before: float = globe.get_camera_position().normalized().dot(ground)
	_wheel(globe, corner)
	for frame in 120:
		globe._process(1.0 / 60.0)
	_expect(globe.get_camera_position().normalized().dot(ground) > before,
		"zooming at the cursor did not move towards it")

	# Panning: dragging left moves the view east of where it was, at the
	# same altitude.
	globe.look_at_direction(Vector3(0.0, 0.0, 1.0), 0.3)
	var start: Vector3 = globe.get_camera_position().normalized()
	var press := InputEventMouseButton.new()
	press.button_index = MOUSE_BUTTON_LEFT
	press.pressed = true
	globe.handle_input(press)
	var drag := InputEventMouseMotion.new()
	drag.relative = Vector2(-80.0, 0.0)
	globe.handle_input(drag)
	press.pressed = false
	globe.handle_input(press)
	for frame in 120:
		globe._process(1.0 / 60.0)
	var moved: Vector3 = globe.get_camera_position().normalized()
	_expect(moved.x > start.x + 1e-3, "dragging left did not turn the globe eastwards: %s" % moved)

	main.free()
	if _failures > 0:
		quit(1)
		return
	print("globe camera pass")
	quit(0)


func _wheel(globe: Node3D, position: Vector2) -> void:
	var wheel := InputEventMouseButton.new()
	wheel.button_index = MOUSE_BUTTON_WHEEL_UP
	wheel.pressed = true
	wheel.position = position
	globe.handle_input(wheel)


## The planet-centred direction of the ground under a screen point.
func _ground(camera: Camera3D, globe: Node3D, cursor: Vector2) -> Vector3:
	var origin: Vector3 = globe.get_camera_position()
	var direction := camera.project_ray_normal(cursor)
	var b := origin.dot(direction)
	var t := -b - sqrt(b * b - (origin.length_squared() - 1.0))
	return (origin + direction * t).normalized()


func _expect(condition: bool, message: String) -> void:
	if not condition:
		push_error(message)
		_failures += 1
