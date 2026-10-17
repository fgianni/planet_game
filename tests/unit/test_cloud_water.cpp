#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/atmosphere/atmosphere_column.hpp"
#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>

// Cloud water to precipitation (ADR-0023 §4.3, V1, V2, V5; task M8-03):
// autoconversion, the evaporation of cloud water and of falling
// precipitation, on single columns and in the column solve.
namespace {

using planetsim::ColumnMoisture;
using planetsim::ColumnRadiation;
using planetsim::LayerArray;
using planetsim::SurfaceExchange;

constexpr double surface_pressure_Pa = 100'000.0;
constexpr double cp = planetsim::dry_air_heat_capacity_J_kg_K;
constexpr double latent = planetsim::latent_heat_vaporisation_J_kg;
constexpr double month_s = 2.63e6;

struct Column {
    ColumnRadiation radiation;
    ColumnMoisture moisture;

    explicit Column(std::size_t layers, double dt_s) {
        planetsim::AtmosphereParameters parameters;
        parameters.layer_count = static_cast<std::uint32_t>(layers);
        const double gravity =
            planetsim::surface_gravity_m_s2(planetsim::PlanetParameters::earth_development());
        radiation = planetsim::column_radiation(parameters, surface_pressure_Pa, gravity);
        moisture.layer_mass_kg_m2 = surface_pressure_Pa / (gravity * static_cast<double>(layers));
        moisture.latent_J_kg = latent;
        moisture.moist_convection = true;
        moisture.moist_convection_fraction = -std::expm1(-dt_s / parameters.moist_convection_time_s);
        moisture.cloud_water_threshold_kg_kg = parameters.cloud_water_threshold_kg_kg;
        moisture.autoconversion_fraction = -std::expm1(-dt_s / parameters.autoconversion_time_s);
        moisture.evaporation_fraction = -std::expm1(-dt_s / parameters.rain_evaporation_time_s);
        moisture.rain_evaporation_efficiency = parameters.rain_evaporation_efficiency;
        for (std::size_t k = 0; k < layers; ++k) {
            moisture.pressure_Pa[k] = planetsim::layer_sigma(k, layers) * surface_pressure_Pa;
        }
    }

    [[nodiscard]] double saturation(double t, std::size_t k) const {
        return planetsim::saturation_specific_humidity(t, moisture.pressure_Pa[k]);
    }
};

// Σ (c_p T + L q) of equal-mass layers, per kilogram of one layer.
double moist_enthalpy(const LayerArray& t, const LayerArray& q, std::size_t n) {
    double sum = 0.0;
    for (std::size_t k = 0; k < n; ++k) sum += cp * t[k] + latent * q[k];
    return sum;
}

double total(const LayerArray& x, std::size_t n) {
    double sum = 0.0;
    for (std::size_t k = 0; k < n; ++k) sum += x[k];
    return sum;
}

struct HeldSurface {
    double surface_K = 300.0;
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
    constexpr std::size_t n = 5U;
    const LayerArray profile{295.0, 283.0, 268.0, 248.0, 222.0};
    const LayerArray none{};

    // 1. The evaporation to saturation: q + x = q_sat(T − L x / c_p) to
    // rounding, from below, and nothing where the layer is saturated.
    {
        const Column column(n, 600.0);
        double worst = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            for (const double rh : {0.0, 0.3, 0.9, 0.999999}) {
                const double q = rh * column.saturation(profile[k], k);
                const double x = planetsim::evaporation_to_saturation(
                    profile[k], q, column.moisture.pressure_Pa[k], latent / cp);
                const double t = profile[k] - latent / cp * x;
                worst = std::max(worst, std::abs(q + x - column.saturation(t, k)) /
                                            column.saturation(t, k));
                PLANETSIM_EXPECT(test, x >= 0.0 && q + x <= column.saturation(t, k) * (1.0 + 1e-12));
            }
            PLANETSIM_EXPECT(test, planetsim::evaporation_to_saturation(
                                       profile[k], 1.01 * column.saturation(profile[k], k),
                                       column.moisture.pressure_Pa[k], latent / cp) == 0.0);
        }
        std::cout << "evaporation to saturation: worst relative error " << worst << '\n';
        PLANETSIM_EXPECT(test, worst < 1e-12);
    }

