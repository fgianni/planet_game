#include "sim/planet/climatology/monthly_climatology.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/planet_state.hpp"
#include "tests/test_support.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

// Welford's running mean and population variance per month, against the
// direct two-pass computation; months are k mod 12; reset clears them.
void check_statistics(planetsim::test::Context& test) {
    const auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(2U, 6'371'000.0));
    planetsim::PlanetState state(mesh);
    const std::size_t cells = mesh->cell_count();
    const auto value = [](std::size_t cell, std::int64_t year, std::int64_t month) {
        return 250.0 + static_cast<double>(cell % 7U) + 3.0 * static_cast<double>(month) +
               0.5 * std::sin(1.3 * static_cast<double>(year) + static_cast<double>(cell));
    };
    constexpr std::int64_t years = 9;
    for (std::int64_t year = 0; year < years; ++year) {
        for (std::int64_t month = 0; month < 12; ++month) {
            for (std::size_t cell = 0; cell < cells; ++cell) {
                state.forcing().surface_temperature_K[cell] =
                    static_cast<float>(value(cell, year, month));
                state.slow().sea_ice_mass_kg_m2[cell] = static_cast<double>(year * 10 + month);
            }
            planetsim::accumulate_climatology(state, year * 12 + month, 4U);
        }
    }
    double worst_mean = 0.0;
    double worst_variance = 0.0;
    double worst_ice = 0.0;
    const auto& climatology = state.climatology();
    for (std::size_t month = 0; month < 12U; ++month) {
        for (std::size_t cell = 0; cell < cells; ++cell) {
            double mean = 0.0;
            for (std::int64_t year = 0; year < years; ++year) {
                mean += static_cast<float>(value(cell, year, static_cast<std::int64_t>(month)));
            }
            mean /= years;
            double variance = 0.0;
            for (std::int64_t year = 0; year < years; ++year) {
                const double d =
                    static_cast<float>(value(cell, year, static_cast<std::int64_t>(month))) - mean;
                variance += d * d;
            }
            variance /= years;
            worst_mean = std::max(
                worst_mean, std::abs(climatology.surface_temperature_mean_K.layer(month)[cell] - mean));
            worst_variance = std::max(
                worst_variance,
                std::abs(climatology.surface_temperature_variance_K2.layer(month)[cell] - variance));
            worst_ice = std::max(worst_ice,
                                 std::abs(climatology.sea_ice_mean_kg_m2.layer(month)[cell] -
                                          (40.0 + static_cast<double>(month))));
        }
        PLANETSIM_EXPECT(test, climatology.samples[month] == years);
    }
    PLANETSIM_EXPECT(test, worst_mean <= 1e-4);
    PLANETSIM_EXPECT(test, worst_variance <= 1e-4);
    PLANETSIM_EXPECT(test, worst_ice <= 1e-4);

    planetsim::reset_climatology(state);
    PLANETSIM_EXPECT(test, state.climatology().samples[3] == 0U);
    PLANETSIM_EXPECT(test, state.climatology().surface_temperature_mean_K.layer(3)[0] == 0.0F);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_statistics(test);
    return test.result();
}
