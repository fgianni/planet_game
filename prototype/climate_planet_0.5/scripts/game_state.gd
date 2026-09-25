class_name GameState
extends RefCounted
## The game: you govern one region of the planet. Buildings sit on climate-model cells, so what you
## build changes the climate, and the climate decides what your land can produce.
## Pure logic, no nodes: testable headless, and every game is reproducible from its seed + actions.

const YEARS_PER_TURN := 5
const START_YEAR := 2025
const MAX_TURNS := 25

const BUILD := {
	"farm":     { "name": "Industrial farm", "icon": "🌾", "cost": 20, "upkeep": 1, "food": 11, "energy": -1, "co2": 0.8, "veg": 0.3, "heat": 3.0, "jobs": 1,
				  "desc": "Big harvests. Strips the land, so less moisture returns to the air." },
	"agro":     { "name": "Agroecological farm", "icon": "🥕", "cost": 25, "upkeep": 1, "food": 7, "co2": 0.1, "veg": 0.95, "heat": 0.5,
				  "desc": "Smaller harvests, living soil, keeps the local water cycle going." },
	"coal":     { "name": "Coal plant", "icon": "🏭", "cost": 30, "upkeep": 2, "energy": 16, "co2": 4.0, "heat": 9.0, "happy": -1.5, "jobs": 3, "fossil": true,
				  "desc": "Cheap, reliable power. The heaviest emitter you can build." },
	"gas":      { "name": "Gas plant", "icon": "🔥", "cost": 35, "upkeep": 3, "energy": 10, "co2": 2.0, "heat": 5.0, "jobs": 1, "fossil": true,
				  "desc": "Half the emissions of coal. Steady whatever the weather." },
	"wind":     { "name": "Wind farm", "icon": "🌬️", "cost": 35, "upkeep": 1, "energy": 7, "wind_power": true,
				  "desc": "Output depends on the local winds — and the winds change with the climate." },
	"solar":    { "name": "Solar farm", "icon": "☀️", "cost": 42, "upkeep": 1, "energy": 7, "sun_power": true,
				  "desc": "Output depends on sunlight, so clouds and haze matter." },
	"house":    { "name": "Housing", "icon": "🏠", "cost": 15, "cap": 15, "energy": -2.5, "co2": 0.4, "veg": 0.45, "heat": 6.0,
				  "desc": "Homes for 15k people." },
	"factory":  { "name": "Factory", "icon": "⚙️", "cost": 35, "upkeep": 1, "money": 10, "energy": -4, "co2": 1.5, "veg": 0.4, "heat": 8.0, "jobs": 2,
				  "desc": "The backbone of your economy. Hungry for energy." },
	"forest":   { "name": "Plant forest", "icon": "🌱", "cost": 15, "veg": 1.25, "co2": -0.3,
				  "desc": "Takes 15 years to grow. Stores carbon and feeds rain downwind." },
	"reserve":  { "name": "Nature reserve", "icon": "🦋", "cost": 12, "upkeep": 1, "veg": 1.15, "happy": 1,
				  "desc": "Protects what is already there. Wildlife and wellbeing." },
}

var model: ClimateModel
var region := PackedInt32Array()          # cells the player governs
var buildings := {}                        # cell -> { type, built, age }
var pop := 40.0
var money := 110.0
var happy := 60.0
var aware := 25.0
var turn := 0
var year := START_YEAR
var over := false
var result := {}
var last := {}                             # last turn's resolved numbers
var log: Array = []
var papers: Array = []
var actions: Array = []
var turns_log: Array = []
var base_rain := {}                        # cell -> rain at the start, to spot local drying
var world_co2_per_turn := 4.5              # the rest of the world, ppm per turn
var seed_ := 0
var sea_rise := 0.0                        # planet radius units, from deep-ocean warming
var deep_base := 0.0
var rng := RandomNumberGenerator.new()
var shadow: ClimateModel                   # the same planet where your region never industrialised


