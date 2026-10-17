#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/atmosphere/atmosphere_column.hpp"
#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

// Moist convection (ADR-0023 §4.2, V2–V4; task M8-02): the dry adjustment to
// the dry adiabat and the Betts–Miller relaxation, on single columns.
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
    double gravity = 0.0;

    explicit Column(std::size_t layers) {
        planetsim::AtmosphereParameters parameters;
        parameters.layer_count = static_cast<std::uint32_t>(layers);
        gravity = planetsim::surface_gravity_m_s2(planetsim::PlanetParameters::earth_development());
        radiation = planetsim::column_radiation(parameters, surface_pressure_Pa, gravity);
        moisture.layer_mass_kg_m2 = surface_pressure_Pa / (gravity * static_cast<double>(layers));
        moisture.latent_J_kg = latent;
        moisture.moist_convection = true;
        moisture.moist_convection_fraction = 1.0;
        moisture.moist_convection_relative_humidity = 0.8;
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
    for (std::size_t k = 0; k < n; ++k) {
        sum += cp * t[k] + latent * q[k];
    }
    return sum;
}

// A surface held at `surface_K`, evaporating a fixed rate.
struct HeldSurface {
    double surface_K = 300.0;
    double exchange_W_m2_K = 10.0;
    double evaporation_kg_m2_s = 0.0;

    SurfaceExchange operator()(double /*downward_W_m2*/, double air_K) const {
        SurfaceExchange x;
        const double t2 = surface_K * surface_K;
        x.upward_W_m2 = planetsim::column_stefan_boltzmann_W_m2_K4 * t2 * t2;
        x.sensible_W_m2 = exchange_W_m2_K * (surface_K - air_K);
        x.d_sensible_d_air = -exchange_W_m2_K;
        x.vapour_kg_m2_s = evaporation_kg_m2_s;
        return x;
    }
};

}  // namespace

