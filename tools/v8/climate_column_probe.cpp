// INV-04 / ADR-0029: what sets climate mode's tropical air temperature.
// Climate mode only, L4, seed 1, three layers, a ten-year spin-up then three
// years (as V8's climate half), with one setting changed:
//
//   climate_column_probe [rh_bm=X] [air_exchange=X] [dry_depth_scale=X]
//                        [vapour_scale=X] [wave_speed=X] [group_mixing=X]
//                        [tropical_eddy=X]
//
// Prints the three years' means: the tropical (30° S–30° N) ocean's layer
// temperatures and humidity, the bottom layer's mass flux at 10° S and
// 10° N (the humidity transport's own faces), the latent heat delivered to
// land, the sensible heat and the global mean surface temperature.
// Investigation only: the settings are not the preset's.
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <string>

using namespace planetsim;

int main(int argc, char** argv) {
    double rh_bm = -1.0, exchange = -1.0, dry_scale = 1.0, vapour_scale = 1.0;
    double wave_speed = -1.0, group_mixing = -1.0, tropical_eddy = -1.0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&](const char* key) { return std::atof(arg.substr(std::string(key).size() + 1).c_str()); };
        if (arg.rfind("rh_bm=", 0) == 0) rh_bm = value("rh_bm");
        else if (arg.rfind("air_exchange=", 0) == 0) exchange = value("air_exchange");
        else if (arg.rfind("dry_depth_scale=", 0) == 0) dry_scale = value("dry_depth_scale");
        else if (arg.rfind("vapour_scale=", 0) == 0) vapour_scale = value("vapour_scale");
        else if (arg.rfind("wave_speed=", 0) == 0) wave_speed = value("wave_speed");
        else if (arg.rfind("group_mixing=", 0) == 0) group_mixing = value("group_mixing");
        else if (arg.rfind("tropical_eddy=", 0) == 0) tropical_eddy = value("tropical_eddy");
        else { std::cerr << "unknown setting " << arg << "\n"; return 2; }
    }
    Scenario scenario;
    scenario.subdivision = 4;
    scenario.seed = 1;
    scenario.atmosphere_layers = 3;
    scenario.spin_up_years = 10;
    PlanetRun run(scenario, 16, ClimateCirculationUse::coupled, [=](SurfaceEnergyParameters& p) {
        if (rh_bm >= 0.0) p.atmosphere.moist_convection_relative_humidity = rh_bm;
        if (exchange >= 0.0) p.air_exchange_W_m2_K = exchange;
        p.atmosphere.dry_optical_depth *= dry_scale;
        p.atmosphere.vapour_absorption_m2_kg *= vapour_scale;
        if (wave_speed >= 0.0) p.equatorial_wave_speed_m_s = wave_speed;
        if (group_mixing >= 0.0) p.heat_group_mixing = group_mixing;
        if (tropical_eddy >= 0.0) p.tropical_eddy_factor = tropical_eddy;
    });
    const auto& mesh = run.state().mesh();
    const auto& fractions = run.fractions();
    double area = 0.0;
    for (const auto& cell : mesh.cells()) area += cell.area_m2;
    const double g = surface_gravity_m_s2(run.parameters());
    double seconds = 0.0, surface = 0.0, sensible = 0.0, land_latent = 0.0, land_s = 0.0;
    double mass_10s = 0.0, mass_10n = 0.0, tropical = 0.0;
    double layer_K[3] = {}, layer_q[3] = {};
    for (int month = 0; month < 36; ++month) {
        run.run_until(climate_substep_containing(run.tick(), run.parameters()).end_tick);
        const auto& step = run.last_step();
        const double dt = step.duration_s;
        const auto& slow = run.state().slow();
        seconds += dt;
        surface += step.mean_surface_temperature_K * dt;
        sensible += step.sensible_heat_W / area * dt;
        mass_10s += step.humidity_transport.northward_mass_kg_s[0][7] * dt;
        mass_10n += step.humidity_transport.northward_mass_kg_s[0][9] * dt;
        for (std::size_t c = 0; c < mesh.cell_count(); ++c) {
            const double land = mesh.cells()[c].area_m2 * fractions.land_fraction[c];
            if (land > 0.0 && c < step.cell_latent_transport_W_m2.size()) {
                land_latent += land * step.cell_latent_transport_W_m2[c] * dt;
                land_s += land * dt;
            }
            const double lat = std::asin(std::clamp(mesh.cells()[c].center_unit.z, -1.0, 1.0)) * 180.0 / std::numbers::pi;
            const double ocean = mesh.cells()[c].area_m2 * fractions.ocean_fraction[c];
            if (std::abs(lat) < 30.0 && ocean > 0.0) {
                tropical += ocean * dt;
                for (std::size_t k = 0; k < 3; ++k) {
                    layer_K[k] += ocean * slow.atmosphere_temperature_K.layer(k)[c] * dt;
                    layer_q[k] += ocean * slow.atmosphere_specific_humidity_kg_kg.layer(k)[c] * dt;
                }
            }
        }
    }
    std::cout.setf(std::ios::fixed);
    std::cout.precision(2);
    std::cout << "surface " << surface / seconds << " K | tropical ocean air";
    for (int k = 0; k < 3; ++k) std::cout << ' ' << layer_K[k] / tropical;
    std::cout << " K, q";
    for (int k = 0; k < 3; ++k) std::cout << ' ' << 1000.0 * layer_q[k] / tropical;
    std::cout << " g/kg | bottom mass 10S/10N " << mass_10s / seconds * 1e-9 << " / " << mass_10n / seconds * 1e-9
              << " Sv | land latent " << land_latent / land_s << " W/m2 | sensible " << sensible / seconds << " W/m2\n";
}
