// INV-04: climate mode, L4, seed 1, three layers, ten-year spin-up, then
// three years: the heat the transport delivers to land, split by whether
// the land cell's transport group (ADR-0009 §12) is all land or mixed.
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/surface/heat_transport.hpp"
#include <iostream>
using namespace planetsim;
int main() {
  Scenario sc; sc.subdivision = 4; sc.seed = 1; sc.atmosphere_layers = 3; sc.spin_up_years = 10;
  PlanetRun run(sc, 16);
  const auto& mesh = run.state().mesh();
  const auto& f = run.fractions();
  const std::vector<std::size_t>* map = nullptr;
  const auto& graph = agglomerated_transport_graph(mesh, map);
  std::vector<double> group_land(graph.size(), 0.0), group_area(graph.size(), 0.0);
  for (std::size_t c = 0; c < mesh.cell_count(); ++c) {
    group_land[(*map)[c]] += mesh.cells()[c].area_m2 * f.land_fraction[c];
    group_area[(*map)[c]] += mesh.cells()[c].area_m2;
  }
  double w[2]{}, lat[2]{}, dry[2]{}, t[2]{}, q[2]{};
  for (int m = 0; m < 36; ++m) {
    run.run_until(climate_substep_containing(run.tick(), run.parameters()).end_tick);
    const auto& d = run.last_step();
    const auto& s = run.state().slow();
    for (std::size_t c = 0; c < mesh.cell_count(); ++c) {
      const double a = mesh.cells()[c].area_m2 * f.land_fraction[c] * d.duration_s;
      if (!(a > 0)) continue;
      const int k = group_land[(*map)[c]] / group_area[(*map)[c]] > 0.95 ? 0 : 1;
      w[k] += a; lat[k] += a * d.cell_latent_transport_W_m2[c]; dry[k] += a * d.cell_transport_W_m2[c];
      t[k] += a * s.land_surface_temperature_K[c]; q[k] += a * s.atmosphere_specific_humidity_kg_kg.layer(0)[c];
    }
  }
  double land = 0; for (std::size_t c = 0; c < mesh.cell_count(); ++c) land += mesh.cells()[c].area_m2 * f.land_fraction[c];
  std::cout << "groups " << graph.size() << " cells " << mesh.cell_count() << "\n";
  const char* name[2] = {"all-land groups", "mixed groups"};
  for (int k = 0; k < 2; ++k)
    std::cout << name[k] << ": share of land " << w[k] / (w[0] + w[1]) << ", latent " << lat[k] / w[k]
              << " W/m2, dry " << dry[k] / w[k] << " W/m2, land surface " << t[k] / w[k] << " K, air q " << 1000 * q[k] / w[k] << " g/kg\n";
}
