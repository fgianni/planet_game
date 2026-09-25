extends SceneTree
## Headless checks of the climate model against well-known patterns.
## Run:  godot --headless --path . -s tests/validate_climate.gd

func _init() -> void:
	var planet := Planet.new()
	var m := ClimateModel.new()
	var t0 := Time.get_ticks_msec()
	m.setup(4, planet.radius_at)
	print("cells: %d  land: %.0f%%" % [m.n, 100.0 * m.land.count(1) / m.n])
	m.spin_up(300)
	m.store_baseline()
	print("spin-up: %.1f s" % ((Time.get_ticks_msec() - t0) / 1000.0))
	_report(m, "PREINDUSTRIAL 280 ppm (equilibrium)")
	var base_global := m.global_T
	var base_hadley := m.hadley_edge
	var tp := m.zonal_mean(m.T, 60, 91) ; var sp := m.zonal_mean(m.T, -91, -60)
	var trop := m.mean_where(m.T, func(i): return absf(rad_to_deg(m.lat[i])) < 30)
	var pole := m.mean_where(m.T, func(i): return absf(rad_to_deg(m.lat[i])) > 60)

	# speed of a normal quarter-year step
	var t1 := Time.get_ticks_msec()
	for s in 20: m.step(0.25)
	print("step time: %.1f ms per quarter-year" % ((Time.get_ticks_msec() - t1) / 20.0))

	# transient: 1% per year CO2 rise to doubling (~70 years), then compare with equilibrium
	m.spin_up(60)
	var ty := 0.0
	var trace := "  ramp:"
	while m.co2 < 560.0:
		m.co2 = minf(560.0, m.co2 * pow(1.01, 0.25))
		m.step(0.25)
		ty += 0.25
		if int(ty * 4) % 40 == 0:
			trace += "  y%d %.0fppm %+.2f°C deep %.2f" % [ty, m.co2, m.global_T - base_global, m.T_deep]
	print(trace)
	var transient := m.global_T - base_global
	print("\nTRANSIENT: CO2 doubled in %.0f years -> +%.2f °C at that moment" % [ty, transient])
	m.spin_up(400)
	_report(m, "DOUBLED CO2 560 ppm (equilibrium)")
	var dG := m.global_T - base_global
	var trop2 := m.mean_where(m.T, func(i): return absf(rad_to_deg(m.lat[i])) < 30)
	var pole2 := m.mean_where(m.T, func(i): return absf(rad_to_deg(m.lat[i])) > 60)
	# compare land and ocean at the same latitudes, away from ice (as climate studies do)
	var mid := func(i, l): return m.land[i] == l and absf(rad_to_deg(m.lat[i])) < 60.0 and m.baseline_T[i] > 0.0
	var dl := m.mean_where(m.T, func(i): return mid.call(i, 1)) - m.mean_where(m.baseline_T, func(i): return mid.call(i, 1))
	var do_ := m.mean_where(m.T, func(i): return mid.call(i, 0)) - m.mean_where(m.baseline_T, func(i): return mid.call(i, 0))
	print("\nCHECKS")
	print("  climate sensitivity (doubling): +%.2f °C   [target ~3]" % dG)
	print("  transient / equilibrium: %.2f              [target 0.5–0.75: the lag]" % (transient / dG))
	print("  polar amplification: poles +%.2f vs tropics +%.2f -> ×%.1f   [target 2–4]" % [pole2 - pole, trop2 - trop, (pole2 - pole) / maxf(0.01, trop2 - trop)])
	print("  land vs ocean warming (60°S–60°N, ice-free): +%.2f vs +%.2f -> ×%.2f   [land warms more, ~1.3–1.6]" % [dl, do_, dl / do_])
	print("  Hadley edge: %.1f° -> %.1f°   [moves poleward]" % [base_hadley, m.hadley_edge])
	# wet gets wetter / dry drier (zonal)
	var wet0 := m.zonal_mean(m.baseline_P, -10, 10); var wet1 := m.zonal_mean(m.P, -10, 10)
	var dry0 := (m.zonal_mean(m.baseline_P, 20, 35) + m.zonal_mean(m.baseline_P, -35, -20)) * 0.5
	var dry1 := (m.zonal_mean(m.P, 20, 35) + m.zonal_mean(m.P, -35, -20)) * 0.5
	print("  equatorial rain %.0f -> %.0f mm/yr, subtropical rain %.0f -> %.0f mm/yr" % [wet0, wet1, dry0, dry1])
	var gp0 := m.mean_where(m.baseline_P, func(i): return true)
	var gp1 := m.mean_where(m.P, func(i): return true)
	print("  global rain %.0f -> %.0f mm/yr = %+.1f %%/K   [target ~2–3 %%/K]" % [gp0, gp1, 100.0 * (gp1 / gp0 - 1.0) / dG])
	print("  tropical circulation %+.1f %%/K   [models: slows, roughly −1 to −2 %%/K]" % [100.0 * (m.trade_factor - 1.0) / dG])
	print("  westerlies %+.0f %% as the equator–pole contrast shrinks" % [100.0 * (m.westerly_factor - 1.0)])
	var lc0 := m.mean_where(m.cloud_low_base, func(i): return true); var lc1 := m.mean_where(m.cloud_low, func(i): return true)
	print("  low clouds %+.0f %%   [thin as the sea warms: positive feedback]" % [100.0 * (lc1 / lc0 - 1.0)])
	# climate sensitivity across the cloud-feedback uncertainty
	var line := "  sensitivity by cloud feedback:"
	for fb in ["low", "medium", "high"]:
		var mm := ClimateModel.new()
		mm.setup(4, planet.radius_at)
		mm.spin_up(220)
		mm.store_baseline()
		var g0 := mm.global_T
		mm.set_cloud_feedback(fb)
		mm.co2 = 560.0
		mm.spin_up(260)
		line += "  %s %+.2f °C" % [fb, mm.global_T - g0]
	print(line + "   [IPCC likely 2.5–4, very likely 2–5]")
	quit()


