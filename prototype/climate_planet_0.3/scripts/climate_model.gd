class_name ClimateModel
extends RefCounted
## Simplified climate model on the planet's icosphere grid (one cell per vertex).
##
## Temperature: energy balance per cell (Budyko–Sellers type). Sunlight depends on latitude and
##   on albedo (ice is bright, so melting ice warms: the ice–albedo feedback emerges by itself).
##   CO₂ reduces outgoing heat (5.35·ln(C/280) W/m²). Heat spreads to neighbours (diffusion) and
##   is carried by the wind (upwind advection). Ocean cells exchange heat with a slow deep ocean.
## Winds: three-cell circulation (trades, westerlies, polar easterlies); the Hadley edge moves
##   poleward as the planet warms. Weaker over land and mountains.
## Moisture: evaporation (more over warm ocean, Clausius–Clapeyron), carried by the wind, rains out
##   where air rises (equator, polar front, windward slopes) and less where it sinks (~30°, lee sides).
##
## Pure data, no nodes: deterministic, runs in a thread, testable headless.

const R_EARTH := 6.371e6
const SEC_YEAR := 3.156e7
const Q0 := 340.25            # mean top-of-atmosphere sunlight, W/m²
var A15 := 224.9              # heat loss to space (+ evaporation) at 15 °C, W/m²   } calibrated together:
var B_OLR := 2.05             # W/m²/K                                                  } 13.9 °C at 280 ppm,
var B_POLAR := 0.9            # weaker heat loss near the poles → polar amplification } +3.1 °C per doubling,
const B_OCEAN := 0.0          # extra evaporative cooling over the sea → land warms faster than ocean
const B_LAND := 0.0
const D_HEAT := 0.42          # meridional heat diffusion, W/m²/K
const ADV_HEAT := 1.2         # heat carried by wind, W/m²/K per m/s
const GAMMA_DEEP := 0.7       # mixed layer ↔ deep ocean exchange, W/m²/K
const C_OCEAN := 2.9e8        # J/m²/K, ~70 m mixed layer
const C_LAND := 2.5e7
const C_DEEP := 2.5e9
const LAND_FORCING := 1.6
const OCEAN_FORCING := 0.53    # 0.44·1.6 + 0.56·0.53 ≈ 1: same global forcing, but land warms faster
const EXPORT := 0.75          # share of moisture shipped away from sinking-air regions
const P0 := 45.0              # rain-out rate, 1/yr (water vapour lives ~8 days)
const E0 := 1350.0            # evaporation scale, mm/yr
var ALB_ICE := 0.5             # ice albedo (planetary, clouds included)               } poles ×2.5, ice 9%
var ICE_T0 := -2.0             # ice starts forming below this annual mean temperature…
var ICE_T1 := -10.0            # …and fully covers the cell below this one
const SEA_LEVEL_R := 1.004    # planet radius of the coastline (matches Planet.BASE_SEA)
const ELEV_SCALE := 70000.0   # metres per unit of planet radius above the coast

var n := 0
var pos := PackedVector3Array()
var lat := PackedFloat32Array()          # radians
var east := PackedVector3Array()
var north := PackedVector3Array()
var nb_start := PackedInt32Array()
var nb := PackedInt32Array()
var nb_de := PackedFloat32Array()        # unit direction i→j, east component
var nb_dn := PackedFloat32Array()        # north component
var nb_dist := PackedFloat32Array()      # metres
var nb_kd := PackedFloat32Array()        # diffusion coupling, W/m²/K
var land := PackedByteArray()
var elev := PackedFloat32Array()         # metres above sea (0 for ocean)
var insol := PackedFloat32Array()        # W/m²
var veg_use := PackedFloat32Array()      # 1 = natural vegetation; the game can lower it (deforestation)

# state
var T := PackedFloat32Array()            # surface temperature °C
var q := PackedFloat32Array()            # precipitable water, mm
var P := PackedFloat32Array()            # precipitation, mm/yr
var u := PackedFloat32Array()            # wind east, m/s
var v := PackedFloat32Array()            # wind north, m/s
var w_up := PackedFloat32Array()         # large-scale rising (+) / sinking (−), −1..1
var ice := PackedFloat32Array()          # 0..1
var veg := PackedFloat32Array()          # 0..1 vegetation (follows rainfall)
var T_deep := 4.0
var co2 := 280.0
var year := 1850.0
var hadley_edge := 30.0
var global_T := 14.0
var global_E := 0.0                       # mean evaporation = mean rain, mm/yr
var baseline_T := PackedFloat32Array()   # preindustrial equilibrium, for anomaly maps
var baseline_P := PackedFloat32Array()
var ref_global_T := 14.0                 # global mean the circulation is calibrated for
var b_cell := PackedFloat32Array()


