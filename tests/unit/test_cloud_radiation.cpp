#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/atmosphere/atmosphere_column.hpp"
#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <utility>

// Cloud radiation (ADR-0023 §4.4, §13, V6; tasks M8-04, M8-05) on single columns: the
// fraction, the in-cloud optical depth, the shortwave reflection and the
// longwave depth.
namespace {

using planetsim::AtmosphereParameters;
using planetsim::ColumnMoisture;
using planetsim::ColumnRadiation;
using planetsim::LayerArray;

constexpr double surface_pressure_Pa = 100'000.0;
constexpr std::size_t n = 4U;

struct Column {
    AtmosphereParameters parameters;
    ColumnRadiation radiation;
    ColumnMoisture moisture;
    LayerArray temperature{290.0, 275.0, 255.0, 230.0};

    Column() {
        parameters.layer_count = static_cast<std::uint32_t>(n);
        const double gravity =
            planetsim::surface_gravity_m_s2(planetsim::PlanetParameters::earth_development());
        radiation = planetsim::column_radiation(parameters, surface_pressure_Pa, gravity);
        moisture.layer_mass_kg_m2 = surface_pressure_Pa / (gravity * static_cast<double>(n));
        moisture.latent_J_kg = planetsim::latent_heat_vaporisation_J_kg;
        moisture.moist_convection = true;
        for (std::size_t k = 0; k < n; ++k) {
            moisture.pressure_Pa[k] = planetsim::layer_sigma(k, n) * surface_pressure_Pa;
        }
    }

    [[nodiscard]] double saturation(std::size_t k) const {
        return planetsim::saturation_specific_humidity(temperature[k], moisture.pressure_Pa[k]);
    }

    [[nodiscard]] planetsim::CloudOptics optics() const {
        return planetsim::cloud_optics(parameters, radiation, moisture);
    }
};

}  // namespace

