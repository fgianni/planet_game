extends SceneTree
## Headless play-through: builds a small region and plays 25 turns with a simple strategy.
func _init() -> void:
	for strategy in ["coal", "clean"]:
		var planet := Planet.new()
		var m := ClimateModel.new()
		m.setup(4, planet.radius_at)
		m.spin_up(160)
		m.store_baseline()
		m.co2 = 424.0
		for s in 40: m.step(0.5)                       # bring the planet to roughly 2025
		var g := GameState.new()
		g.setup(m, 7)
		print("\n=== %s === region %d cells, %d land" % [strategy, g.region.size(), _land(g)])
		while not g.over:
			_play(g, strategy)
			m.co2 += g.emissions_ppm()
			for s in 10: m.step(0.5)
			g.resolve_turn()
			if true:
				var f := g.flows()
				print("  %d  pop %3.0f  money %4.0f  mood %2.0f  concern %2.0f  food %.0f/%.0f  energy %.0f/%.0f  rain %.0f  CO2 %.0f  +%.2f°C  AMOC %.0f%%" % [
					g.year, g.pop, g.money, g.happy, g.aware, f["food"], f["food_need"], f["energy_in"], f["energy_out"],
					g.last.get("rain", 0), m.co2, m.global_T - m.ref_global_T, 100.0 * m.amoc_strength])
		print("  -> %s, score %d" % [g.result["kind"], g.result["score"]])
		print("  headlines: ", g.papers.slice(max(0, g.papers.size() - 3)).map(func(p): return "%d %s" % [p["year"], p["head"]]))
	quit()

func _land(g: GameState) -> int:
	var c := 0
	for i in g.region: if g.model.land[i] == 1: c += 1
	return c

func _play(g: GameState, strategy: String) -> void:
	var f := g.flows()
	var wants: Array = []
	if f["cap"] - g.pop < 5.0: wants.append("house")
	if f["energy_in"] - f["energy_out"] < 3.0: wants.append("coal" if strategy == "coal" else "wind")
	if f["food"] < f["food_need"] + 4.0: wants.append("farm" if strategy == "coal" else "agro")
	if g.money > 60.0: wants.append("factory" if strategy == "coal" else "forest")
	for w in wants:
		for cell in g.region:
			if g.can_place(cell, w) == "":
				g.place(cell, w)
				break
