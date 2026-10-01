#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/surface_energy.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

using planetsim::PlanetPreset;

constexpr std::uint64_t test_seed = 1U;
constexpr int spin_up_years = 40;

struct Planet {
    std::shared_ptr<const planetsim::PlanetMesh> mesh;
    planetsim::PlanetParameters parameters = planetsim::PlanetParameters::earth_development();
    planetsim::SurfaceEnergyParameters surface;
    planetsim::PlanetState state;
    planetsim::SurfaceFractions fractions;

    Planet(std::shared_ptr<const planetsim::PlanetMesh> shared_mesh, PlanetPreset preset,
           float precipitation_kg_m2_s)
        : mesh(std::move(shared_mesh)),
          surface(planetsim::surface_energy_parameters_for(preset)),
          state(mesh) {
        static_cast<void>(planetsim::generate_terrain(
            state, test_seed, planetsim::geology_parameters_for(preset), 4U));
        fractions = planetsim::compute_surface_fractions(*mesh, state.slow().hypsometry_m,
                                                         state.slow().sea_level_m, 4U);
        planetsim::initialise_surface_temperatures(*mesh, state.slow(), parameters, surface, 4U);
        planetsim::initialise_cryosphere(*mesh, state.slow());
        auto& precipitation = state.forcing().prescribed_precipitation_kg_m2_s;
        for (std::size_t cell = 0; cell < precipitation.size(); ++cell) {
            precipitation[cell] = precipitation_kg_m2_s;
        }
    }

    planetsim::AnnualSurfaceSummary years(int count) {
        return planetsim::spin_up_surface_energy(state, parameters, surface, fractions, count,
                                                 4U);
    }
};

// Decadal statistics of the seasonal cycle: for each cover (ice north, ice
// south, snow north, snow south; area weighted by the albedo ramp, ADR-0008
// §4.2) the mean and the standard deviation over the decade of each year's
// largest and smallest value.
struct Decade {
    std::array<double, 4> max_mean{};
    std::array<double, 4> min_mean{};
    std::array<double, 4> min_deviation{};
    double ice_kg = 0.0;   // at the decade's end
    double snow_kg = 0.0;
    double mean_K = 0.0;
};

[[nodiscard]] Decade decade(Planet& planet) {
    constexpr int years = 10;
    Decade result;
    std::array<double, 4> min_squares{};
    for (int year = 0; year < years; ++year) {
        const auto summary = planet.years(1);
        for (std::size_t index = 0; index < 4U; ++index) {
            result.max_mean[index] += summary.cover_max_m2[index] / years;
            result.min_mean[index] += summary.cover_min_m2[index] / years;
            min_squares[index] += summary.cover_min_m2[index] * summary.cover_min_m2[index] / years;
        }
        result.ice_kg = summary.ice_kg;
        result.snow_kg = summary.snow_kg;
        result.mean_K += summary.mean_surface_temperature_K / years;
    }
    for (std::size_t index = 0; index < 4U; ++index) {
        result.min_deviation[index] = std::sqrt(
            std::max(0.0, min_squares[index] - result.min_mean[index] * result.min_mean[index]));
    }
    return result;
}

// The largest change of a decadal mean largest or smallest cover between two
// consecutive decades, relative to the cover's largest.
[[nodiscard]] double decadal_change(const Decade& first, const Decade& second) {
    double worst = 0.0;
    for (std::size_t index = 0; index < 4U; ++index) {
        const double scale = std::max(first.max_mean[index], second.max_mean[index]);
        if (scale > 0.0) {
            worst = std::max(
                {worst, std::abs(second.max_mean[index] - first.max_mean[index]) / scale,
                 std::abs(second.min_mean[index] - first.min_mean[index]) / scale});
        }
    }
    return worst;
}

void report(std::string_view label, const Decade& first, const Decade& second) {
    std::cout << label << " decadal_change=" << decadal_change(first, second)
              << " mean_K=" << second.mean_K << " ice_cover_north_km2="
              << second.min_mean[0] / 1e6 << "(sd " << second.min_deviation[0] / 1e6 << ").."
              << second.max_mean[0] / 1e6 << " ice_cover_south_km2=" << second.min_mean[1] / 1e6
              << "(sd " << second.min_deviation[1] / 1e6 << ").." << second.max_mean[1] / 1e6
              << " snow_cover_north_km2=" << second.min_mean[2] / 1e6 << ".."
              << second.max_mean[2] / 1e6
              << " ice_mass_drift_per_decade=" << (second.ice_kg - first.ice_kg) / second.ice_kg
              << " snow_mass_drift_per_decade="
              << (second.snow_kg > 0.0 ? (second.snow_kg - first.snow_kg) / second.snow_kg : 0.0)
              << '\n';
}

// ADR-0008 V7 (amended, §9.2): after spin-up the seasonal cycle of sea-ice
// and snow cover is stationary: the decadal means of each year's largest and
// smallest cover agree between consecutive decades within 2 %. The cycle is
// not exactly periodic (thin edge ice melts out or survives a summer, which
// amplifies small differences), so the interannual spread is recorded, as
// are the volume drifts (perennial ice and land snow have no limit yet).
void check_seasonal_cycle(planetsim::test::Context& test) {
    const auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(4U, 6'371'000.0));
    {
        Planet planet(mesh, PlanetPreset::earth_like, 0.0F);
        static_cast<void>(planet.years(spin_up_years));
        const auto first = decade(planet);
        const auto second = decade(planet);
        report("earth_like", first, second);
        PLANETSIM_EXPECT(test, decadal_change(first, second) <= 0.02);
        // Seasonal: the northern cover's mean smallest is under four fifths of
        // its mean largest.
        PLANETSIM_EXPECT(test, second.min_mean[0] < 0.8 * second.max_mean[0]);
    }
    {
        Planet planet(mesh, PlanetPreset::earth_like, 1e-5F);
        static_cast<void>(planet.years(spin_up_years));
        const auto first = decade(planet);
        const auto second = decade(planet);
        report("earth_like_snowfall", first, second);
        PLANETSIM_EXPECT(test, decadal_change(first, second) <= 0.02);
        PLANETSIM_EXPECT(test, second.max_mean[2] > second.min_mean[2]);
    }
    {
        Planet planet(mesh, PlanetPreset::aqua_planet, 0.0F);
        static_cast<void>(planet.years(spin_up_years));
        const auto first = decade(planet);
        const auto second = decade(planet);
        report("aqua_planet", first, second);
        PLANETSIM_EXPECT(test, decadal_change(first, second) <= 0.02);
    }
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_seasonal_cycle(test);
    return test.result();
}
