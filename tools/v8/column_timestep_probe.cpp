// INV-04: a controlled column timestep experiment, not a planetary V8 gate.
// A held 302 K surface supplies 4e-5 kg/m²/s vapour at 100 kPa. Each run
// uses the same current-preset physics and initial state for 120 days;
// means cover days 90–120. The surface is an external reservoir, so this
// experiment does not predict the free planet's temperature response.
// Exit 1: column convergence failure; exit 2: column budget failure.
// The reported post-adjustment OLR is diagnostic only, never fed back.
#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/atmosphere/atmosphere_column.hpp"
#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/planet/planet_parameters.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <span>

using namespace planetsim;
struct Surface {
    SurfaceExchange operator()(double, double air_K) const {
        SurfaceExchange x;
        x.upward_W_m2 = column_stefan_boltzmann_W_m2_K4 * std::pow(302.0, 4);
        x.sensible_W_m2 = 10.0 * (302.0 - air_K);
        x.d_sensible_d_air = -10.0;
        x.vapour_kg_m2_s = 4e-5;
        return x;
    }
};
int main() {
    const auto p = atmosphere_parameters_for(PlanetPreset::earth_like);
    const double g = surface_gravity_m_s2(PlanetParameters::earth_development());
    constexpr std::size_t n = 3;
    constexpr double ps = 100'000.0, day = 86'400.0;
    for (const double dt : {600.0, 3600.0, day, 5 * day, 30 * day}) {
        ColumnMoisture m;
        m.layer_mass_kg_m2 = ps / (g * n);
        m.latent_J_kg = latent_heat_vaporisation_J_kg;
        m.moist_convection = true;
        m.convective_moistening = true;
        m.moist_convection_fraction = -std::expm1(-dt / p.moist_convection_time_s);
        m.moist_convection_relative_humidity = p.moist_convection_relative_humidity;
        m.autoconversion_fraction = -std::expm1(-dt / p.autoconversion_time_s);
        m.autoconversion_time_ratio = p.autoconversion_time_s / dt;
        m.evaporation_fraction = -std::expm1(-dt / p.rain_evaporation_time_s);
        m.rain_evaporation_efficiency = p.rain_evaporation_efficiency;
        LayerArray t{290, 260, 225};
        for (std::size_t k = 0; k < n; ++k) {
            m.pressure_Pa[k] = layer_sigma(k, n) * ps;
            m.humidity_kg_kg[k] = 0.8 * saturation_specific_humidity(t[k], m.pressure_Pa[k]);
        }
        double mean_olr = 0, mean_shift = 0, mean_t = 0, mean_rain = 0, seconds = 0;
        double worst_water = 0, worst_energy = 0;
        const int count = static_cast<int>(120 * day / dt);
        for (int i = 0; i < count; ++i) {
            auto column =
                column_radiation(p, ps, g, std::span<const double>(m.humidity_kg_kg.data(), n));
            add_cloud_longwave(column, cloud_optics(p, column, m));
            LayerArray after = t;
            const auto r = solve_atmosphere_column(
                column, std::span<const double>(t.data(), n), dt, 0, true, Surface{},
                std::span<double>(after.data(), n), {}, nullptr, &m);
            if (!r.converged) {
                std::cerr << "column convergence failure: dt_s=" << dt << " step=" << i << '\n';
                return 1;
            }
            double water = -4e-5 * dt + r.precipitation_kg_m2_s * dt, heat = 0;
            LayerArray emission{};
            for (std::size_t k = 0; k < n; ++k) {
                water += m.layer_mass_kg_m2 * (r.humidity_kg_kg[k] + r.cloud_water_kg_kg[k] -
                                               m.humidity_kg_kg[k] - m.cloud_water_kg_kg[k]);
                heat += column.layer_heat_capacity_J_m2_K * (after[k] - t[k]) / dt;
                emission[k] =
                    column.emissivity[k] * column_stefan_boltzmann_W_m2_K4 * std::pow(after[k], 4);
            }
            const double budget = r.upward_surface_W_m2 - r.downward_surface_W_m2 -
                                  r.outgoing_W_m2 + r.sensible_W_m2 + r.condensation_W_m2;
            if (!std::isfinite(water) || !std::isfinite(heat) || !std::isfinite(budget)) {
                std::cerr << "non-finite column budget: dt_s=" << dt << " step=" << i << '\n';
                return 2;
            }
            worst_energy = std::max(worst_energy, std::abs(heat - budget));
            worst_water = std::max(worst_water, std::abs(water));
            if (i * dt >= 90 * day) {
                mean_olr += r.outgoing_W_m2 * dt;
                mean_shift +=
                    (longwave_fluxes(column, emission, r.upward_surface_W_m2).outgoing_W_m2 -
                     r.outgoing_W_m2) *
                    dt;
                mean_t += after[0] * dt;
                mean_rain += r.precipitation_kg_m2_s * dt;
                seconds += dt;
            }
            t = after;
            m.humidity_kg_kg = r.humidity_kg_kg;
            m.cloud_water_kg_kg = r.cloud_water_kg_kg;
        }
        std::cout << "dt_s=" << dt << " bottom_K=" << mean_t / seconds
                  << " olr_W_m2=" << mean_olr / seconds
                  << " post_adjustment_olr_minus_budget_W_m2=" << mean_shift / seconds
                  << " rain_mm_day=" << mean_rain / seconds * day
                  << " worst_water_kg_m2=" << worst_water << " worst_energy_W_m2=" << worst_energy
                  << '\n';
        if (worst_water > 1e-9 || worst_energy > 1e-7) {
            std::cerr << "column budget failure: dt_s=" << dt << '\n';
            return 2;
        }
    }
}
