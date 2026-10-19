#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/atmosphere/atmosphere_column.hpp"
#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/planet/atmosphere/subgrid_cloud.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

// Sub-grid condensation (ADR-0025 §4.1, V1, V2; task M8-07).
namespace {

using planetsim::ColumnMoisture;
using planetsim::ColumnRadiation;
using planetsim::LayerArray;
using planetsim::SurfaceExchange;

constexpr double surface_pressure_Pa = 100'000.0;
constexpr double cp = planetsim::dry_air_heat_capacity_J_kg_K;
constexpr double latent = planetsim::latent_heat_vaporisation_J_kg;
constexpr double month_s = 2.63e6;

// ∫ max(0, s') p(s') ds' and ∫_{s' > 0} p(s') ds' for the triangle of half-width b
// centred on s, by the midpoint rule.
std::pair<double, double> integrated(double s, double b) {
    constexpr int steps = 200'000;
    double water = 0.0;
    double area = 0.0;
    const double h = 2.0 * b / steps;
    for (int i = 0; i < steps; ++i) {
        const double x = -b + (i + 0.5) * h;   // offset from the mean
        const double p = (b - std::abs(x)) / (b * b);
        const double value = s + x;
        if (value > 0.0) {
            water += value * p * h;
            area += p * h;
        }
    }
    return {water, area};
}

struct Column {
    ColumnRadiation radiation;
    ColumnMoisture moisture;

    Column(double dt_s, double width) {
        planetsim::AtmosphereParameters parameters;
        parameters.layer_count = 3U;
        const double gravity =
            planetsim::surface_gravity_m_s2(planetsim::PlanetParameters::earth_development());
        radiation = planetsim::column_radiation(parameters, surface_pressure_Pa, gravity);
        moisture.layer_mass_kg_m2 = surface_pressure_Pa / (gravity * 3.0);
        moisture.latent_J_kg = latent;
        moisture.moist_convection = true;
        moisture.moist_convection_fraction = -std::expm1(-dt_s / parameters.moist_convection_time_s);
        moisture.cloud_water_threshold_kg_kg = parameters.cloud_water_threshold_kg_kg;
        moisture.autoconversion_fraction = -std::expm1(-dt_s / parameters.autoconversion_time_s);
        moisture.autoconversion_time_ratio = parameters.autoconversion_time_s / dt_s;
        moisture.evaporation_fraction = -std::expm1(-dt_s / parameters.rain_evaporation_time_s);
        moisture.rain_evaporation_efficiency = parameters.rain_evaporation_efficiency;
        moisture.subgrid_width_fraction = width;
        for (std::size_t k = 0; k < 3U; ++k) {
            moisture.pressure_Pa[k] = planetsim::layer_sigma(k, 3U) * surface_pressure_Pa;
        }
    }

    [[nodiscard]] double saturation(double t, std::size_t k) const {
        return planetsim::saturation_specific_humidity(t, moisture.pressure_Pa[k]);
    }
};

struct HeldSurface {
    double surface_K = 295.0;
    double evaporation_kg_m2_s = 0.0;

    SurfaceExchange operator()(double /*downward_W_m2*/, double air_K) const {
        SurfaceExchange x;
        const double t2 = surface_K * surface_K;
        x.upward_W_m2 = planetsim::column_stefan_boltzmann_W_m2_K4 * t2 * t2;
        x.sensible_W_m2 = 10.0 * (surface_K - air_K);
        x.d_sensible_d_air = -10.0;
        x.vapour_kg_m2_s = evaporation_kg_m2_s;
        return x;
    }
};

}  // namespace