func setup(m: ClimateModel, game_seed: int) -> void:
	model = m
	seed_ = game_seed
	deep_base = m.T_deep
	rng.seed = game_seed
	# Pick a good homeland: score each candidate by what surrounds it (land, low, ice-free,
	# watered, temperate), not just by the centre cell.
	var best := -1
	var best_score := -1e9
	for i in m.n:
		if m.land[i] == 0 or m.ice[i] > 0.2 or m.elev[i] > 900.0 or m.T[i] < 4.0 or m.T[i] > 24.0:
			continue
		var la := absf(rad_to_deg(m.lat[i]))
		if la < 18.0 or la > 52.0:
			continue
		var land_n := 0
		var total := 0
		var rain := 0.0
		var elev := 0.0
		var ice := 0.0
		for j in m.n:
			if m.pos[j].angle_to(m.pos[i]) < 0.3:
				total += 1
				ice += m.ice[j]
				if m.land[j] == 1:
					land_n += 1
					rain += m.P[j]
					elev += m.elev[j]
		if land_n < 14:
			continue                                  # need a real piece of land, not a few islands
		var score := float(land_n) / total * 60.0 + minf(rain / land_n, 1400.0) * 0.012 			- elev / land_n * 0.004 - ice / total * 40.0 - absf(m.T[i] - 18.0) * 0.8 + rng.randf()
		if score > best_score:
			best_score = score
			best = i
	var centre: Vector3 = m.pos[best]
	# grow the region outwards over land, keeping the coastal sea cells that touch it
	var frontier: Array[int] = [best]
	var seen := { best: true }
	var land_cells: Array[int] = []
	while not frontier.is_empty() and land_cells.size() < 26:
		var cur: int = frontier.pop_front()
		if m.land[cur] == 1 and m.ice[cur] < 0.4 and m.elev[cur] < 1600.0:
			land_cells.append(cur)
			for k in range(m.nb_start[cur], m.nb_start[cur + 1]):
				var j: int = m.nb[k]
				if not seen.has(j) and m.pos[j].angle_to(centre) < 0.42:
					seen[j] = true
					frontier.append(j)
	for c in land_cells:
		region.append(c)
		base_rain[c] = m.P[c]
	var coast := 0
	for c in land_cells:
		for k in range(m.nb_start[c], m.nb_start[c + 1]):
			var j: int = m.nb[k]
			if m.land[j] == 0 and region.find(j) < 0 and coast < 12:
				region.append(j)
				base_rain[j] = m.P[j]
				coast += 1
	var tsum := 0.0
	var rsum := 0.0
	for cell in region:
		tsum += m.T[cell]
		rsum += m.P[cell]
	_base_temp = tsum / region.size()
	recent_temp = _base_temp
	recent_rain = rsum / region.size()
	for t in ["coal", "farm", "farm", "house", "house", "house", "factory"]:
		var cell := _free_land_cell(rng)
		if cell >= 0:
			_raw_place(cell, t)
	_apply_land_use()
	_note("Your region takes shape: coal power, two big farms, a factory.", "info")
	papers.append({ "year": year, "head": "A new government takes office", "deck": "It inherits fields, factories and a coal plant — and a climate that has already warmed.", "also": [] })


func _cell_radius(cell: int) -> float:
	return ClimateModel.SEA_LEVEL_R + model.elev[cell] / ClimateModel.ELEV_SCALE


var _base_temp := 0.0
var recent_temp := 0.0                     # what people are used to: events are judged against this
var recent_rain := 0.0

func _in_north() -> bool:
	for cell in region:
		if rad_to_deg(model.lat[cell]) > 35.0:
			return true
	return false


## warmest sea within reach of the region — hurricanes need about 27 °C
func _warm_ocean_near() -> float:
	var best := 0.0
	var centre: Vector3 = model.pos[region[0]]
	for i in model.n:
		if model.land[i] == 0 and model.pos[i].angle_to(centre) < 0.7:
			best = maxf(best, model.T[i])
	return best


func _free_land_cell(rng: RandomNumberGenerator) -> int:
	for attempt in 500:
		var c: int = region[rng.randi() % region.size()]
		if model.land[c] == 1 and not buildings.has(c):
			return c
	return -1


