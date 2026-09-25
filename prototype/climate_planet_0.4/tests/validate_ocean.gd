extends SceneTree
## Headless checks of the ocean: gyres, western boundary currents, heat transport, overturning.
## Run:  godot --headless --path . -s tests/validate_ocean.gd [-- --scenarios]

func _init() -> void:
	var planet := Planet.new()
	var m := ClimateModel.new()
	m.setup(4, planet.radius_at)
	m.spin_up(250)
	m.store_baseline()
	if not ("--A" in OS.get_cmdline_user_args() or "--B" in OS.get_cmdline_user_args() or "--C" in OS.get_cmdline_user_args()):
		_gyres(m)
	var args := OS.get_cmdline_user_args()
	if "--A" in args or "--scenarios" in args:
		_scenario("A: CO2 +1%/yr to 560 ppm, then hold 330 years", 560.0, 330, planet, false, 1.0)
	if "--B" in args or "--scenarios" in args:
		_scenario("B: CO2 +1%/yr to 1120 ppm, then hold", 1120.0, 100, planet, false)
	if "--C" in args or "--scenarios" in args:
		_scenario("C: as B, held 250 years, then CO2 back to 280 ppm (is the collapse reversible?)", 1120.0, 250, planet, true, 1.0)
	quit()


func _gyres(m: ClimateModel) -> void:
	print("BASELINE  global %.2f °C   ocean cells %d   northern sinking region %d cells" % [m.global_T, m.land.count(0), m.basin_north.size()])
	var vmax := 0.0
	for i in m.n:
		vmax = maxf(vmax, Vector2(m.cu[i], m.cv[i]).length())
	print("  fastest current %.2f m/s" % vmax)
	# western vs eastern boundary cells in the subtropics (15–40°), poleward velocity and SST
	for hemi: float in [1.0, -1.0]:
		var wv := 0.0; var wn := 0; var ev := 0.0; var en := 0; var wt := 0.0; var et := 0.0
		var iv := 0.0; var inn := 0
		for i in m.n:
			var la := rad_to_deg(m.lat[i]) * hemi
			if m.land[i] == 1 or la < 15.0 or la > 40.0:
				continue
			var west_land := false
			var east_land := false
			for k in range(m.nb_start[i], m.nb_start[i + 1]):
				if m.land[m.nb[k]] == 1:
					if m.nb_de[k] < -0.5: west_land = true
					if m.nb_de[k] > 0.5: east_land = true
			var poleward := m.cv[i] * hemi
			if west_land and not east_land:
				wv += poleward; wn += 1; wt += m.T[i]
			elif east_land and not west_land:
				ev += poleward; en += 1; et += m.T[i]
			elif not west_land and not east_land:
				iv += poleward; inn += 1
		print("  %s subtropics: western-boundary poleward %.2f m/s (%d cells), interior %.2f, eastern-boundary %.2f (%d cells)" % [
			"northern" if hemi > 0 else "southern", wv / maxf(1, wn), wn, iv / maxf(1, inn), ev / maxf(1, en), en])
		print("     sea temperature: western side %.1f °C vs eastern side %.1f °C   [western side warmer]" % [wt / maxf(1, wn), et / maxf(1, en)])
	print("  overturning: x %.2f  mu %.3f  q %.3f  strength %.2f   (collapse threshold mu > x²/4 = %.3f)" % [m.amoc_x, m.amoc_mu, m.amoc_q, m.amoc_strength, m.amoc_x * m.amoc_x / 4.0])


func _scenario(title: String, peak: float, hold: int, planet: Planet, reverse: bool, DT: float = 0.5) -> void:
	var m := ClimateModel.new()
	m.setup(4, planet.radius_at)
	m.spin_up(160)
	m.store_baseline()
	var g0 := m.global_T
	var n0 := _north_T(m)
	print("\n", title)
	var y := 0.0
	while m.co2 < peak:
		m.co2 = minf(peak, m.co2 * pow(1.01, DT))
		m.step(DT)
		y += DT
		if fmod(y, 20.0) < DT * 0.5: print(_row(m, y, g0, n0))
	for s in int(hold / DT):
		m.step(DT)
		y += DT
		if fmod(y, 20.0) < DT * 0.5: print(_row(m, y, g0, n0))
	if reverse:
		print("  -- CO2 now falls back to 280 ppm --")
		while m.co2 > 280.0:
			m.co2 = maxf(280.0, m.co2 * pow(0.98, DT))
			m.step(DT)
			y += DT
			if fmod(y, 20.0) < DT * 0.5: print(_row(m, y, g0, n0))
		for s in int(150 / DT):
			m.step(DT)
			y += DT
			if fmod(y, 30.0) < DT * 0.5: print(_row(m, y, g0, n0))


func _north_T(m: ClimateModel) -> float:
	var s := 0.0
	for i in m.basin_north:
		s += m.T[i]
	return s / maxf(1, m.basin_north.size())


func _row(m: ClimateModel, y: float, g0: float, n0: float) -> String:
	return "  y%3d %4dppm  global %+.2f  northern sea %+.2f  overturning %3.0f%%  (mu %.3f vs threshold %.3f)" % [
		int(y), int(m.co2), m.global_T - g0, _north_T(m) - n0, 100.0 * m.amoc_strength, m.amoc_mu, m.amoc_x * m.amoc_x / 4.0]
