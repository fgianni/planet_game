extends Node3D

## The Earth-viewer camera of ADR-0022 §4.8 (task R1-04 steps 1-2).
##
## The view looks at a focus point on the globe from a range, with a heading
## and a tilt; the tilt follows altitude (straight down in orbit, towards the
## horizon near the ground) plus the player's own tilt. Drag turns the
## globe, the wheel zooms towards the point under the cursor, Shift-drag
## turns the heading and the tilt. Motion is smoothed frame-rate
## independently, and fly_to() moves the view to a cell.
##
## Rendering is camera-relative (ADR-0022 §4.8, amended): the state is kept
## in GDScript's 64-bit floats, the camera stays at the scene origin and the
## planet is placed at -camera every frame, so the GPU only sees offsets.
## Units are planet radii, as the planet node draws.

signal band_changed(band: String)

const RADIUS_M := 6_371_000.0
const MIN_ALTITUDE_M := 100.0 # above the surface as drawn
const MAX_RANGE := 12.0 # planet radii
const REGION_LOCAL_M := 30_000.0 # ADR-0022 §4.8: fixed in metres
const PLANET_REGION_CELLS := 10.0 # and in cell sizes
const BAND_BLEND_DECADES := 0.15 # half-width of each cross-fade, in log10 altitude
const ORBIT_TILT_FROM_CELLS := 10.0 # straight down above this altitude
const GROUND_TILT := deg_to_rad(72.0) # towards the horizon at the minimum altitude
const MAX_TILT := deg_to_rad(85.0)
const SMOOTHING := 10.0 # 1/s; the view closes this share of the gap per second, exponentially
const ZOOM_STEP := 0.85

var planet: Node3D # the planet node, moved to -camera each frame
var camera: Camera3D

# Target and current view, in double: focus as a unit vector, range in radii.
var _target := {"focus": [0.0, 0.0, 1.0], "range": 2.2, "heading": 0.0, "tilt": 0.0}
var _view := {"focus": [0.0, 0.0, 1.0], "range": 2.2, "heading": 0.0, "tilt": 0.0}
var _camera_position := [0.0, 0.0, 3.2] # double, planet-centred
var _altitude_m := 0.0
var _band := ""
var _band_weights := {"planet": 1.0, "region": 0.0, "local": 0.0}
var _dragging := false
var _cell_spacing_rad := 0.0175


## Points the view at a direction (planet-centred), from a range in radii,
## at once.
func look_at_direction(direction: Vector3, range_radii: float, heading: float = 0.0) -> void:
	_target["focus"] = _normalized([direction.x, direction.y, direction.z])
	_target["range"] = clampf(range_radii, _min_range(), MAX_RANGE)
	_target["heading"] = heading
	_target["tilt"] = 0.0
	_view = _target.duplicate(true)
	_apply()


## Flies the view to a cell, ending altitude_m above it, smoothly.
func fly_to(cell: int, altitude_m: float) -> void:
	var direction: Vector3 = planet.get_cell_direction(cell)
	_target["focus"] = _normalized([direction.x, direction.y, direction.z])
	_target["range"] = range_for_altitude(altitude_m)


## The range at which the camera, at the tilt it will then have, stands
## altitude_m above the focus.
func range_for_altitude(altitude_m: float) -> float:
	var height := maxf(altitude_m, MIN_ALTITUDE_M) / RADIUS_M
	var range_radii := height
	for step in 30:
		var tilt := clampf(_auto_tilt(range_radii) + _target["tilt"], 0.0, MAX_TILT)
		range_radii = height / maxf(cos(tilt), 0.05)
	return clampf(range_radii, _min_range(), MAX_RANGE)


func set_cell_spacing(spacing_rad: float) -> void:
	_cell_spacing_rad = spacing_rad


func get_altitude_m() -> float:
	return _altitude_m


## "planet", "region" or "local" (ADR-0022 §4.8), by altitude.
func get_current_band() -> String:
	return _band


## Each band's weight, summing to 1: across a band edge both are drawn.
func get_band_weights() -> Dictionary:
	return _band_weights


func get_camera_position() -> Vector3:
	return _vector(_camera_position)