# ---------- building ----------
func can_place(cell: int, type: String) -> String:
	if over:
		return "The game is over"
	if not BUILD.has(type):
		return "Unknown building"
	if region.find(cell) < 0:
		return "Outside your region"
	if model.land[cell] == 0:
		return "That is open sea"
	if buildings.has(cell):
		return "Already built on"
	if model.ice[cell] > 0.4:
		return "Under ice"
	if model.elev[cell] > 1600.0:
		return "Too high and steep"
	if money < BUILD[type]["cost"]:
		return "Needs %d credits" % BUILD[type]["cost"]
	return ""


func place(cell: int, type: String) -> String:
	var err := can_place(cell, type)
	if err != "":
		return err
	money -= BUILD[type]["cost"]
	actions.append({ "t": turn, "a": "build", "k": type, "i": cell })
	_raw_place(cell, type)
	_apply_land_use()
	_note("Built %s." % BUILD[type]["name"].to_lower(), "build")
	return ""


func _raw_place(cell: int, type: String) -> void:
	buildings[cell] = { "type": type, "built": year, "age": 0 }


func demolish(cell: int) -> String:
	if not buildings.has(cell) or over:
		return "Nothing to remove"
	if money < 5.0:
		return "Needs 5 credits"
	money -= 5.0
	var d: Dictionary = BUILD[buildings[cell]["type"]]
	actions.append({ "t": turn, "a": "remove", "k": buildings[cell]["type"], "i": cell })
	buildings.erase(cell)
	_apply_land_use()
	if d.get("jobs", 0) > 0:
		happy = clampf(happy - d["jobs"] * 2.0, 0.0, 100.0)
		_note("%s closed. Jobs lost." % d["name"], "harm")
	else:
		_note("%s removed." % d["name"], "info")
	return ""


## Land use feeds straight into the climate model, and these effects are local and fast:
## cleared ground returns less moisture to the air (less rain downwind), while cities, industry and
## bare fields warm their own ground (the urban heat island).
func _apply_land_use() -> void:
	for cell in region:
		var v := 1.0
		var heat := 0.0
		if buildings.has(cell):
			var d: Dictionary = BUILD[buildings[cell]["type"]]
			v = d.get("veg", 0.7)
			heat = d.get("heat", 0.0)
			if buildings[cell]["type"] == "forest":
				v = lerpf(0.8, 1.25, clampf(buildings[cell]["age"] / 3.0, 0.0, 1.0))
		model.veg_use[cell] = v
		model.local_forcing[cell] = heat


# ---------- local climate → production ----------
func rain_factor(cell: int) -> float:
	var r: float = model.P[cell]
	if r < 250.0:
		return clampf(r / 250.0, 0.1, 1.0) * 0.5
	return clampf(0.45 + r / 1100.0, 0.3, 1.4)


func heat_factor(cell: int) -> float:
	return clampf(1.0 - 0.07 * maxf(0.0, model.T[cell] - 25.0), 0.35, 1.0)


func wind_factor(cell: int) -> float:
	return clampf(Vector2(model.u[cell], model.v[cell]).length() / 7.5, 0.25, 1.5)


func sun_factor(cell: int) -> float:
	return clampf(model.insol[cell] / 340.0 * (1.0 - 0.55 * model.cloud[cell]) * 1.5, 0.3, 1.4)


func flows() -> Dictionary:
	var f := { "food": 0.0, "energy_in": 0.0, "energy_out": 0.0, "money": 0.0, "upkeep": 0.0, "cap": 0.0, "co2": 0.0, "happy": 0.0, "jobs": 0.0 }
	for cell in buildings:
		var b: Dictionary = buildings[cell]
		var d: Dictionary = BUILD[b["type"]]
		var food: float = d.get("food", 0.0)
		if food > 0.0:
			food *= rain_factor(cell) * heat_factor(cell)
		f["food"] += food
		var e: float = d.get("energy", 0.0)
		if d.get("wind_power", false):
			e *= wind_factor(cell)
		elif d.get("sun_power", false):
			e *= sun_factor(cell)
		if e > 0.0:
			f["energy_in"] += e
		else:
			f["energy_out"] -= e
		f["money"] += d.get("money", 0.0)
		f["upkeep"] += d.get("upkeep", 0.0)
		f["cap"] += d.get("cap", 0.0)
		f["co2"] += d.get("co2", 0.0)
		f["happy"] += d.get("happy", 0.0)
	f["energy_out"] += pop * 0.05
	f["food_need"] = pop / 4.0
	f["tax"] = pop * 0.4
	return f