int main() {
    planetsim::test::Context test;

    // 1. The fraction (§13.3): f = min(1, q_l m / W_c), whatever the
    // humidity. Saturated air without cloud water is clear; a convecting,
    // dry column without cloud water is clear.
    {
        Column column;
        const double w_c = column.parameters.cloud_in_cloud_water_path_kg_m2;
        const double m = column.moisture.layer_mass_kg_m2;
        for (const double path : {0.0, 0.01, 0.05, 0.1, 0.3}) {
            for (std::size_t k = 0; k < n; ++k) {
                column.moisture.humidity_kg_kg[k] = column.saturation(k);
                column.moisture.cloud_water_kg_kg[k] = path / m;
            }
            const auto optics = column.optics();
            const double expected = std::min(1.0, path / w_c);
            for (std::size_t k = 0; k < n; ++k) {
                PLANETSIM_EXPECT_NEAR(test, optics.fraction[k], expected, 1e-12);
            }
            PLANETSIM_EXPECT_NEAR(test, optics.column_fraction,
                                  1.0 - std::pow(1.0 - expected, static_cast<double>(n)), 1e-12);
            if (path == 0.0) {
                PLANETSIM_EXPECT(test, optics.transmission == 1.0 && optics.longwave_depth[0] == 0.0);
            }
        }
        column.temperature = {300.0, 280.0, 258.0, 225.0};
        for (std::size_t k = 0; k < n; ++k) {
            column.moisture.humidity_kg_kg[k] = (k == 0U ? 0.8 : 0.3) * column.saturation(k);
            column.moisture.cloud_water_kg_kg[k] = 0.0;
        }
        const auto optics = column.optics();
        PLANETSIM_EXPECT(test, optics.column_fraction == 0.0 && optics.transmission == 1.0);
    }

    // 2. Below overcast a layer's reflection and longwave depth are
    // proportional to its cloud water, so the mean of two states' optics is
    // the optics of their mean (§13.3, §13.5).
    {
        Column column;
        const double m = column.moisture.layer_mass_kg_m2;
        const auto layer_optics = [&](double path_kg_m2) {
            for (std::size_t k = 0; k < n; ++k) {
                column.moisture.cloud_water_kg_kg[k] = 0.0;
            }
            column.moisture.cloud_water_kg_kg[1] = path_kg_m2 / m;
            const auto o = column.optics();
            return std::pair{o.reflectance[1], o.longwave_depth[1]};
        };
        const auto [r1, l1] = layer_optics(0.01);
        const auto [r4, l4] = layer_optics(0.04);
        const auto [r9, l9] = layer_optics(0.09);
        PLANETSIM_EXPECT_NEAR(test, r4, 4.0 * r1, 1e-14);
        PLANETSIM_EXPECT_NEAR(test, l4, 4.0 * l1, 1e-12);
        const auto [rm, lm] = layer_optics(0.05);
        PLANETSIM_EXPECT_NEAR(test, rm, 0.5 * (r1 + r9), 1e-14);
        PLANETSIM_EXPECT_NEAR(test, lm, 0.5 * (l1 + l9), 1e-12);
        // Above overcast the reflection saturates: less than proportional.
        const auto [r3, l3] = layer_optics(0.3);
        PLANETSIM_EXPECT(test, r3 < 3.0 * r9 / 0.9 && r3 > r9);
        std::cout << "reflection at 10, 40, 90, 300 g/m2: " << r1 << ' ' << r4 << ' ' << r9 << ' '
                  << r3 << '\n';
    }

    // 3. Optics (Stephens 1978): an overcast layer with 50 g/m² of cloud
    // water has τ = 3 · 0.05 / (2 · 1000 · 10 µm) = 7.5, reflects
    // 0.15 · 7.5 / (2 + 0.15 · 7.5) = 0.36, and its longwave depth
    // 0.13 m²/g · 50 g/m² = 6.5 makes it nearly black.
    // With W_c = 50 g/m² the layer is just overcast.
    {
        Column column;
        column.parameters.cloud_in_cloud_water_path_kg_m2 = 0.05;
        for (std::size_t k = 0; k < n; ++k) {
            column.moisture.humidity_kg_kg[k] = 0.5 * column.saturation(k);
        }
        column.moisture.humidity_kg_kg[1] = column.saturation(1);
        column.moisture.cloud_water_kg_kg[1] = 0.05 / column.moisture.layer_mass_kg_m2;
        const auto optics = column.optics();
        PLANETSIM_EXPECT_NEAR(test, optics.fraction[1], 1.0, 1e-12);
        PLANETSIM_EXPECT_NEAR(test, optics.optical_depth[1], 7.5, 1e-9);
        PLANETSIM_EXPECT_NEAR(test, optics.reflectance[1], 0.15 * 7.5 / (2.0 + 0.15 * 7.5), 1e-12);
        PLANETSIM_EXPECT_NEAR(test, optics.transmission, 1.0 - optics.reflectance[1], 1e-12);
        PLANETSIM_EXPECT_NEAR(test, optics.longwave_depth[1], 6.5, 1e-9);
        ColumnRadiation cloudy = column.radiation;
        planetsim::add_cloud_longwave(cloudy, optics);
        PLANETSIM_EXPECT(test, cloudy.emissivity[1] > 0.998 &&
                                   cloudy.emissivity[1] > column.radiation.emissivity[1]);
        PLANETSIM_EXPECT(test, cloudy.emissivity[0] == column.radiation.emissivity[0]);
        PLANETSIM_EXPECT_NEAR(test, 1.0 - cloudy.emissivity[1],
                              (1.0 - column.radiation.emissivity[1]) * std::exp(-6.5), 1e-15);
    }

    // 4. V6: reflection rises with the water path, and with the fraction at a
    // fixed in-cloud path; random overlap multiplies the layers'
    // transmissions.
    {
        Column column;
        double previous = 0.0;
        bool rising = true;
        for (double path_g_m2 = 1.0; path_g_m2 <= 2'000.0; path_g_m2 *= 1.5) {
            for (std::size_t k = 0; k < n; ++k) {
                column.moisture.humidity_kg_kg[k] = 0.9 * column.saturation(k);
                column.moisture.cloud_water_kg_kg[k] = 0.0;
            }
            column.moisture.cloud_water_kg_kg[2] = path_g_m2 * 1e-3 / column.moisture.layer_mass_kg_m2;
            const double reflected = 1.0 - column.optics().transmission;
            rising = rising && reflected > previous;
            previous = reflected;
        }
        PLANETSIM_EXPECT(test, rising && previous < column.optics().fraction[2]);
        // Two layers: T = (1 − R₁)(1 − R₂).
        column.moisture.cloud_water_kg_kg[1] = 30e-3 / column.moisture.layer_mass_kg_m2;
        const auto optics = column.optics();
        PLANETSIM_EXPECT_NEAR(test, optics.transmission,
                              (1.0 - optics.reflectance[1]) * (1.0 - optics.reflectance[2]), 1e-15);
        // A larger fraction holding the same in-cloud path reflects more.
        const double w_c = column.parameters.cloud_in_cloud_water_path_kg_m2;
        double last = 0.0;
        bool more = true;
        for (const double f : {0.1, 0.3, 0.5, 0.8, 1.0}) {
            for (std::size_t k = 0; k < n; ++k) {
                column.moisture.cloud_water_kg_kg[k] = 0.0;
            }
            column.moisture.cloud_water_kg_kg[2] = f * w_c / column.moisture.layer_mass_kg_m2;
            const auto o = column.optics();
            PLANETSIM_EXPECT_NEAR(test, o.fraction[2], f, 1e-12);
            PLANETSIM_EXPECT_NEAR(test, o.optical_depth[2], 15.0, 1e-9);
            more = more && o.reflectance[2] > last;
            last = o.reflectance[2];
        }
        PLANETSIM_EXPECT(test, more);
    }

    return test.result();
}