func handle_input(event: InputEvent) -> bool:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT:
			_dragging = event.pressed
			return true
		if event.pressed and event.button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
			_zoom(ZOOM_STEP if event.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0 / ZOOM_STEP,
				event.position)
			return true
	elif event is InputEventMouseMotion and _dragging:
		if event.shift_pressed:
			_target["heading"] = wrapf(_target["heading"] - event.relative.x * 0.005, -PI, PI)
			_target["tilt"] = clampf(_target["tilt"] + event.relative.y * 0.004, -0.6, 0.9)
		else:
			_pan(event.relative)
		return true
	return false


func _process(delta: float) -> void:
	if planet == null or camera == null:
		return
	var keep := exp(-SMOOTHING * delta)
	_view["focus"] = _slerp(_target["focus"], _view["focus"], keep)
	_view["range"] = exp(lerp(log(_target["range"]), log(_view["range"]), keep))
	_view["heading"] = _target["heading"] + wrapf(_view["heading"] - _target["heading"], -PI, PI) * keep
	_view["tilt"] = lerp(_target["tilt"], _view["tilt"], keep)
	_apply()


## The camera pose from the current view; the planet moves to -camera.
func _apply() -> void:
	var up: Array = _view["focus"]
	var north := _normalized(_minus([0.0, 1.0, 0.0], _scale(up, up[1])))
	if _dot(north, north) < 1e-12:
		north = [0.0, 0.0, -1.0]
	var east := _cross(north, up)
	var heading: float = _view["heading"]
	var forward := _add(_scale(north, cos(heading)), _scale(east, sin(heading)))
	var surface: float = planet.get_surface_radius(_vector(up))
	var focus_point := _scale(up, surface)
	var range_radii: float = _view["range"]
	var tilt := clampf(_auto_tilt(range_radii) + _view["tilt"], 0.0, MAX_TILT)
	# Behind and above the focus, by the tilt.
	var offset := _add(_scale(up, cos(tilt) * range_radii), _scale(forward, -sin(tilt) * range_radii))
	_camera_position = _add(focus_point, offset)
	# Ground collision against the surface as drawn.
	var length := sqrt(_dot(_camera_position, _camera_position))
	var below: float = planet.get_surface_radius(_vector(_camera_position))
	var floor_radius := below + MIN_ALTITUDE_M / RADIUS_M
	if length < floor_radius:
		_camera_position = _scale(_camera_position, floor_radius / length)
		length = floor_radius
	_altitude_m = (length - below) * RADIUS_M
	# Look at the focus; at zero tilt the screen's top is the heading.
	var look := _normalized(_minus(focus_point, _camera_position))
	var screen_up := _normalized(_minus(forward, _scale(look, _dot(forward, look))))
	if tilt > 0.05:
		screen_up = _normalized(_minus(up, _scale(look, _dot(up, look))))
	var right := _cross(look, screen_up)
	camera.global_transform = Transform3D(
		Basis(_vector(right), _vector(screen_up), _vector(_scale(look, -1.0))), Vector3.ZERO)
	planet.global_position = _vector(_scale(_camera_position, -1.0))
	var altitude_radii := _altitude_m / RADIUS_M
	camera.near = maxf(altitude_radii * 0.005, 1.0e-9)
	camera.far = sqrt(maxf(length * length - 1.0, 0.0)) + 1.5
	_update_bands()
	# Below the planet band the streamed terrain tiles draw, from the
	# camera's double-precision position (R1-04 steps 3-4).
	if planet.has_method("update_terrain_tiles"):
		var height := maxf(get_viewport().get_visible_rect().size.y, 1.0)
		var focal := height * 0.5 / tan(deg_to_rad(camera.fov) * 0.5)
		var size := get_viewport().get_visible_rect().size
		var half_height := tan(deg_to_rad(camera.fov) * 0.5)
		var aspect := size.x / maxf(size.y, 1.0)
		var half_diagonal := atan(half_height * sqrt(1.0 + aspect * aspect))
		planet.update_terrain_tiles(PackedFloat64Array(_camera_position), PackedFloat64Array(look),
			focal, half_diagonal, _band_weights["planet"] < 0.5)


## 0 (straight down) above ORBIT_TILT_FROM_CELLS cell sizes, rising in
## log altitude to GROUND_TILT at the minimum altitude.
func _auto_tilt(range_radii: float) -> float:
	var high := log(ORBIT_TILT_FROM_CELLS * _cell_spacing_rad)
	var low := log(MIN_ALTITUDE_M / RADIUS_M)
	var t := clampf((high - log(range_radii)) / (high - low), 0.0, 1.0)
	return GROUND_TILT * t * t * (3.0 - 2.0 * t)