## ppm added to the whole planet this turn: your region plus the rest of the world, which follows
## your example (a deliberate simplification — it keeps one region's choices meaningful).
func emissions_ppm() -> float:
	var f := flows()
	return maxf(0.0, f["co2"] * 0.55 + world_ppm())


# ---------- the turn ----------
## Called after the climate model has advanced 5 years.
func resolve_turn() -> Dictionary:
	var f := flows()
	var ev := { "drought": false, "heat": false, "shortage": 0.0, "blackout": 0.0, "good": false, "storm": "", "flood": "", "cold": false }
	var rain_now := 0.0
	var rain_base := 0.0
	var t_now := 0.0
	for cell in region:
		rain_now += model.P[cell]
		rain_base += base_rain[cell]
		t_now += model.T[cell]
	rain_now /= region.size()
	rain_base /= region.size()
	t_now /= region.size()
	var warming_here: float = t_now - _base_temp
	# Events are anomalies against what people are used to (a rolling memory of recent decades), not
	# against the preindustrial world: a slowly drying climate shows up in yields, not as news.
	if rain_now < recent_rain * 0.85:
		ev["drought"] = true
	if t_now > recent_temp + 0.8:
		ev["heat"] = true
	if model.amoc_strength < 0.5 and _in_north():
		ev["cold"] = true
	var shock := 0.0
	# --- what the climate does to your region this turn ---
	var harvest := 1.0
	if ev["drought"]:
		harvest *= 0.6
		shock -= 3.0
		var fp := fingerprint()
		var blame := ""
		if fp.y < -8.0:
			blame = " Your own emissions and cleared land account for about %d mm/yr of the rain you have lost." % int(-fp.y)
		_note("Drought: the harvest is down by 40%%.%s" % blame, "event")
	if ev["heat"]:
		var lost: float = pop * 0.01 * clampf(warming_here, 0.5, 3.0)
		pop -= lost
		shock -= 3.0 + minf(warming_here, 4.0)
		harvest *= 0.9
		var fph := fingerprint()
		var blame_h := ""
		if fph.x > 0.05:
			blame_h = " Your region is %.1f °C hotter than it would have been without your own choices." % fph.x
		_note("Heatwave: %dk people affected, work stopped in the hottest weeks.%s" % [int(lost), blame_h], "event")
	if ev["cold"]:
		harvest *= 0.75
		shock -= 4.0
		_note("The seas have turned cold: shorter growing season, poor harvests.", "event")
	# rising seas and hurricanes destroy what stands in their way
	sea_rise = clampf(0.004 * (model.T_deep - deep_base), 0.0, 0.03)
	for cell in buildings.keys():
		if model.land[cell] == 1 and _cell_radius(cell) < ClimateModel.SEA_LEVEL_R + sea_rise:
			ev["flood"] = BUILD[buildings[cell]["type"]]["name"]
			buildings.erase(cell)
			shock -= 5.0
			_note("The sea swallowed a %s on the coast." % ev["flood"].to_lower(), "event")
			break
	var storm_risk: float = clampf((_warm_ocean_near() - 27.0) * 0.18, 0.0, 0.7)
	if rng.randf() < storm_risk and buildings.size() > 3:
		var keys := buildings.keys()
		var victim: int = keys[rng.randi() % keys.size()]
		ev["storm"] = BUILD[buildings[victim]["type"]]["name"]
		buildings.erase(victim)
		shock -= 4.0
		_note("A hurricane came ashore and destroyed a %s." % ev["storm"].to_lower(), "event")
	f["food"] *= harvest
	# food
	if f["food"] < f["food_need"] - 0.5:
		var loss: float = minf(pop * 0.3, (f["food_need"] - f["food"]) * 2.5)
		pop -= loss
		shock -= 10.0
		ev["shortage"] = loss
		_note("Food shortage: %dk people went hungry or left." % int(loss), "event")
	elif happy > 40.0 and pop < f["cap"]:
		pop = minf(f["cap"], minf(pop + pop * 0.08 + 1.0, pop + (f["food"] - f["food_need"]) * 3.0))
		if f["food"] > f["food_need"] * 1.4:
			ev["good"] = true
	if pop > f["cap"]:
		shock -= minf(8.0, (pop - f["cap"]) * 0.5)
	# energy
	var deficit: float = f["energy_out"] - f["energy_in"]
	var money_mul := 1.0
	if deficit > 0.0:
		shock -= minf(15.0, deficit * 1.5)
		money_mul = clampf(1.0 - deficit / maxf(1.0, f["energy_out"]), 0.3, 1.0)
		ev["blackout"] = deficit
		_note("Blackouts: %.1f short of the energy you need." % deficit, "event")
	# money
	money += f["tax"] + f["money"] * money_mul - f["upkeep"]
	if money < 0.0:
		shock -= 5.0
		_note("The treasury is in debt.", "event")
	# climate concern
	var felt := 0.0
	if ev["drought"]: felt += 1.0
	if ev["storm"] != "" or ev["flood"] != "": felt += 1.0
	if ev["heat"]: felt += 1.0
	if ev["shortage"] > 0.0: felt += 1.0
	if model.amoc_strength < 0.6: felt += 1.0
	aware = clampf(aware + 5.0 * felt + 1.5 - 0.1 * (aware - 20.0) - (3.0 if deficit > 0.0 else 0.0), 0.0, 100.0)
	# mood
	var warming: float = model.global_T - model.ref_global_T
	var target: float = 60.0 + f["happy"] - maxf(0.0, warming - 1.5) * 4.0
	if aware > 50.0:
		target += 3.0 if f["co2"] < 2.0 else -((aware - 50.0) / 10.0) * minf(1.0, f["co2"] / 5.0) * 2.0
	happy = clampf(happy + 0.3 * (target - happy) + shock, 0.0, 100.0)
	pop = maxf(0.0, pop)
	for cell in buildings:
		buildings[cell]["age"] += 1
	_apply_land_use()
	recent_temp += (t_now - recent_temp) * 0.35          # people adapt to the new normal
	recent_rain += (rain_now - recent_rain) * 0.35
	last = { "flows": f, "events": ev, "rain": rain_now, "rain_base": rain_base, "temp": t_now }
	_front_page(ev, rain_now, rain_base)
	turn += 1
	year += YEARS_PER_TURN
	turns_log.append({ "y": year, "pop": roundf(pop * 10.0) / 10.0, "happy": roundf(happy), "aware": roundf(aware), "money": roundf(money),
		"co2": roundf(model.co2), "temp": roundf(model.global_T * 100.0) / 100.0, "rain": roundf(rain_now), "amoc": roundf(model.amoc_strength * 100.0) })
	if pop < 10.0:
		_finish("collapse", "Your region emptied out.")
	elif happy < 12.0:
		_finish("revolt", "Unrest swept the government away.")
	elif turn >= MAX_TURNS:
		_finish("end", "You reached %d." % year)
	return ev