## height_fn(dir: Vector3) -> planet radius at that direction (e.g. Planet.radius_at).
func setup(level: int, height_fn: Callable) -> void:
	var ico: Array = Planet._icosphere(level)
	pos = ico[0]
	var idx: PackedInt32Array = ico[1]
	n = pos.size()
	var sets: Array = []
	sets.resize(n)
	for i in n:
		sets[i] = {}
	for f in range(0, idx.size(), 3):
		for e in 3:
			var a := idx[f + e]
			var b := idx[f + (e + 1) % 3]
			sets[a][b] = true
			sets[b][a] = true
	lat.resize(n); east.resize(n); north.resize(n); land.resize(n); elev.resize(n); insol.resize(n); veg_use.resize(n)
	nb_start.resize(n + 1)
	for i in n:
		var p := pos[i]
		lat[i] = asin(clampf(p.y, -1.0, 1.0))
		var e := Vector3.UP.cross(p)
		if e.length() < 1e-4:
			e = Vector3.RIGHT
		e = e.normalized()
		east[i] = e
		north[i] = p.cross(e).normalized()
		var r: float = height_fn.call(p)
		land[i] = 1 if r >= SEA_LEVEL_R else 0
		elev[i] = maxf(0.0, (r - SEA_LEVEL_R) * ELEV_SCALE)
		var x := sin(lat[i])
		insol[i] = Q0 * (1.0 - 0.482 * (3.0 * x * x - 1.0) * 0.5)
		veg_use[i] = 1.0
	b_cell.resize(n)
	for i in n:
		var x := sin(lat[i])
		b_cell[i] = B_OLR * (1.0 + B_POLAR * (1.0 / 3.0 - x * x)) + (B_OCEAN if land[i] == 0 else B_LAND)
	var k := 0
	for i in n:
		nb_start[i] = k
		var nbs: Array = sets[i].keys()
		for j in nbs:
			var d: Vector3 = pos[j] - pos[i]
			var t := d - pos[i] * d.dot(pos[i])
			t = t.normalized()
			var ang := pos[i].angle_to(pos[j])
			nb.append(j)
			nb_de.append(t.dot(east[i]))
			nb_dn.append(t.dot(north[i]))
			nb_dist.append(ang * R_EARTH)
			nb_kd.append(D_HEAT * 4.0 / (nbs.size() * ang * ang))
			k += 1
	nb_start[n] = k
	# (packed arrays are values in GDScript: resize each one directly, not through a loop variable)
	T.resize(n); q.resize(n); P.resize(n); u.resize(n); v.resize(n); w_up.resize(n); ice.resize(n); veg.resize(n)
	for i in n:
		var x := sin(lat[i])
		T[i] = 28.0 - 45.0 * x * x
		q[i] = _qsat(T[i]) * 0.6
		veg[i] = 0.6
		ice[i] = 1.0 if T[i] < -10.0 else 0.0
	_update_winds()


static func _qsat(t: float) -> float:
	return 25.0 * exp(0.064 * (t - 15.0))


func forcing() -> float:
	return 5.35 * log(co2 / 280.0)


## Wind and vertical motion from a three-cell circulation whose edges move with global temperature.
func _update_winds() -> void:
	hadley_edge = clampf(30.0 + 1.1 * (global_T - ref_global_T), 24.0, 40.0)
	var H := hadley_edge
	var F := clampf(60.0 + 0.5 * (global_T - ref_global_T), 55.0, 70.0)
	for i in n:
		var la := rad_to_deg(lat[i])
		var a := absf(la)
		var s := signf(la) if a > 0.01 else 1.0
		var zu: float
		var mv: float
		var wu: float
		if a < H:
			var f := a / H
			zu = -7.0 * sin(PI * f)                      # trade winds (easterly)
			mv = -s * 1.8 * sin(PI * f)                   # towards the equator
			wu = cos(PI * f)                              # rising at the ITCZ, sinking at the edge
		elif a < F:
			var f := (a - H) / (F - H)
			zu = 9.0 * sin(PI * f)                        # westerlies
			mv = s * 1.5 * sin(PI * f)                    # poleward
			wu = -cos(PI * f)                             # sinking at H, rising at the polar front
		else:
			var f := (a - F) / (90.0 - F)
			zu = -4.0 * sin(PI * f)                       # polar easterlies
			mv = -s * 1.0 * sin(PI * f)
			wu = cos(PI * f)
		var damp := 1.0
		if land[i] == 1:
			damp = 0.65 / (1.0 + elev[i] / 2000.0)
		elif la < -H:
			damp = 1.3                                    # open Southern-style ocean: stronger westerlies
		u[i] = zu * damp
		v[i] = mv * damp
		if land[i] == 0:
			wu += 0.03 * (T[i] - 26.0)                    # deep convection over very warm water
		w_up[i] = clampf(wu, -1.0, 1.2)