int main() {
    planetsim::test::Context test;

    // 1. V2: the closed forms are the triangle's integrals.
    {
        const double b = 2.0e-3;
        double worst_water = 0.0;
        double worst_area = 0.0;
        for (double q = -1.4; q <= 1.4; q += 0.07) {
            const auto cloud = planetsim::subgrid_cloud(q * b, b);
            const auto [water, area] = integrated(q * b, b);
            worst_water = std::max(worst_water, std::abs(cloud.water_kg_kg - water) / b);
            worst_area = std::max(worst_area, std::abs(cloud.area - area));
        }
        std::cout << "closed forms against the integral: water " << worst_water << " b, area "
                  << worst_area << '\n';
        PLANETSIM_EXPECT(test, worst_water < 1e-6 && worst_area < 1e-6);
    }

    // 2. V2: dq_l*/ds is the area, dq_l*/db the stated form; continuous at
    // Q = −1, 0, 1; q_l* rises with s; b → 0 is saturation adjustment.
    {
        const double b = 1.5e-3;
        double worst_slope = 0.0;
        double worst_width = 0.0;
        double worst_jump = 0.0;
        bool rising = true;
        double previous = -1.0;
        for (double q = -1.3; q <= 1.3; q += 0.013) {
            const double s = q * b;
            const double h = 1e-9;
            const auto cloud = planetsim::subgrid_cloud(s, b);
            const double ds = (planetsim::subgrid_cloud(s + h, b).water_kg_kg -
                               planetsim::subgrid_cloud(s - h, b).water_kg_kg) / (2.0 * h);
            const double db = (planetsim::subgrid_cloud(s, b + h).water_kg_kg -
                               planetsim::subgrid_cloud(s, b - h).water_kg_kg) / (2.0 * h);
            worst_slope = std::max(worst_slope, std::abs(ds - cloud.area));
            worst_width = std::max(worst_width, std::abs(db - cloud.d_water_d_width));
            rising = rising && cloud.water_kg_kg >= previous;
            previous = cloud.water_kg_kg;
        }
        for (const double q : {-1.0, 0.0, 1.0}) {
            const auto below = planetsim::subgrid_cloud(q * b * (1.0 - 1e-12) - 1e-18, b);
            const auto above = planetsim::subgrid_cloud(q * b * (1.0 + 1e-12) + 1e-18, b);
            worst_jump = std::max({worst_jump, std::abs(above.water_kg_kg - below.water_kg_kg) / b,
                                   std::abs(above.area - below.area)});
        }
        PLANETSIM_EXPECT(test, worst_slope < 1e-6 && worst_width < 1e-6);
        PLANETSIM_EXPECT(test, worst_jump < 1e-9 && rising);
        PLANETSIM_EXPECT(test, planetsim::subgrid_cloud(3e-4, 0.0).water_kg_kg == 3e-4 &&
                                   planetsim::subgrid_cloud(-3e-4, 0.0).water_kg_kg == 0.0);
        PLANETSIM_EXPECT_NEAR(test, planetsim::subgrid_cloud(3e-4, 1e-12).water_kg_kg, 3e-4, 1e-15);
        PLANETSIM_EXPECT(test, planetsim::subgrid_cloud(-1e-3, 1e-3).water_kg_kg == 0.0);
    }

    // 3. At β = 0 the evaporation towards equilibrium is the evaporation
    // to saturation (bounded by the cloud water available).
    {
        Column column(600.0, 0.0);
        double worst = 0.0;
        for (const double rh : {0.3, 0.7, 0.95}) {
            for (const double cloud : {1e-5, 1e-4, 1e-3}) {
                const double t = 270.0;
                const double q = rh * column.saturation(t, 1U);
                const double heating = latent / cp;
                // The equilibrium root at β = 0 needs a positive width to
                // take the new branch: a vanishing one gives the same root.
                column.moisture.subgrid_width_fraction = 1e-12;
                const double towards = planetsim::evaporation_to_equilibrium(
                    column.moisture, 1U, t, q, cloud, cloud, heating);
                const double to_saturation = std::min(
                    cloud, planetsim::evaporation_to_saturation(t, q, column.moisture.pressure_Pa[1],
                                                                heating));
                worst = std::max(worst, std::abs(towards - to_saturation) / cloud);
            }
        }
        PLANETSIM_EXPECT(test, worst < 1e-6);
    }

    // 4. V1 and §4.1 in the column solve, a month and ten minutes over an
    // evaporating surface, a conditionally unstable column (convecting) and
    // a stable one. Water closes; in the stable column at 90 % no layer
    // saturates, so it holds cloud water only with β = 0.2.
    for (const double dt_s : {600.0, month_s}) {
        for (const bool stable : {false, true})
        for (const double width : {0.0, 0.2}) {
            Column column(dt_s, width);
            const LayerArray before = stable ? LayerArray{280.0, 276.0, 262.0}
                                             : LayerArray{290.0, 268.0, 236.0};
            for (std::size_t k = 0; k < 3U; ++k) {
                column.moisture.humidity_kg_kg[k] = 0.9 * column.saturation(before[k], k);
                column.moisture.cloud_water_kg_kg[k] = 0.0;
            }
            HeldSurface surface;
            surface.evaporation_kg_m2_s = 2e-5;
            LayerArray after = before;
            const auto result = planetsim::solve_atmosphere_column(
                column.radiation, std::span<const double>(before.data(), 3U), dt_s, 0.0, true,
                surface, std::span<double>(after.data(), 3U), {}, nullptr, &column.moisture);
            const double m = column.moisture.layer_mass_kg_m2;
            double water_in = surface.evaporation_kg_m2_s * dt_s;
            double water_out = result.precipitation_kg_m2_s * dt_s;
            double cloud = 0.0;
            for (std::size_t k = 0; k < 3U; ++k) {
                water_in += column.moisture.humidity_kg_kg[k] * m;
                water_out += (result.humidity_kg_kg[k] + result.cloud_water_kg_kg[k]) * m;
                cloud += result.cloud_water_kg_kg[k] * m;
            }
            PLANETSIM_EXPECT(test, result.converged);
            PLANETSIM_EXPECT(test, std::abs(water_in - water_out) < 1e-11 * water_in);
            std::cout << "dt " << dt_s << " s, " << (stable ? "stable" : "unstable") << ", beta " << width << ": cloud water "
                      << cloud * 1e3 << " g/m2, precipitation " << result.precipitation_kg_m2_s * 86'400.0
                      << " mm/day, RH";
            for (std::size_t k = 0; k < 3U; ++k)
                std::cout << ' ' << result.humidity_kg_kg[k] / column.saturation(after[k], k);
            std::cout << '\n';
            if (stable && dt_s == 600.0) {
                PLANETSIM_EXPECT(test, result.convection_top < 1);
                // From 90 %: below saturation the layers hold cloud only
                // with the sub-grid spread.
                PLANETSIM_EXPECT(test, width > 0.0 ? cloud > 0.0 : cloud == 0.0);
            }
        }
    }
    return test.result();
}
