// INV-04: climate mode, L4, seed 1, three layers, ten-year spin-up, then
// three years: the latent heat the humidity transport delivers to land,
// split into its eddies' part (ADR-0024 §4.3's E) and the mean flow's (the
// rest). Each transport group's delivery is weighted by the land in it;
// groups are all land (≥ 95 %), mixed, or all ocean (no land).
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/surface/heat_transport.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
using namespace planetsim;
int main() {
  Scenario sc; sc.subdivision = 4; sc.seed = 1; sc.atmosphere_layers = 3; sc.spin_up_years = 10;
  PlanetRun run(sc, 16);
  const auto& mesh = run.state().mesh();
  const auto& f = run.fractions();
  const std::vector<std::size_t>* map = nullptr;
  const auto& graph = agglomerated_transport_graph(mesh, map);
  const std::size_t groups = graph.size();
  std::vector<double> group_land(groups, 0.0), group_ocean(groups, 0.0), group_lat(groups, 0.0);
  for (std::size_t c = 0; c < mesh.cell_count(); ++c) {
    const auto& cell = mesh.cells()[c];
    group_land[(*map)[c]] += cell.area_m2 * f.land_fraction[c];
    group_ocean[(*map)[c]] += cell.area_m2 * f.ocean_fraction[c];
    group_lat[(*map)[c]] += cell.area_m2 * std::asin(std::clamp(cell.center_unit.z, -1.0, 1.0));
  }
  const auto kind = [&](std::size_t g) {
    const double share = group_land[g] / graph.area_m2[g];
    return share > 0.95 ? 0 : (group_land[g] > 0.0 ? 1 : 2);
  };
  // [kind][0 total, 1 eddy] weighted by the group's land (kinds 0, 1) or
  // ocean (kind 2); by 30° band of the group, land-weighted.
  double w[3]{}, total[3]{}, eddy[3]{};
  double bw[6]{}, btotal[6]{}, beddy[6]{};
  int months = 0;
  for (int m = 0; m < 36; ++m) {
    run.run_until(climate_substep_containing(run.tick(), run.parameters()).end_tick);
    const auto& d = run.last_step();
    if (d.cell_eddy_latent_transport_W_m2.empty()) continue;
    ++months;
    std::vector<double> delivered(groups, 0.0), e(groups, 0.0);
    for (std::size_t c = 0; c < mesh.cell_count(); ++c) {
      delivered[(*map)[c]] += mesh.cells()[c].area_m2 * d.cell_latent_transport_W_m2[c];
      e[(*map)[c]] = d.cell_eddy_latent_transport_W_m2[c];
    }
    for (std::size_t g = 0; g < groups; ++g) {
      const int k = kind(g);
      const double weight = (k == 2 ? group_ocean[g] : group_land[g]) * d.duration_s;
      const double t = delivered[g] / graph.area_m2[g];
      w[k] += weight; total[k] += weight * t; eddy[k] += weight * e[g];
      if (k == 2) continue;
      const double lat = group_lat[g] / graph.area_m2[g] * 180.0 / std::numbers::pi;
      const auto b = static_cast<std::size_t>(std::clamp(std::floor((lat + 90.0) / 30.0), 0.0, 5.0));
      const double lw = group_land[g] * d.duration_s;
      bw[b] += lw; btotal[b] += lw * t; beddy[b] += lw * e[g];
    }
  }
  std::cout << "groups " << groups << " months with moist eddies " << months << "\n";
  const char* name[3] = {"all-land groups (land-weighted)", "mixed groups (land-weighted)",
                         "all-ocean groups (ocean-weighted)"};
  for (int k = 0; k < 3; ++k)
    std::cout << name[k] << ": total " << total[k] / w[k] << " W/m2, eddies " << eddy[k] / w[k]
              << ", mean flow " << (total[k] - eddy[k]) / w[k] << "\n";
  const double land_w = w[0] + w[1];
  std::cout << "all land groups: total " << (total[0] + total[1]) / land_w << " W/m2, eddies "
            << (eddy[0] + eddy[1]) / land_w << ", mean flow "
            << (total[0] + total[1] - eddy[0] - eddy[1]) / land_w << "\n";
  for (int b = 0; b < 6; ++b)
    std::cout << "band " << -90 + 30 * b << ".." << -60 + 30 * b << ": land share " << bw[b] / land_w
              << ", total " << btotal[b] / bw[b] << " W/m2, eddies " << beddy[b] / bw[b]
              << ", mean flow " << (btotal[b] - beddy[b]) / bw[b] << "\n";
}
