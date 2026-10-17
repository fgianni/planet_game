#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/atmosphere/atmosphere_column.hpp"
#include "sim/planet/atmosphere/saturation.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

// Cloud radiation (ADR-0023 §4.4, V6; task M8-04) on single columns: the
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
        return planetsim::cloud_optics(parameters, radiation, moisture,
                                       std::span<const double>(temperature.data(), n));
    }
};

}  // namespace

int main() {
    planetsim::test::Context test;

    // 1. The fraction (Sundqvist et al. 1989): zero up to RH_c, the formula
    // above it, one at saturation, cloud water counted in RH. A stable
    // column (an inversion over the bottom layer) has no convective floor.
    {
        Column column;
        column.temperature = {270.0, 275.0, 260.0, 240.0};
        for (const double rh : {0.5, 0.8, 0.85, 0.95, 0.99, 1.0, 1.2}) {
            for (std::size_t k = 0; k < n; ++k) {
                column.moisture.humidity_kg_kg[k] = std::min(rh, 1.0) * column.saturation(k);
                column.moisture.cloud_water_kg_kg[k] = std::max(0.0, rh - 1.0) * column.saturation(k);
            }
            const auto optics = column.optics();
            const double expected =
                rh >= 1.0 ? 1.0 : rh > 0.8 ? 1.0 - std::sqrt((1.0 - rh) / 0.2) : 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                PLANETSIM_EXPECT_NEAR(test, optics.fraction[k], expected, 1e-12);
            }
            PLANETSIM_EXPECT_NEAR(test, optics.column_fraction,
                                  1.0 - std::pow(1.0 - expected, static_cast<double>(n)), 1e-12);
        }
        // Cloud water raises RH: 0.7 of vapour and 0.2 of cloud water is RH 0.9.
        for (std::size_t k = 0; k < n; ++k) {
            column.moisture.humidity_kg_kg[k] = 0.7 * column.saturation(k);
            column.moisture.cloud_water_kg_kg[k] = 0.2 * column.saturation(k);
        }
        PLANETSIM_EXPECT_NEAR(test, column.optics().fraction[1], 1.0 - std::sqrt(0.5), 1e-12);
    }

    // 2. A convecting column carries at least f_conv in its convecting
    // layers, by the trigger's weights, even where it is dry.
    {
        Column column;
        column.temperature = {300.0, 280.0, 258.0, 225.0};
        for (std::size_t k = 0; k < n; ++k) {
            column.moisture.humidity_kg_kg[k] = (k == 0U ? 0.8 : 0.3) * column.saturation(k);
        }
        const auto optics = column.optics();
        PLANETSIM_EXPECT_NEAR(test, optics.fraction[0], 0.2, 1e-12);
        PLANETSIM_EXPECT(test, optics.fraction[1] > 0.0 && optics.fraction[1] <= 0.2);
        // No cloud water: nothing to reflect or absorb.
        PLANETSIM_EXPECT(test, optics.transmission == 1.0 && optics.longwave_depth[1] == 0.0);
        std::cout << "convective floor: f = " << optics.fraction[0] << ' ' << optics.fraction[1]
                  << ' ' << optics.fraction[2] << ' ' << optics.fraction[3] << '\n';
    }

    // 3. Optics (Stephens 1978): an overcast layer with 50 g/m² of cloud
    // water has τ = 3 · 0.05 / (2 · 1000 · 10 µm) = 7.5, reflects
    // 0.15 · 7.5 / (2 + 0.15 · 7.5) = 0.36, and its longwave depth
    // 0.13 m²/g · 50 g/m² = 6.5 makes it nearly black.
    {
        Column column;
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

    // 4. V6: reflection rises with the water path at a fixed fraction and
    // with the fraction at a fixed in-cloud path; random overlap multiplies
    // the layers' transmissions.
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
        double last = 0.0;
        bool more = true;
        for (const double rh : {0.82, 0.86, 0.9, 0.95, 0.99}) {
            for (std::size_t k = 0; k < n; ++k) {
                column.moisture.humidity_kg_kg[k] = 0.0;
                column.moisture.cloud_water_kg_kg[k] = 0.0;
            }
            const double f = 1.0 - std::sqrt((1.0 - rh) / 0.2);
            const double water = f * 50e-3 / column.moisture.layer_mass_kg_m2;
            column.moisture.cloud_water_kg_kg[2] = water;
            column.moisture.humidity_kg_kg[2] = rh * column.saturation(2) - water;
            const auto o = column.optics();
            PLANETSIM_EXPECT_NEAR(test, o.optical_depth[2], 7.5, 1e-6);
            more = more && o.reflectance[2] > last;
            last = o.reflectance[2];
        }
        PLANETSIM_EXPECT(test, more);
    }

    return test.result();
}