## Advance the model by dt years (one or more quarter-year steps).
func step(dt_years: float = 0.25, sweeps_T: int = 6, sweeps_q: int = 12) -> void:
	var dt := dt_years * SEC_YEAR
	var F := forcing()
	var alb := PackedFloat32Array()
	alb.resize(n)
	for i in n:
		var base := 0.29 if land[i] == 0 else 0.30 + 0.09 * (1.0 - veg[i])
		alb[i] = lerpf(base, ALB_ICE, ice[i])
	# ---- temperature (implicit, Gauss–Seidel) ----
	var T_old := T.duplicate()
	var ocean_sum := 0.0
	var ocean_n := 0
	for sweep in sweeps_T:
		for i in n:
			var c := (C_OCEAN if land[i] == 0 else C_LAND) / dt
			var g := GAMMA_DEEP if land[i] == 0 else 0.0
			var bi := b_cell[i]
			var diag := c + bi + g
			# land takes a larger share of the CO₂ forcing (drier air, stronger lapse-rate effects): land warms faster
			var fi := F * (LAND_FORCING if land[i] == 1 else OCEAN_FORCING)
			var rhs := c * T_old[i] + insol[i] * (1.0 - alb[i]) - A15 + bi * 15.0 + fi + g * T_deep
			var ui := u[i]
			var vi := v[i]
			for k in range(nb_start[i], nb_start[i + 1]):
				var wgt := nb_kd[k] + ADV_HEAT * maxf(0.0, -(ui * nb_de[k] + vi * nb_dn[k]))
				diag += wgt
				rhs += wgt * T[nb[k]]
			T[i] = rhs / diag
	var gsum := 0.0
	for i in n:
		gsum += T[i]
		if land[i] == 0:
			ocean_sum += T[i] - T_deep
			ocean_n += 1
	global_T = gsum / n
	T_deep += GAMMA_DEEP * (ocean_sum / maxf(1.0, ocean_n)) * dt / C_DEEP
	# ---- ice: sea ice reacts in a few years, land ice sheets over decades ----
	for i in n:
		var target := smoothstep(ICE_T0, ICE_T1, T[i])
		var tau := 2.0 if land[i] == 0 else 60.0
		ice[i] += (target - ice[i]) * minf(1.0, dt_years / tau)
	_update_winds()
	# ---- moisture (steady state each step, implicit upwind transport) ----
	var p_rate := PackedFloat32Array()
	var evap := PackedFloat32Array()
	p_rate.resize(n)
	evap.resize(n)
	for i in n:
		var qs := _qsat(T[i])
		var oro := 0.0
		for k in range(nb_start[i], nb_start[i + 1]):
			oro += (u[i] * nb_de[k] + v[i] * nb_dn[k]) * (elev[nb[k]] - elev[i]) / nb_dist[k]
		oro /= 0.01                                        # ~1 for a big windward slope
		var lift := clampf(0.75 + 0.8 * w_up[i] + 0.9 * oro, 0.06, 4.0)
		p_rate[i] = P0 * lift * clampf(q[i] / qs, 0.3, 1.6)
		var beta := 1.0 - 0.95 * ice[i]
		if land[i] == 1:
			beta = (0.2 + 0.35 * veg[i] * veg_use[i]) * (1.0 - 0.9 * ice[i])
		# evaporation is limited by energy: a few %/K globally, not the ~7 %/K of saturation vapour pressure
		evap[i] = E0 * beta * pow(qs / 25.0, 0.3)
	# Humidity ceiling: sinking air (subtropics, lee sides) is dry, rising air can approach saturation.
	var q_cap := PackedFloat32Array()
	q_cap.resize(n)
	for i in n:
		q_cap[i] = clampf(0.75 + 0.2 * w_up[i], 0.5, 0.97) * _qsat(T[i])
	for sweep in sweeps_q:
		for i in n:
			var diag := p_rate[i]
			var rhs := evap[i]
			var ui := u[i]
			var vi := v[i]
			for k in range(nb_start[i], nb_start[i + 1]):
				var m := maxf(0.0, -(ui * nb_de[k] + vi * nb_dn[k])) * SEC_YEAR / (2.0 * nb_dist[k])
				diag += m
				rhs += m * q[nb[k]]
			q[i] = minf(rhs / maxf(diag, 1e-3), q_cap[i])     # anything above the ceiling rains out
	# Rain from the moisture budget of each cell: evaporation + inflow − outflow.
	for i in n:
		var inflow := 0.0
		var msum := 0.0
		for k in range(nb_start[i], nb_start[i + 1]):
			var m := maxf(0.0, -(u[i] * nb_de[k] + v[i] * nb_dn[k])) * SEC_YEAR / (2.0 * nb_dist[k])
			inflow += m * q[nb[k]]
			msum += m
		P[i] = maxf(0.0, evap[i] + inflow - msum * q[i])
	# Overturning cells: where air sinks (subtropics, poles) most of the moisture is shipped by the
	# low-level flow to where air rises (tropical rain belt, polar front) instead of raining locally.
	var export_total := 0.0
	var sink_w := 0.0
	for i in n:
		if w_up[i] < 0.0:
			var share := clampf(-w_up[i], 0.0, 1.0) * EXPORT
			export_total += P[i] * share
			P[i] *= 1.0 - share
		elif w_up[i] > 0.2:
			sink_w += w_up[i] - 0.2
	if sink_w > 0.0:
		for i in n:
			if w_up[i] > 0.2:
				P[i] += export_total * (w_up[i] - 0.2) / sink_w
	# Water is conserved: the upwind transport is not exactly conservative, so rescale rain so that
	# global precipitation equals global evaporation (keeps the regional pattern).
	var e_sum := 0.0
	var p_sum := 0.0
	for i in n:
		e_sum += evap[i]
		p_sum += P[i]
	var fix := e_sum / maxf(p_sum, 1e-3)
	global_E = e_sum / n
	for i in n:
		P[i] *= fix
		if land[i] == 1:
			var target := clampf((P[i] - 150.0) / 900.0, 0.0, 1.0) * veg_use[i]
			veg[i] += (target - veg[i]) * minf(1.0, dt_years / 5.0)
	year += dt_years


