#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/atmosphere/atmosphere_column.hpp"
#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "tests/test_support.hpp"

#include <cmath>
#include <iostream>

// ADR-0026 option 1 (prototype): Betts–Miller moistening, V1 and V2.
namespace {

using planetsim::ColumnMoisture;
using planetsim::ColumnRadiation;
using planetsim::LayerArray;
using planetsim::SurfaceExchange;

constexpr double surface_pressure_Pa = 100'000.0;
constexpr double cp = planetsim::dry_air_heat_capacity_J_kg_K;
constexpr double latent = planetsim::latent_heat_vaporisation_J_kg;

struct Column {
    ColumnRadiation radiation;
    ColumnMoisture moisture;

    explicit Column(bool moistening) {
        planetsim::AtmosphereParameters parameters;
        parameters.layer_count = 3U;
        const double gravity =
            planetsim::surface_gravity_m_s2(planetsim::PlanetParameters::earth_development());
        radiation = planetsim::column_radiation(parameters, surface_pressure_Pa, gravity);
        moisture.layer_mass_kg_m2 = surface_pressure_Pa / (gravity * 3.0);
        moisture.latent_J_kg = latent;
        moisture.moist_convection = true;
        moisture.moist_convection_fraction = 1.0;
        moisture.moist_convection_relative_humidity = 0.8;
        moisture.convective_moistening = moistening;
        moisture.autoconversion_fraction = 1.0;
        moisture.autoconversion_time_ratio = 1.0 / 600.0;
        moisture.evaporation_fraction = 1.0;
        for (std::size_t k = 0; k < 3U; ++k)
            moisture.pressure_Pa[k] = planetsim::layer_sigma(k, 3U) * surface_pressure_Pa;
    }
    [[nodiscard]] double saturation(double t, std::size_t k) const {
        return planetsim::saturation_specific_humidity(t, moisture.pressure_Pa[k]);
    }
};

double enthalpy(const LayerArray& t, const LayerArray& q) {
    double sum = 0.0;
    for (std::size_t k = 0; k < 3U; ++k) sum += cp * t[k] + latent * q[k];
    return sum;
}

struct HeldSurface {
    SurfaceExchange operator()(double /*downward_W_m2*/, double air_K) const {
        SurfaceExchange x;
        const double t2 = 300.0 * 300.0;
        x.upward_W_m2 = planetsim::column_stefan_boltzmann_W_m2_K4 * t2 * t2;
        x.sensible_W_m2 = 10.0 * (300.0 - air_K);
        x.d_sensible_d_air = -10.0;
        x.vapour_kg_m2_s = 3e-5;
        return x;
    }
};

}  // namespace

