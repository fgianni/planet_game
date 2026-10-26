// INV-04: climate mode, L4, seed 1, three layers, ten-year spin-up, then
// three years: the latent heat the humidity transport delivers to land,
// split into its eddies' part (ADR-0024 §4.3's E) and the mean flow's (the
// rest). Each transport group's delivery is weighted by the land in it;
// groups are all land (≥ 95 %), mixed, or all ocean (no land).
// Optional arguments, the sensitivity tests (both through the spin-up):
// SurfaceEnergyParameters::humidity_azonal_share, humidity_eddy_factor,
// zonal_advection_share (heat and water), humidity_zonal_share,
// humidity_group_mixing, heat_group_mixing and
// humidity_azonal_column_share, tropical_eddy_factor and
// equatorial_wave_speed_m_s. PROBE_LEVEL and
// PROBE_SPIN_UP_YEARS change the level and spin-up (the land-group split
// and the probe's printed band shares assume nothing about the level).
// PROBE_WORKERS sets the worker count (16); the final state hash is printed.
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/surface/heat_transport.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
using namespace planetsim;
int main(int argc, char** argv) {
  const double share = argc > 1 ? std::atof(argv[1]) : 0.0;
  const double factor = argc > 2 ? std::atof(argv[2]) : 1.0;
  const double zonal = argc > 3 ? std::atof(argv[3]) : 0.0;
  const double zonal_water = argc > 4 ? std::atof(argv[4]) : 0.0;
  const double mixing = argc > 5 ? std::atof(argv[5]) : 0.0;
  const double heat_mixing = argc > 6 ? std::atof(argv[6]) : 0.0;
  const double column_share = argc > 7 ? std::atof(argv[7]) : 0.0;
  const double tropical = argc > 8 ? std::atof(argv[8]) : 1.0;
  const double wave = argc > 9 ? std::atof(argv[9]) : 0.0;
  Scenario sc; sc.subdivision = 4; sc.seed = 1; sc.atmosphere_layers = 3; sc.spin_up_years = 10;
  if (const char* v = std::getenv("PROBE_LEVEL")) sc.subdivision = static_cast<std::uint32_t>(std::atoi(v));
  if (const char* v = std::getenv("PROBE_SPIN_UP_YEARS")) sc.spin_up_years = std::atoi(v);
  std::size_t workers = 16;
  if (const char* v = std::getenv("PROBE_WORKERS")) workers = static_cast<std::size_t>(std::atoi(v));
  PlanetRun run(sc, workers, ClimateCirculationUse::coupled, [&](SurfaceEnergyParameters& s) {
    s.humidity_azonal_share = share;
    s.humidity_eddy_factor = factor;
    s.zonal_advection_share = zonal;
    s.humidity_zonal_share = zonal_water;
    s.humidity_group_mixing = mixing;
    s.heat_group_mixing = heat_mixing;
    s.humidity_azonal_column_share = column_share;
    s.tropical_eddy_factor = tropical;
    s.equatorial_wave_speed_m_s = wave;
  });
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
  int months = 0, fallbacks = 0;
  double within = 0, within_w = 0, between = 0, between_w = 0;
  int newton = 0, open_energy = 0, open_water = 0; double solve_s = 0, precipitation_kg = 0;
  double seconds = 0, mean_K = 0, land_K = 0, land_q = 0, land_area_s = 0;
  for (int m = 0; m < 36; ++m) {
    run.run_until(climate_substep_containing(run.tick(), run.parameters()).end_tick);
    const auto& d = run.last_step();
    seconds += d.duration_s; mean_K += d.duration_s * d.mean_surface_temperature_K;
    land_K += d.duration_s * d.land_mean_surface_temperature_K;
    fallbacks += d.transport_fell_back ? 1 : 0;
    newton += d.transport_newton_iterations; solve_s += d.transport_solve_s;
    precipitation_kg += d.precipitation_kg;
    if (std::getenv("PROBE_MONTHS")) std::cerr << "month " << m << " newton " << d.transport_newton_iterations << " residual " << d.transport_consistency_W_m2 << "\n";
    open_energy += d.closure_residual_J() > d.closure_gate_J() ? 1 : 0;
    open_water += d.water_cycle_residual_kg() > d.water_cycle_gate_kg() ? 1 : 0;
    for (std::size_t c = 0; c < mesh.cell_count(); ++c) {
      const double a = mesh.cells()[c].area_m2 * f.land_fraction[c] * d.duration_s;
      land_area_s += a;
      land_q += a * run.state().slow().atmosphere_specific_humidity_kg_kg.layer(0)[c];
    }
    {
      // Bottom-layer air: land cells against ocean cells (land fraction
      // above 0.5 or below 0.5) within each mixed group, and all-land
      // groups against all-ocean groups in the same 10° band.
      const auto t = run.state().slow().atmosphere_temperature_K.layer(0);
      std::vector<double> lw(groups, 0.0), lt(groups, 0.0), ow(groups, 0.0), ot(groups, 0.0);
      for (std::size_t c = 0; c < mesh.cell_count(); ++c) {
        const double a = mesh.cells()[c].area_m2;
        if (f.land_fraction[c] > 0.5) { lw[(*map)[c]] += a; lt[(*map)[c]] += a * t[c]; }
        else { ow[(*map)[c]] += a; ot[(*map)[c]] += a * t[c]; }
      }
      double bl[18]{}, blw[18]{}, bo[18]{}, bow[18]{};
      for (std::size_t g = 0; g < groups; ++g) {
        const double lat = group_lat[g] / graph.area_m2[g] * 180.0 / std::numbers::pi;
        const auto b = static_cast<std::size_t>(std::clamp(std::floor((lat + 90.0) / 10.0), 0.0, 17.0));
        if (lw[g] > 0 && ow[g] > 0) {
          within_w += lw[g] * d.duration_s; within += lw[g] * d.duration_s * (lt[g] / lw[g] - ot[g] / ow[g]);
        } else if (ow[g] == 0) { bl[b] += lt[g]; blw[b] += lw[g]; }
        else if (group_land[g] == 0) { bo[b] += ot[g]; bow[b] += ow[g]; }
      }
      for (int b = 0; b < 18; ++b)
        if (blw[b] > 0 && bow[b] > 0) {
          between_w += blw[b] * d.duration_s;
          between += blw[b] * d.duration_s * (bl[b] / blw[b] - bo[b] / bow[b]);
        }
    }
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
  std::cout << "azonal_share " << share << " eddy_factor " << factor << " zonal " << zonal
            << " zonal_water " << zonal_water << " group_mixing " << mixing << " heat_mixing " << heat_mixing << " column_share " << column_share << " tropical_eddy " << tropical << " wave_speed " << wave << " groups " << groups
            << " months with moist eddies " << months << " fallback months " << fallbacks << "\n"
            << "mean_K " << mean_K / seconds << " land_K " << land_K / seconds << " land_air_q_g_kg "
            << 1000 * land_q / land_area_s << " land_minus_ocean_K "
            << (land_K / seconds - (mean_K / seconds - 0.29 * land_K / seconds) / 0.71)
            << " precipitation_m_yr "
            << precipitation_kg / 1000.0 / (4.0 * std::numbers::pi * mesh.radius_m() * mesh.radius_m()) /
                   (seconds / (365.25 * 86400.0))
            << "\n";
  std::cout << "measured months: newton " << newton << " transport_solve_s " << solve_s
            << " energy_gate_failures " << open_energy << " water_gate_failures " << open_water
            << " state_hash " << std::hex << run.state_hash() << std::dec << "\n";
  std::cout << "air land_minus_ocean_K within_mixed_groups " << within / within_w
            << " all_land_vs_all_ocean_groups_same_band " << between / between_w << "\n";
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
  // The last month's humidity by layer, area-weighted over the planet.
  std::cout << "final q_g_kg by layer:";
  for (std::size_t l = 0; l < run.state().slow().atmosphere_layer_count(); ++l) {
    double qa = 0, aa = 0;
    for (std::size_t c = 0; c < mesh.cell_count(); ++c) {
      qa += mesh.cells()[c].area_m2 * run.state().slow().atmosphere_specific_humidity_kg_kg.layer(l)[c];
      aa += mesh.cells()[c].area_m2;
    }
    std::cout << " " << 1000 * qa / aa;
  }
  std::cout << "\n";
}