int main() {
    planetsim::test::Context test;

    // 1. V3: the dry adjustment acts only on super-adiabatic layers. A column
    // between the moist and the dry adiabats (6.5 K/km-like) is untouched;
    // a super-adiabatic one is mixed to the dry adiabat, Σ T kept.
    {
        Column column(5U);
        ColumnRadiation dry = column.radiation;
        dry.exner = column.radiation.exner_dry;
        LayerArray stable{};
        LayerArray unstable{};
        for (std::size_t k = 0; k < 5U; ++k) {
            // θ rising with height on the dry adiabat, and falling.
            stable[k] = (290.0 + 4.0 * static_cast<double>(k)) * dry.exner[k];
            unstable[k] = (300.0 - 2.0 * static_cast<double>(k)) * dry.exner[k];
        }
        LayerArray before = stable;
        const bool stable_changed =
            planetsim::convective_adjustment(dry, std::span<double>(stable.data(), 5U));
        PLANETSIM_EXPECT(test, !stable_changed && stable == before);
        double sum_before = 0.0;
        for (std::size_t k = 0; k < 5U; ++k) sum_before += unstable[k];
        const bool unstable_changed =
            planetsim::convective_adjustment(dry, std::span<double>(unstable.data(), 5U));
        double sum_after = 0.0;
        double theta_spread = 0.0;
        for (std::size_t k = 0; k < 5U; ++k) {
            sum_after += unstable[k];
            theta_spread = std::max(theta_spread,
                                    std::abs(unstable[k] / dry.exner[k] - unstable[0] / dry.exner[0]));
        }
        PLANETSIM_EXPECT(test, unstable_changed);
        PLANETSIM_EXPECT_NEAR(test, sum_after, sum_before, 1e-10);
        PLANETSIM_EXPECT(test, theta_spread < 1e-9);
        // The moist adiabat's Γ_c is not the dry one's: a profile neutral to
        // Γ_c is stable to the dry adiabat.
        LayerArray gamma_c{};
        for (std::size_t k = 0; k < 5U; ++k) gamma_c[k] = 300.0 * column.radiation.exner[k];
        const LayerArray gamma_c_before = gamma_c;
        PLANETSIM_EXPECT(test,
                         !planetsim::convective_adjustment(dry, std::span<double>(gamma_c.data(), 5U)) &&
                             gamma_c == gamma_c_before);
    }

    // A conditionally unstable column: warm and moist at the bottom, steep
    // lapse aloft (the parcel is buoyant to the top).
    const LayerArray buoyant_t{300.0, 284.0, 266.0, 243.0, 196.0};
    // A stable one: an inversion over a cool bottom layer.
    const LayerArray stable_t{280.0, 285.0, 280.0, 268.0, 245.0};

    // 2. V4 and V2: with r = 1 the column reaches the reference: T on the
    // parcel's moist adiabat shifted by one constant, the column's moist
    // enthalpy kept to round-off, no layer moistened, the loss the
    // precipitation.
    {
        Column column(5U);
        LayerArray t = buoyant_t;
        LayerArray q{};
        for (std::size_t k = 0; k < 5U; ++k) q[k] = 0.95 * column.saturation(t[k], k);
        const LayerArray t0 = t;
        const LayerArray q0 = q;
        const auto bm = planetsim::betts_miller(column.radiation, column.moisture,
                                                std::span<double>(t.data(), 5U),
                                                std::span<double>(q.data(), 5U));
        PLANETSIM_EXPECT(test, bm.top == 4);
        PLANETSIM_EXPECT(test, !bm.shallow && bm.precipitation_kg_m2 > 0.0);
        double worst_reference = 0.0;
        bool drier = true;
        double water = 0.0;
        for (std::size_t k = 0; k < 5U; ++k) {
            worst_reference = std::max(worst_reference, std::abs(t[k] - (bm.parcel_K[k] + bm.shift_K)));
            drier = drier && q[k] <= q0[k];
            water += (q0[k] - q[k]) * column.moisture.layer_mass_kg_m2;
        }
        PLANETSIM_EXPECT(test, worst_reference < 1e-9);
        PLANETSIM_EXPECT(test, drier);
        PLANETSIM_EXPECT_NEAR(test, water, bm.precipitation_kg_m2, 1e-12);
        const double enthalpy_scale = moist_enthalpy(t0, q0, 5U);
        PLANETSIM_EXPECT(test, std::abs(moist_enthalpy(t, q, 5U) - enthalpy_scale) <
                                   1e-12 * enthalpy_scale);
        // The parcel conserves its moist static energy c_p T + Φ + L q_sat
        // where saturated (layers 1..4 here): Φ from the environment's
        // hydrostatics. Checked to the parcel solver's tolerance.
        double phi = 0.0;
        double worst_mse = 0.0;
        const double start = cp * t0[0] + latent * q0[0];
        for (std::size_t k = 1; k < 5U; ++k) {
            phi += planetsim::dry_air_gas_constant_J_kg_K * 0.5 * (t0[k - 1U] + t0[k]) *
                   std::log(column.moisture.pressure_Pa[k - 1U] / column.moisture.pressure_Pa[k]);
            const double mse =
                cp * bm.parcel_K[k] + phi + latent * column.saturation(bm.parcel_K[k], k);
            worst_mse = std::max(worst_mse, std::abs(mse - start));
        }
        PLANETSIM_EXPECT(test, worst_mse < 1e-6);
        std::cout << "buoyant column: shift " << bm.shift_K << " K, rain "
                  << bm.precipitation_kg_m2 << " kg/m2, parcel";
        for (std::size_t k = 0; k < 5U; ++k) std::cout << ' ' << bm.parcel_K[k];
        std::cout << '\n';

        // A part step, r = 1 − exp(−Δt/τ_bm), moves each layer by r of the
        // full step, and the reference is the same.
        Column partial(5U);
        partial.moisture.moist_convection_fraction = -std::expm1(-600.0 / 7200.0);
        LayerArray tp = t0;
        LayerArray qp = q0;
        const auto part = planetsim::betts_miller(partial.radiation, partial.moisture,
                                                  std::span<double>(tp.data(), 5U),
                                                  std::span<double>(qp.data(), 5U));
        const double r = partial.moisture.moist_convection_fraction;
        double worst_share = 0.0;
        for (std::size_t k = 0; k < 5U; ++k) {
            worst_share = std::max(worst_share, std::abs((tp[k] - t0[k]) - r * (t[k] - t0[k])));
            worst_share = std::max(worst_share, std::abs((qp[k] - q0[k]) - r * (q[k] - q0[k])) * 1e3);
        }
        PLANETSIM_EXPECT(test, part.top == bm.top && worst_share < 1e-9);
        PLANETSIM_EXPECT_NEAR(test, part.precipitation_kg_m2, r * bm.precipitation_kg_m2, 1e-12);
    }

    // 3. V4: a stable column is untouched, bit for bit.
    {
        Column column(5U);
        LayerArray t = stable_t;
        LayerArray q{};
        for (std::size_t k = 0; k < 5U; ++k) q[k] = 0.9 * column.saturation(t[k], k);
        const LayerArray t0 = t;
        const LayerArray q0 = q;
        const auto bm = planetsim::betts_miller(column.radiation, column.moisture,
                                                std::span<double>(t.data(), 5U),
                                                std::span<double>(q.data(), 5U));
        PLANETSIM_EXPECT(test, bm.top == -1 && t == t0 && q == q0 && bm.precipitation_kg_m2 == 0.0);
    }

    // 4. ADR-0023 §4.2.5: a buoyant but dry column moistens nothing: the
    // shallow case, temperature alone relaxes, Σ T kept.
    {
        Column column(5U);
        LayerArray t = buoyant_t;
        LayerArray q{};
        q[0] = 0.9 * column.saturation(t[0], 0U);
        for (std::size_t k = 1; k < 5U; ++k) q[k] = 0.05 * column.saturation(t[k], k);
        const LayerArray t0 = t;
        const LayerArray q0 = q;
        const auto bm = planetsim::betts_miller(column.radiation, column.moisture,
                                                std::span<double>(t.data(), 5U),
                                                std::span<double>(q.data(), 5U));
        bool drier = true;
        for (std::size_t k = 0; k < 5U; ++k) drier = drier && q[k] <= q0[k];
        PLANETSIM_EXPECT(test, bm.top >= 1 && drier);
        const double scale = moist_enthalpy(t0, q0, 5U);
        PLANETSIM_EXPECT(test, std::abs(moist_enthalpy(t, q, 5U) - scale) < 1e-12 * scale);
        std::cout << "dry buoyant column: top " << bm.top << ", shallow " << bm.shallow
                  << ", rain " << bm.precipitation_kg_m2 << " kg/m2\n";
    }

    // 5. In the column solve (ADR-0023 §4.2–§4.3): a reference step and a
    // climate month over an evaporating surface. The moist enthalpy change
    // of convection is round-off, no layer ends above saturation, and the
    // precipitation's latent heat is the condensation reported.
    for (const double dt_s : {600.0, 2.63e6}) {
        for (const double evaporation : {0.0, 3e-5}) {
            Column column(5U);
            column.moisture.moist_convection_fraction = -std::expm1(-dt_s / 7200.0);
            column.moisture.autoconversion_fraction = -std::expm1(-dt_s / 3600.0);
            column.moisture.autoconversion_time_ratio = 3600.0 / dt_s;
            column.moisture.evaporation_fraction = -std::expm1(-dt_s / 3600.0);
            LayerArray before = buoyant_t;
            for (std::size_t k = 0; k < 5U; ++k)
                column.moisture.humidity_kg_kg[k] = 0.95 * column.saturation(before[k], k);
            HeldSurface surface;
            surface.evaporation_kg_m2_s = evaporation;
            LayerArray after = before;
            const auto result = planetsim::solve_atmosphere_column(
                column.radiation, std::span<const double>(before.data(), 5U), dt_s, 0.0, true,
                surface, std::span<double>(after.data(), 5U), {}, nullptr, &column.moisture);
            double worst_saturation = 0.0;
            double water_in = evaporation * dt_s;
            double water_out = result.precipitation_kg_m2_s * dt_s;
            double cloud_kg_m2 = 0.0;   // the cloud water starts at zero
            for (std::size_t k = 0; k < 5U; ++k) {
                worst_saturation = std::max(
                    worst_saturation, result.humidity_kg_kg[k] / column.saturation(after[k], k) - 1.0);
                water_in += column.moisture.humidity_kg_kg[k] * column.moisture.layer_mass_kg_m2;
                water_out += (result.humidity_kg_kg[k] + result.cloud_water_kg_kg[k]) *
                             column.moisture.layer_mass_kg_m2;
                cloud_kg_m2 += result.cloud_water_kg_kg[k] * column.moisture.layer_mass_kg_m2;
            }
            PLANETSIM_EXPECT(test, result.converged);
            PLANETSIM_EXPECT(test, result.convection_top >= 1);
            PLANETSIM_EXPECT(test, worst_saturation < 1e-9);
            PLANETSIM_EXPECT(test, std::abs(water_in - water_out) < 1e-12 * water_in);
            PLANETSIM_EXPECT(test, std::abs(result.convection_enthalpy_change_J_m2) <
                                       1e-9 * column.moisture.layer_mass_kg_m2 * cp * 300.0);
            // The net latent heat: L (P + Δ cloud water) (ADR-0023 §4.3).
            PLANETSIM_EXPECT_NEAR(test, result.condensation_W_m2,
                                  latent * (result.precipitation_kg_m2_s + cloud_kg_m2 / dt_s),
                                  1e-9 * std::max(1.0, result.condensation_W_m2));
            PLANETSIM_EXPECT(test, result.convective_precipitation_kg_m2_s > 0.0 &&
                                       result.convective_precipitation_kg_m2_s <=
                                           result.precipitation_kg_m2_s);
            std::cout << "dt " << dt_s << " s, E " << evaporation << ": top "
                      << result.convection_top << ", P " << result.precipitation_kg_m2_s
                      << " (convective " << result.convective_precipitation_kg_m2_s
                      << ") kg/m2/s, cloud " << cloud_kg_m2 << " kg/m2, T0 " << after[0] << " K\n";
        }
    }

    // 6. The onset is continuous (task M8-02): as layer 1 cools through the
    // parcel's temperature the column joins smoothly, so the monthly
    // transport solve has a root. Every layer's change and the rain move by
    // little more than the 1 mK sweep step across the onset.
    {
        Column column(5U);
        LayerArray base = buoyant_t;
        LayerArray q{};
        for (std::size_t k = 0; k < 5U; ++k) q[k] = 0.95 * column.saturation(base[k], k);
        // The parcel's layer-1 temperature for this start.
        LayerArray probe_t = base;
        LayerArray probe_q = q;
        const auto probe = planetsim::betts_miller(column.radiation, column.moisture,
                                                   std::span<double>(probe_t.data(), 5U),
                                                   std::span<double>(probe_q.data(), 5U));
        const double onset = probe.parcel_K[1];
        double worst_jump_K = 0.0;
        double worst_rain_jump = 0.0;
        LayerArray previous_t{};
        double previous_rain = 0.0;
        bool first = true;
        int convecting = 0;
        for (double offset = -2.0; offset <= 2.0; offset += 1e-3) {
            LayerArray t = base;
            t[1] = onset + offset;
            LayerArray qq = q;
            const LayerArray start = t;
            const auto bm = planetsim::betts_miller(column.radiation, column.moisture,
                                                    std::span<double>(t.data(), 5U),
                                                    std::span<double>(qq.data(), 5U));
            convecting += bm.top >= 1 ? 1 : 0;
            LayerArray change{};
            for (std::size_t k = 0; k < 5U; ++k) change[k] = t[k] - start[k];
            if (!first) {
                for (std::size_t k = 0; k < 5U; ++k)
                    worst_jump_K = std::max(worst_jump_K, std::abs(change[k] - previous_t[k]));
                worst_rain_jump = std::max(worst_rain_jump, std::abs(bm.precipitation_kg_m2 - previous_rain));
            }
            previous_t = change;
            previous_rain = bm.precipitation_kg_m2;
            first = false;
        }
        std::cout << "onset sweep: worst change jump " << worst_jump_K << " K, rain jump "
                  << worst_rain_jump << " kg/m2 per mK\n";
        PLANETSIM_EXPECT(test, convecting > 0 && convecting < 4001);
        PLANETSIM_EXPECT(test, worst_jump_K < 0.05);
        PLANETSIM_EXPECT(test, worst_rain_jump < 0.1);
    }

    // 7. Off, the column solve is M7's, bit for bit.
    {
        Column column(5U);
        LayerArray before = buoyant_t;
        for (std::size_t k = 0; k < 5U; ++k)
            column.moisture.humidity_kg_kg[k] = 0.95 * column.saturation(before[k], k);
        ColumnMoisture m7 = column.moisture;
        m7.moist_convection = false;
        m7.moist_convection_fraction = 0.3;
        ColumnMoisture plain = m7;
        plain.moist_convection_fraction = 1.0;
        plain.moist_convection_relative_humidity = 0.5;
        plain.cloud_water_kg_kg[1] = 1e-3;   // cloud water is M8's too
        plain.autoconversion_fraction = 0.2;
        LayerArray a = before;
        LayerArray b = before;
        const auto ra = planetsim::solve_atmosphere_column(
            column.radiation, std::span<const double>(before.data(), 5U), 600.0, 0.0, true,
            HeldSurface{}, std::span<double>(a.data(), 5U), {}, nullptr, &m7);
        const auto rb = planetsim::solve_atmosphere_column(
            column.radiation, std::span<const double>(before.data(), 5U), 600.0, 0.0, true,
            HeldSurface{}, std::span<double>(b.data(), 5U), {}, nullptr, &plain);
        PLANETSIM_EXPECT(test, a == b && ra.precipitation_kg_m2_s == rb.precipitation_kg_m2_s &&
                                   ra.convection_top == -1 && rb.cloud_water_kg_kg[1] == 0.0);
    }

    return test.result();
}