int main() {
    planetsim::test::Context test;

    // V2, deep: a moist bottom layer under dry layers. Every layer goes to
    // RH_bm q_sat(T_ref); the upper layers moisten; the rain is the net loss.
    {
        Column column(true);
        LayerArray t{299.0, 272.0, 238.0};
        LayerArray q{};
        q[0] = 0.98 * column.saturation(t[0], 0U);
        q[1] = 0.3 * column.saturation(t[1], 1U);
        q[2] = 0.3 * column.saturation(t[2], 2U);
        const LayerArray t0 = t;
        const LayerArray q0 = q;
        const auto bm = planetsim::betts_miller(column.radiation, column.moisture,
                                                std::span<double>(t.data(), 3U),
                                                std::span<double>(q.data(), 3U));
        double lost = 0.0;
        double worst_reference = 0.0;
        for (std::size_t k = 0; k < 3U; ++k) {
            lost += (q0[k] - q[k]) * column.moisture.layer_mass_kg_m2;
            if (bm.weight[k] >= 1.0) {
                worst_reference = std::max(worst_reference,
                                           std::abs(q[k] / column.saturation(t[k], k) - 0.8));
            }
        }
        std::cout << "deep: top " << bm.top << " shallow " << bm.shallow << ", RH after "
                  << q[0] / column.saturation(t[0], 0U) << ' ' << q[1] / column.saturation(t[1], 1U)
                  << ' ' << q[2] / column.saturation(t[2], 2U) << ", rain " << bm.precipitation_kg_m2
                  << " kg/m2\n";
        PLANETSIM_EXPECT(test, bm.top >= 1 && !bm.shallow);
        PLANETSIM_EXPECT(test, q[1] > q0[1]);   // moistened
        PLANETSIM_EXPECT(test, q[0] < q0[0]);   // dried
        PLANETSIM_EXPECT(test, worst_reference < 1e-9);
        PLANETSIM_EXPECT_NEAR(test, bm.precipitation_kg_m2, lost, 1e-12);
        PLANETSIM_EXPECT(test, bm.precipitation_kg_m2 > 0.0);
        PLANETSIM_EXPECT(test, std::abs(enthalpy(t, q) - enthalpy(t0, q0)) < 1e-12 * enthalpy(t0, q0));
    }

    // V2, shallow: a buoyant but drier column, which relaxing would
    // moisten overall. It adds no water and rains nothing; enthalpy kept.
    {
        Column column(true);
        LayerArray t{299.0, 272.0, 238.0};
        LayerArray q{};
        q[0] = 0.82 * column.saturation(t[0], 0U);
        q[1] = 0.1 * column.saturation(t[1], 1U);
        q[2] = 0.1 * column.saturation(t[2], 2U);
        const LayerArray t0 = t;
        const LayerArray q0 = q;
        const auto bm = planetsim::betts_miller(column.radiation, column.moisture,
                                                std::span<double>(t.data(), 3U),
                                                std::span<double>(q.data(), 3U));
        double water = 0.0;
        double water0 = 0.0;
        for (std::size_t k = 0; k < 3U; ++k) {
            water += q[k];
            water0 += q0[k];
        }
        std::cout << "shallow: top " << bm.top << " shallow " << bm.shallow << ", q change "
                  << q[0] - q0[0] << ' ' << q[1] - q0[1] << ' ' << q[2] - q0[2] << '\n';
        PLANETSIM_EXPECT(test, bm.top >= 1 && bm.shallow && bm.precipitation_kg_m2 == 0.0);
        PLANETSIM_EXPECT(test, std::abs(water - water0) < 1e-15);
        PLANETSIM_EXPECT(test, q[1] > q0[1]);
        PLANETSIM_EXPECT(test, std::abs(enthalpy(t, q) - enthalpy(t0, q0)) < 1e-12 * enthalpy(t0, q0));
    }

    // V1 in the column solve: water closes with the convective condensate
    // scaled to the net loss; off, the solve is unchanged.
    {
        LayerArray results[2];
        for (const bool moistening : {false, true}) {
            Column column(moistening);
            const LayerArray before{299.0, 272.0, 238.0};
            column.moisture.humidity_kg_kg = {0.95 * column.saturation(before[0], 0U),
                                              0.3 * column.saturation(before[1], 1U),
                                              0.3 * column.saturation(before[2], 2U)};
            LayerArray after = before;
            const double dt = 600.0;
            const auto r = planetsim::solve_atmosphere_column(
                column.radiation, std::span<const double>(before.data(), 3U), dt, 0.0, true,
                HeldSurface{}, std::span<double>(after.data(), 3U), {}, nullptr, &column.moisture);
            const double m = column.moisture.layer_mass_kg_m2;
            double in = 3e-5 * dt;
            double out = r.precipitation_kg_m2_s * dt;
            for (std::size_t k = 0; k < 3U; ++k) {
                in += column.moisture.humidity_kg_kg[k] * m;
                out += (r.humidity_kg_kg[k] + r.cloud_water_kg_kg[k]) * m;
            }
            std::cout << "solve, moistening " << moistening << ": middle layer q "
                      << r.humidity_kg_kg[1] << ", water error " << (out - in) / in << '\n';
            PLANETSIM_EXPECT(test, r.converged);
            PLANETSIM_EXPECT(test, std::abs(out - in) < 1e-11 * in);
            results[moistening ? 1 : 0] = r.humidity_kg_kg;
        }
        PLANETSIM_EXPECT(test, results[1][1] > results[0][1]);
    }
    return test.result();
}