    // 2. V5: autoconversion. A climate month brings each saturated layer's
    // cloud water to q_l0 exactly; a reference step converts
    // 1 − exp(−Δt/τ_a) of the excess; below q_l0 nothing rains. Saturated
    // layers evaporate nothing, so all the converted water reaches the
    // surface (the rain falls through saturated air).
    for (const double dt_s : {600.0, month_s}) {
        Column column(n, dt_s);
        LayerArray t = profile;
        LayerArray q{};
        LayerArray cloud{0.2e-3, 1.0e-3, 2.0e-3, 0.5e-3, 0.0};
        LayerArray condensate{0.0, 0.5e-3, 0.0, 0.0, 0.0};
        for (std::size_t k = 0; k < n; ++k) q[k] = column.saturation(t[k], k);
        const LayerArray start = cloud;
        const auto result = planetsim::cloud_microphysics(
            column.radiation, column.moisture, std::span<const double>(condensate.data(), n),
            std::span<const double>(none.data(), n), std::span<double>(t.data(), n),
            std::span<double>(q.data(), n), std::span<double>(cloud.data(), n));
        const double q0 = column.moisture.cloud_water_threshold_kg_kg;
        const double r = column.moisture.autoconversion_fraction;
        double expected = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            const double with = start[k] + condensate[k];
            const double excess = std::max(0.0, with - q0);
            PLANETSIM_EXPECT_NEAR(test, cloud[k], with - r * excess, 1e-15);
            expected += r * excess;
            PLANETSIM_EXPECT(test, t[k] == profile[k]);
        }
        PLANETSIM_EXPECT_NEAR(test, result.precipitation_kg_m2,
                              expected * column.moisture.layer_mass_kg_m2, 1e-12);
        PLANETSIM_EXPECT(test, result.rain_evaporation_kg_m2 == 0.0 &&
                                   result.cloud_evaporation_kg_m2 == 0.0);
        if (dt_s == month_s) {
            for (const std::size_t k : {1U, 2U, 3U}) PLANETSIM_EXPECT_NEAR(test, cloud[k], q0, 1e-18);
            PLANETSIM_EXPECT(test, cloud[0] == start[0]);
        }
        std::cout << "autoconversion, dt " << dt_s << " s: rain " << result.precipitation_kg_m2
                  << " kg/m2, cloud water left " << cloud[1] * 1e3 << " g/kg in layer 1\n";
    }

    // 3. V5 and V2: cloud water in subsaturated air evaporates towards
    // saturation and never beyond. A month with plenty of cloud water ends
    // the layer exactly saturated; with little it all evaporates; a
    // reference step evaporates 1 − exp(−Δt/τ_e) of it unless saturation
    // stops it first. The moist enthalpy c_p T + L q is kept.
    for (const double dt_s : {600.0, month_s}) {
        for (const double amount : {1e-5, 5e-3}) {
            Column column(n, dt_s);
            column.moisture.cloud_water_threshold_kg_kg = 1.0;   // no autoconversion
            LayerArray t = profile;
            LayerArray q{};
            LayerArray cloud{};
            for (std::size_t k = 0; k < n; ++k) {
                q[k] = 0.5 * column.saturation(t[k], k);
                cloud[k] = amount;
            }
            const double before = moist_enthalpy(t, q, n);
            const double water = total(q, n) + total(cloud, n);
            const auto result = planetsim::cloud_microphysics(
                column.radiation, column.moisture, std::span<const double>(none.data(), n),
                std::span<const double>(none.data(), n), std::span<double>(t.data(), n),
                std::span<double>(q.data(), n), std::span<double>(cloud.data(), n));
            double worst_rh = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                const double rh = q[k] / column.saturation(t[k], k);
                worst_rh = std::max(worst_rh, rh);
                PLANETSIM_EXPECT(test, cloud[k] >= 0.0);
                if (dt_s == month_s && amount > 1e-3) {
                    PLANETSIM_EXPECT_NEAR(test, rh, 1.0, 1e-12);
                }
                if (dt_s == month_s && amount < 1e-3) {
                    PLANETSIM_EXPECT(test, cloud[k] < 1e-15);
                }
                if (dt_s == 600.0 && amount < 1e-3) {
                    PLANETSIM_EXPECT_NEAR(test, cloud[k],
                                          amount * (1.0 - column.moisture.evaporation_fraction),
                                          1e-18);
                }
            }
            PLANETSIM_EXPECT(test, worst_rh <= 1.0 + 1e-12);
            PLANETSIM_EXPECT_NEAR(test, moist_enthalpy(t, q, n), before, 1e-10 * before);
            PLANETSIM_EXPECT_NEAR(test, total(q, n) + total(cloud, n), water, 1e-15);
            PLANETSIM_EXPECT_NEAR(test, result.cloud_evaporation_kg_m2,
                                  (total(cloud, n) - n * amount) * -column.moisture.layer_mass_kg_m2,
                                  1e-12);
            PLANETSIM_EXPECT(test, result.precipitation_kg_m2 == 0.0);
        }
    }

    // 4. V5, V1 and V2: falling precipitation re-evaporates by
    // ε (1 − RH) (1 − exp(−Δt/τ_e)) in each layer below its source and never
    // supersaturates one, over random columns; the water and the moist
    // enthalpy are kept, and the convective part evaporates in proportion.
    {
        std::mt19937_64 random(20261017U);
        std::uniform_real_distribution<double> uniform(0.0, 1.0);
        double worst_rh = 0.0;
        double worst_water = 0.0;
        double worst_enthalpy = 0.0;
        double evaporated = 0.0;
        double formed = 0.0;
        for (int trial = 0; trial < 2'000; ++trial) {
            const double dt_s = trial % 2 == 0 ? 600.0 : month_s;
            Column column(n, dt_s);
            LayerArray t{};
            LayerArray q{};
            LayerArray cloud{};
            LayerArray condensate{};
            LayerArray convective{};
            for (std::size_t k = 0; k < n; ++k) {
                t[k] = profile[k] + 10.0 * (uniform(random) - 0.5);
                q[k] = uniform(random) * column.saturation(t[k], k);
                cloud[k] = 2e-3 * uniform(random);
                condensate[k] = 2e-3 * uniform(random) * uniform(random);
                convective[k] = uniform(random) < 0.3 ? 5e-3 * uniform(random) : 0.0;
            }
            const double before = moist_enthalpy(t, q, n);
            const double water = total(q, n) + total(cloud, n) + total(condensate, n) +
                                 total(convective, n);
            const auto result = planetsim::cloud_microphysics(
                column.radiation, column.moisture, std::span<const double>(condensate.data(), n),
                std::span<const double>(convective.data(), n), std::span<double>(t.data(), n),
                std::span<double>(q.data(), n), std::span<double>(cloud.data(), n));
            const double m = column.moisture.layer_mass_kg_m2;
            // The condensate's latent heat enters with the column solve; here
            // only the evaporations cool.
            const double after = moist_enthalpy(t, q, n);
            worst_enthalpy = std::max(worst_enthalpy, std::abs(after - before) / before);
            const double left = total(q, n) + total(cloud, n) + result.precipitation_kg_m2 / m;
            worst_water = std::max(worst_water, std::abs(left - water) / water);
            for (std::size_t k = 0; k < n; ++k) {
                worst_rh = std::max(worst_rh, q[k] / column.saturation(t[k], k));
            }
            PLANETSIM_EXPECT(test, result.convective_kg_m2 >= 0.0 &&
                                       result.convective_kg_m2 <= total(convective, n) * m * (1.0 + 1e-14) &&
                                       result.convective_kg_m2 <=
                                           result.precipitation_kg_m2 * (1.0 + 1e-14));
            evaporated += result.rain_evaporation_kg_m2;
            formed += result.autoconversion_kg_m2 + total(convective, n) * m;
        }
        std::cout << "falling precipitation: worst RH " << worst_rh << ", water " << worst_water
                  << ", enthalpy " << worst_enthalpy << "; " << evaporated / formed
                  << " of the rain formed re-evaporated\n";
        PLANETSIM_EXPECT(test, worst_rh <= 1.0 + 1e-12);
        PLANETSIM_EXPECT(test, worst_water < 1e-13);
        PLANETSIM_EXPECT(test, worst_enthalpy < 1e-13);
        PLANETSIM_EXPECT(test, evaporated > 0.0 && evaporated < formed);
    }

    // 5. The rule in one layer: rain P falling into a dry-enough layer loses
    // ε (1 − RH) r P exactly, and none in the layer where it forms.
    {
        Column column(2U, 600.0);
        LayerArray t{290.0, 280.0};
        LayerArray q{0.4 * column.saturation(290.0, 0), column.saturation(280.0, 1)};
        LayerArray cloud{0.0, 1.3e-3};
        const double rh = q[0] / column.saturation(t[0], 0);
        const auto result = planetsim::cloud_microphysics(
            column.radiation, column.moisture, std::span<const double>(none.data(), 2U),
            std::span<const double>(none.data(), 2U), std::span<double>(t.data(), 2U),
            std::span<double>(q.data(), 2U), std::span<double>(cloud.data(), 2U));
        const double formed = result.autoconversion_kg_m2;
        PLANETSIM_EXPECT_NEAR(test, result.rain_evaporation_kg_m2,
                              0.5 * (1.0 - rh) * column.moisture.evaporation_fraction * formed,
                              1e-12 * formed);
        PLANETSIM_EXPECT_NEAR(test, result.precipitation_kg_m2,
                              formed - result.rain_evaporation_kg_m2, 1e-15);
    }

    // 6. V1 in the column solve: from a cloudy start, at a reference step
    // and over a climate month, with evaporation: vapour + cloud water +
    // precipitation close to round-off, no layer ends supersaturated, the
    // net latent heat is L (P + Δ cloud water), and a month leaves the
    // condensing layers at q_l0.
    for (const double dt_s : {600.0, month_s}) {
        Column column(n, dt_s);
        LayerArray before{300.0, 284.0, 266.0, 243.0, 196.0};
        for (std::size_t k = 0; k < n; ++k) {
            column.moisture.humidity_kg_kg[k] = 0.95 * column.saturation(before[k], k);
            column.moisture.cloud_water_kg_kg[k] = 0.4e-3;
        }
        HeldSurface surface;
        surface.evaporation_kg_m2_s = 3e-5;
        LayerArray after = before;
        const auto result = planetsim::solve_atmosphere_column(
            column.radiation, std::span<const double>(before.data(), n), dt_s, 0.0, true, surface,
            std::span<double>(after.data(), n), {}, nullptr, &column.moisture);
        const double m = column.moisture.layer_mass_kg_m2;
        double water_in = surface.evaporation_kg_m2_s * dt_s;
        double water_out = result.precipitation_kg_m2_s * dt_s;
        double cloud_change = 0.0;
        double worst_rh = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            water_in += (column.moisture.humidity_kg_kg[k] + column.moisture.cloud_water_kg_kg[k]) * m;
            water_out += (result.humidity_kg_kg[k] + result.cloud_water_kg_kg[k]) * m;
            cloud_change += (result.cloud_water_kg_kg[k] - column.moisture.cloud_water_kg_kg[k]) * m;
            worst_rh = std::max(worst_rh, result.humidity_kg_kg[k] / column.saturation(after[k], k));
        }
        PLANETSIM_EXPECT(test, result.converged);
        PLANETSIM_EXPECT(test, std::abs(water_in - water_out) < 1e-12 * water_in);
        PLANETSIM_EXPECT(test, worst_rh <= 1.0 + 1e-9);
        PLANETSIM_EXPECT_NEAR(test, result.condensation_W_m2,
                              latent * (result.precipitation_kg_m2_s + cloud_change / dt_s),
                              1e-9 * std::max(1.0, std::abs(result.condensation_W_m2)));
        if (dt_s == month_s) {
            for (std::size_t k = 0; k < n; ++k) {
                PLANETSIM_EXPECT(test, result.cloud_water_kg_kg[k] <=
                                           column.moisture.cloud_water_threshold_kg_kg * (1.0 + 1e-12));
                if (result.condensate_kg_kg[k] > 0.0) {
                    PLANETSIM_EXPECT_NEAR(test, result.cloud_water_kg_kg[k],
                                          column.moisture.cloud_water_threshold_kg_kg, 1e-15);
                }
            }
        }
        std::cout << "column solve, dt " << dt_s << " s: P " << result.precipitation_kg_m2_s * 86'400.0
                  << " mm/day (convective " << result.convective_precipitation_kg_m2_s * 86'400.0
                  << "), cloud " << total(result.cloud_water_kg_kg, n) * m * 1e3
                  << " g/m2, re-evaporated " << result.rain_evaporation_kg_m2_s * 86'400.0
                  << " mm/day of rain and " << result.cloud_evaporation_kg_m2_s * 86'400.0
                  << " of cloud\n";
    }

    return test.result();
}