func _update_bands() -> void:
	var altitude := maxf(_altitude_m, 1.0)
	var planet_region := log(PLANET_REGION_CELLS * _cell_spacing_rad * RADIUS_M) / log(10.0)
	var region_local := log(REGION_LOCAL_M) / log(10.0)
	var a := log(altitude) / log(10.0)
	var planet_weight := _ramp(a, planet_region)
	var local_weight := 1.0 - _ramp(a, region_local)
	_band_weights = {"planet": planet_weight, "local": local_weight,
		"region": maxf(0.0, 1.0 - planet_weight - local_weight)}
	var band := "planet" if a >= planet_region else ("local" if a < region_local else "region")
	if band != _band:
		_band = band
		band_changed.emit(band)


func _ramp(a: float, edge: float) -> float:
	var t := clampf((a - edge) / (2.0 * BAND_BLEND_DECADES) + 0.5, 0.0, 1.0)
	return t * t * (3.0 - 2.0 * t)


func _pan(relative: Vector2) -> void:
	# The ground under the screen moves with the cursor.
	var radians_per_pixel: float = 2.0 * _view["range"] * tan(deg_to_rad(camera.fov) * 0.5) \
		/ maxf(get_viewport().get_visible_rect().size.y, 1.0)
	var up: Array = _target["focus"]
	var north := _normalized(_minus([0.0, 1.0, 0.0], _scale(up, up[1])))
	if _dot(north, north) < 1e-12:
		north = [0.0, 0.0, -1.0]
	var east := _cross(north, up)
	var heading: float = _target["heading"]
	var forward := _add(_scale(north, cos(heading)), _scale(east, sin(heading)))
	var right := _cross(forward, up)
	var moved := _add(_scale(right, -relative.x * radians_per_pixel), _scale(forward, relative.y * radians_per_pixel))
	_target["focus"] = _normalized(_add(up, moved))


func _zoom(factor: float, cursor: Vector2) -> void:
	var before: float = _target["range"]
	_target["range"] = clampf(before * factor, _min_range(), MAX_RANGE)
	var share: float = 1.0 - _target["range"] / before
	if share <= 0.0:
		return
	# Zooming in moves the focus towards the ground under the cursor.
	var hit = _ground_under(cursor)
	if hit != null:
		_target["focus"] = _slerp(hit, _target["focus"], 1.0 - share)


## The planet-centred unit direction of the ground under a screen point, or
## null off the globe.
func _ground_under(cursor: Vector2):
	var origin := _camera_position
	var normal := camera.project_ray_normal(cursor)
	var direction := [normal.x, normal.y, normal.z]
	var b := _dot(origin, direction)
	var c := _dot(origin, origin) - 1.0
	var discriminant := b * b - c
	if discriminant < 0.0:
		return null
	var t := -b - sqrt(discriminant)
	if t < 0.0:
		return null
	return _normalized(_add(origin, _scale(direction, t)))


func _min_range() -> float:
	return MIN_ALTITUDE_M / RADIUS_M


func _slerp(a: Array, b: Array, t: float) -> Array:
	# From a (t = 0) to b (t = 1) on the sphere.
	var cosine := clampf(_dot(a, b), -1.0, 1.0)
	var angle := acos(cosine)
	if angle < 1e-12:
		return b.duplicate()
	var s := sin(angle)
	return _normalized(_add(_scale(a, sin((1.0 - t) * angle) / s), _scale(b, sin(t * angle) / s)))


func _add(a: Array, b: Array) -> Array:
	return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]


func _minus(a: Array, b: Array) -> Array:
	return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


func _scale(a: Array, s: float) -> Array:
	return [a[0] * s, a[1] * s, a[2] * s]


func _dot(a: Array, b: Array) -> float:
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


func _cross(a: Array, b: Array) -> Array:
	return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


func _normalized(a: Array) -> Array:
	var length := sqrt(_dot(a, a))
	return [a[0] / length, a[1] / length, a[2] / length] if length > 0.0 else [0.0, 0.0, 1.0]


func _vector(a: Array) -> Vector3:
	return Vector3(a[0], a[1], a[2])