func _finish(kind: String, text: String) -> void:
	over = true
	var eco: float = 0.0
	for cell in region:
		eco += model.veg[cell]
	eco = eco / region.size() * 100.0
	var warming: float = model.global_T - model.ref_global_T
	var score := 0
	if kind == "end":
		score = int(pop * 1.5 + happy + eco - maxf(0.0, warming - 1.5) * 40.0 - (30.0 if model.amoc_strength < 0.5 else 0.0))
	result = { "kind": kind, "text": text, "score": score, "warming": warming }
	_note(text, "tip")


func _note(text: String, kind := "info") -> void:
	log.push_front({ "year": year, "text": text, "kind": kind })


func _front_page(ev: Dictionary, rain_now: float, rain_base: float) -> void:
	var t := 0 if aware < 35.0 else (1 if aware < 65.0 else 2)
	var head := ""
	var deck := ""
	var also: Array = []
	if model.amoc_strength < 0.4:
		head = ["Strange cold in the northern seas", "Ocean current falters: northern seas cooling", "The great ocean current is failing"][t]
		deck = "Fishermen report colder water and stranger weather. Scientists say the circulation that carries warmth north is weakening."
	elif ev["storm"] != "":
		head = ["Hurricane wrecks a %s" % ev["storm"].to_lower(), "Another hurricane: a %s destroyed" % ev["storm"].to_lower(), "Hurricanes are the new normal: a %s lost" % ev["storm"].to_lower()][t]
		deck = "The storm formed over unusually warm water offshore."
	elif ev["flood"] != "":
		head = ["Coastal %s lost to the waves" % ev["flood"].to_lower(), "The sea takes a %s" % ev["flood"].to_lower(), "The coastline is retreating and will not stop"][t]
		deck = "Surveyors have redrawn the coast; insurers are pulling out."
	elif ev["cold"] != false and ev["cold"]:
		head = ["A strange chill settles on the region", "Cooling seas: the current that warms us is failing", "The ocean current has stalled — and we are colder for it"][t]
		deck = "The planet is warmer than ever, yet our growing season is shrinking."
	elif ev["shortage"] > 0.0:
		head = ["Empty shelves as harvests fail", "Hunger returns: %dk people affected" % int(ev["shortage"]), "Climate hunger reaches our own fields"][t]
		deck = "Farm groups blame %s." % ("bad luck and poor planning" if t == 0 else "a drier, hotter climate")
	elif ev["drought"]:
		head = ["Dry spell worries farmers", "Drought: rain down %d%% on the old average" % int(100.0 * (1.0 - rain_now / maxf(rain_base, 1.0))), "The rains are failing us"][t]
		deck = "Wells are low and yields are down across the region."
	elif ev["blackout"] > 0.0:
		head = ["Power cuts anger businesses", "Blackouts as demand outruns supply", "Blackouts: the energy plan is failing"][t]
		deck = "Industry wants more capacity; engineers want it clean."
	elif ev["heat"]:
		head = ["A summer too hot to work in", "Heat records fall again", "Heat emergency declared"][t]
		deck = "Doctors warn about the hottest hours of the day."
	elif ev["good"]:
		head = ["Full granaries and steady jobs", "A good decade for the region", "Prosperity, with an eye on the sky"][t]
		deck = "Harvests are strong and the lights stay on."
	else:
		head = ["Quiet years in the region", "The region waits for direction", "Voters ask what the plan is"][t]
		deck = "Polls show %d%% now call the climate a serious threat." % int(aware)
	if model.global_T - model.ref_global_T > 2.0:
		also.append("Global warming passes +%.1f °C" % (model.global_T - model.ref_global_T))
	if model.amoc_strength < 0.85:
		also.append("Ocean overturning down to %d%%" % int(100.0 * model.amoc_strength))
	papers.append({ "year": year + YEARS_PER_TURN, "head": head, "deck": deck, "also": also })