## Run to (near) equilibrium at the current CO₂ without advancing the calendar.
func spin_up(years: int = 300) -> void:
	var y := year
	# fast phase: long steps, deep ocean pinned to the ocean mean
	for s in years:
		step(1.0, 4, 8)
		var o := 0.0
		var c := 0
		for i in n:
			if land[i] == 0:
				o += T[i]
				c += 1
		T_deep = o / c
	for s in 8:
		step(0.25)
	year = y


func store_baseline() -> void:
	ref_global_T = global_T
	baseline_T = T.duplicate()
	baseline_P = P.duplicate()


# ---------- baseline cache ----------
const STATE_VERSION := 4

func save_state(path: String) -> void:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return
	f.store_var({ "v": STATE_VERSION, "n": n, "T": T, "q": q, "P": P, "ice": ice, "veg": veg, "T_deep": T_deep,
		"global_T": global_T, "global_E": global_E, "baseline_T": baseline_T, "baseline_P": baseline_P, "ref": ref_global_T })


func load_state(path: String) -> bool:
	if not FileAccess.file_exists(path):
		return false
	var f := FileAccess.open(path, FileAccess.READ)
	var d = f.get_var()
	if not (d is Dictionary) or d.get("v", 0) != STATE_VERSION or d.get("n", 0) != n:
		return false
	T = d["T"]; q = d["q"]; P = d["P"]; ice = d["ice"]; veg = d["veg"]; T_deep = d["T_deep"]
	global_T = d["global_T"]; global_E = d.get("global_E", 0.0); baseline_T = d["baseline_T"]; baseline_P = d["baseline_P"]; ref_global_T = d["ref"]
	_update_winds()
	return true


# ---------- diagnostics ----------
func zonal_mean(field: PackedFloat32Array, lat_lo: float, lat_hi: float, only_land := -1) -> float:
	var s := 0.0
	var c := 0
	for i in n:
		var a := rad_to_deg(lat[i])
		if a >= lat_lo and a < lat_hi and (only_land < 0 or land[i] == only_land):
			s += field[i]
			c += 1
	return s / c if c > 0 else NAN


func mean_where(field: PackedFloat32Array, cond: Callable) -> float:
	var s := 0.0
	var c := 0
	for i in n:
		if cond.call(i):
			s += field[i]
			c += 1
	return s / c if c > 0 else NAN


## Nearest cell for a unit direction (used for lookups from the renderer).
func nearest_cell(dir: Vector3, hint := 0) -> int:
	var best := hint
	var bd := pos[hint].dot(dir)
	var improved := true
	while improved:
		improved = false
		for k in range(nb_start[best], nb_start[best + 1]):
			var j := nb[k]
			var d := pos[j].dot(dir)
			if d > bd:
				bd = d
				best = j
				improved = true
	return best