func _report(m: ClimateModel, title: String) -> void:
	print("\n", title)
	print("  global mean %.2f °C   deep ocean %.2f °C   Hadley edge %.1f°" % [m.global_T, m.T_deep, m.hadley_edge])
	var line := "  zonal T:"
	var line_p := "  zonal P:"
	for b in [[-90, -60], [-60, -30], [-30, -10], [-10, 10], [10, 30], [30, 60], [60, 91]]:
		line += "  [%d,%d] %.1f" % [b[0], b[1], m.zonal_mean(m.T, b[0], b[1])]
		line_p += "  [%d,%d] %.0f" % [b[0], b[1], m.zonal_mean(m.P, b[0], b[1])]
	print(line)
	print(line_p)
	var desert := 0
	var land := 0
	var desert_lat := 0.0
	for i in m.n:
		if m.land[i] == 1 and m.ice[i] < 0.5:
			land += 1
			if m.P[i] < 250.0:
				desert += 1
				desert_lat += absf(rad_to_deg(m.lat[i]))
	print("  ice cells %.0f%%   deserts (P<250) %.0f%% of ice-free land, mean |lat| %.0f°   global mean P %.0f mm/yr" % [
		100.0 * m.mean_where(m.ice, func(i): return true), 100.0 * desert / maxf(1, land), desert_lat / maxf(1, desert), m.mean_where(m.P, func(i): return true)])
	# rain shadow: windward vs lee of high land
	var ww := 0.0; var wn := 0; var lw := 0.0; var ln := 0
	for i in m.n:
		if m.land[i] == 0 or m.elev[i] < 300.0: continue
		var oro := 0.0
		for k in range(m.nb_start[i], m.nb_start[i + 1]):
			oro += (m.u[i] * m.nb_de[k] + m.v[i] * m.nb_dn[k]) * (m.elev[m.nb[k]] - m.elev[i]) / m.nb_dist[k]
		if oro > 0.003: ww += m.P[i]; wn += 1
		elif oro < -0.003: lw += m.P[i]; ln += 1
	print("  windward slopes %.0f mm/yr (%d cells) vs lee slopes %.0f mm/yr (%d cells)" % [ww / maxf(1, wn), wn, lw / maxf(1, ln), ln])
