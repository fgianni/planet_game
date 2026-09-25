class_name ClimateLook
extends RefCounted
## Maps a warming value t (0 = 2025, 1 = 2140) to everything the planet shows.
## Three keyframes match the concept board; values in between are interpolated.
## Later, the game simulation will drive these values instead of the slider.

const KEYS := [
	{ "t": 0.0, "year": 2025.0, "temp": 1.2,
	  "sea_rise": 0.0, "ice_lat": 0.80, "snow_height": 1.049, "desert": 0.12, "forest": 1.0,
	  "grass": Color("#72c64c"), "dry": Color("#e9c572"), "deep": Color("#1b5fb4"), "shallow": Color("#39a4ea"),
	  "glow": Color("#8fdcff"), "cloud": Color("#ffffff"), "cover": 0.6, "haze": 0.0, "haze_color": Color("#d9c874"),
	  "storms": 0, "fires": 0, "smoke": 0.35 },
	{ "t": 0.52, "year": 2085.0, "temp": 2.1,
	  "sea_rise": 0.005, "ice_lat": 0.87, "snow_height": 1.06, "desert": 0.35, "forest": 0.68,
	  "grass": Color("#a3bf4f"), "dry": Color("#e2bb62"), "deep": Color("#1d5f96"), "shallow": Color("#3aa0cf"),
	  "glow": Color("#d9ea9a"), "cloud": Color("#f4f0de"), "cover": 0.61, "haze": 0.08, "haze_color": Color("#d9c874"),
	  "storms": 1, "fires": 1, "smoke": 0.7 },
	{ "t": 1.0, "year": 2140.0, "temp": 3.4,
	  "sea_rise": 0.012, "ice_lat": 0.94, "snow_height": 1.2, "desert": 0.62, "forest": 0.37,
	  "grass": Color("#bda45a"), "dry": Color("#e0ae5a"), "deep": Color("#205b73"), "shallow": Color("#3d93a8"),
	  "glow": Color("#ffa65e"), "cloud": Color("#ecdcc0"), "cover": 0.63, "haze": 0.2, "haze_color": Color("#e8a060"),
	  "storms": 2, "fires": 3, "smoke": 1.0 },
]

static func at(t: float) -> Dictionary:
	t = clampf(t, 0.0, 1.0)
	var i := 0
	while i < KEYS.size() - 2 and t > KEYS[i + 1]["t"]:
		i += 1
	var a: Dictionary = KEYS[i]
	var b: Dictionary = KEYS[i + 1]
	var k := inverse_lerp(a["t"], b["t"], t)
	var out := {}
	for key in a.keys():
		var va = a[key]
		var vb = b[key]
		if va is Color:
			out[key] = (va as Color).lerp(vb, k)
		elif va is int:
			out[key] = va if k < 0.5 else vb      # counts switch half-way between keyframes
		else:
			out[key] = lerpf(va, vb, k)
	out["t"] = t
	return out