## Your fingerprint: the difference between the real planet and a shadow planet where your region
## never industrialised and the rest of the world stayed average (so it counts both your own
## emissions and the example you set for everyone else).
## [local warming °C, local rain change mm/yr, global warming °C]
func fingerprint() -> Vector3:
	if shadow == null:
		return Vector3.ZERO
	var dt := 0.0
	var dp := 0.0
	for cell in region:
		dt += model.T[cell] - shadow.T[cell]
		dp += model.P[cell] - shadow.P[cell]
	var c := float(region.size())
	return Vector3(dt / c, dp / c, model.global_T - shadow.global_T)


## ppm the rest of the world adds, with or without your example.
func world_ppm() -> float:
	var f := flows()
	var intensity: float = f["co2"] / maxf(pop, 1.0)
	return world_co2_per_turn * clampf(intensity / 0.2, 0.25, 1.8)


func export_log() -> Dictionary:
	var counts := {}
	for a in actions:
		if a["a"] == "build":
			counts[a["k"]] = counts.get(a["k"], 0) + 1
	return { "schema": 2, "version": "godot-1", "seed": seed_, "status": "finished" if over else "abandoned",
		"turnsPlayed": turn, "result": result, "builds": counts, "actions": actions, "turns": turns_log,
		"headlines": papers.map(func(p): return [p["year"], p["head"]]),
		"fingerprint": [fingerprint().x, fingerprint().y, fingerprint().z] }
