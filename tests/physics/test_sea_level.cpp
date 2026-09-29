#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"
#include "sim/planet/terrain/terrain_diagnostics.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>

namespace {

constexpr std::uint64_t test_seed = 20260928ULL;

// A7 (ADR-0005 V1): finite, non-decreasing hypsometry.
void check_hypsometry(planetsim::test::Context& test, const planetsim::PlanetState& state) {
    std::size_t violations = 0;
    for (const auto& cell : state.mesh().cells()) {
        const auto quantiles = planetsim::cell_hypsometry(state.slow().hypsometry_m, cell.id);
        if (!std::is_sorted(quantiles.begin(), quantiles.end()) ||
            !std::all_of(quantiles.begin(), quantiles.end(),
                         [](float value) { return std::isfinite(value); })) {
            ++violations;
        }
    }
    PLANETSIM_EXPECT(test, violations == 0U);
}

// A8 (ADR-0005 V2): fractions in [0, 1]; land non-increasing as the sea
// rises; no ocean area outside the ocean component.
void check_fractions(planetsim::test::Context& test, const planetsim::PlanetState& state) {
    const auto& mesh = state.mesh();
    double previous_land = 1.0;
    for (double level = -8'000.0; level <= 6'000.0; level += 250.0) {
        const auto fractions =
            planetsim::compute_surface_fractions(mesh, state.slow().hypsometry_m, level, 4U);
        PLANETSIM_EXPECT(test, fractions.land_area_fraction <= previous_land);
        previous_land = fractions.land_area_fraction;
        for (std::size_t index = 0; index < mesh.cell_count(); ++index) {
            const float land = fractions.land_fraction[index];
            const float ocean = fractions.ocean_fraction[index];
            PLANETSIM_EXPECT(test, land >= 0.0F && land <= 1.0F && ocean >= 0.0F && ocean <= 1.0F);
            PLANETSIM_EXPECT(test, fractions.ocean_connected[index] != 0U || ocean == 0.0F);
        }
        PLANETSIM_EXPECT(test, std::abs(planetsim::land_area_fraction(
                                            mesh, state.slow().hypsometry_m, level, 4U) -
                                        fractions.land_area_fraction) == 0.0);
    }
}

// A9 (ADR-0005 V3): sea-level solve for several targets.
void check_solve(planetsim::test::Context& test, const planetsim::PlanetState& state) {
    for (const double target : {0.10, 0.29, 0.50}) {
        const auto solution =
            planetsim::solve_sea_level(state.mesh(), state.slow().hypsometry_m, target, 4U);
        std::cout << "  L" << state.mesh().subdivision() << " target " << target
                  << ": sea_level_m=" << solution.sea_level_m
                  << " achieved=" << solution.achieved_land_fraction
                  << " error=" << solution.achieved_land_fraction - target
                  << " jump=" << (solution.target_in_jump ? "yes" : "no");
        if (solution.target_in_jump) {
            std::cout << " (above " << solution.land_fraction_above_jump << ", connectivity "
                      << (solution.jump_changes_ocean_connectivity ? "yes" : "no") << ")";
        }
        std::cout << '\n';
        if (solution.target_in_jump) {
            PLANETSIM_EXPECT(test, solution.achieved_land_fraction >= target &&
                                       solution.land_fraction_above_jump < target);
        } else {
            PLANETSIM_EXPECT(test, std::abs(solution.achieved_land_fraction - target) <=
                                       planetsim::sea_level_target_tolerance);
        }
    }
}

}  // namespace

int main() {
    planetsim::test::Context test;

    for (const std::uint32_t level : {4U, 5U}) {
        const auto mesh = std::make_shared<const planetsim::PlanetMesh>(
            planetsim::make_icosphere(level, 6'371'000.0));

        planetsim::PlanetState earth(mesh);
        const auto generation = planetsim::generate_terrain(
            earth, test_seed, planetsim::geology_parameters_for(planetsim::PlanetPreset::earth_like),
            4U);
        PLANETSIM_EXPECT(test, earth.slow().sea_level_m == generation.sea_level.sea_level_m);
        // Re-datum: the generated sea level is 0 m and elevations are heights
        // above it, so re-solving on the stored terrain lands back at 0 m.
        PLANETSIM_EXPECT(test, earth.slow().sea_level_m == 0.0);
        PLANETSIM_EXPECT(test, generation.sea_level.datum_shift_m != 0.0);
        const auto resolved = planetsim::solve_sea_level(*mesh, earth.slow().hypsometry_m, 0.29, 4U);
        PLANETSIM_EXPECT(test, std::abs(resolved.sea_level_m) <= 0.5);
        check_hypsometry(test, earth);
        check_fractions(test, earth);
        check_solve(test, earth);

        const auto diagnostics = planetsim::compute_terrain_diagnostics(
            *mesh, generation.geology, earth.slow().hypsometry_m, earth.slow().sea_level_m, 4U);
        std::cout << "L" << level << " earth_like: land=" << diagnostics.land_area_fraction
                  << " sea_level_m=" << diagnostics.sea_level_m
                  << " ocean_shallower_than_200m=" << diagnostics.ocean_shallower_than_200m_fraction
                  << " ocean_deeper_than_4000m=" << diagnostics.ocean_deeper_than_4000m_fraction
                  << " ocean_elevation_mean_m=" << diagnostics.ocean_elevation_mean_m
                  << " ocean_elevation_std_m=" << diagnostics.ocean_elevation_std_m
                  << " inland_depressions=" << diagnostics.inland_depression_count << '\n';
        PLANETSIM_EXPECT(test, std::abs(diagnostics.land_area_fraction - 0.29) <= 1e-4 ||
                                   generation.sea_level.target_in_jump);
        if (level == 5U) {
            // A10: non-flat bathymetry.
            PLANETSIM_EXPECT(test, diagnostics.ocean_shallower_than_200m_fraction > 0.0);
            PLANETSIM_EXPECT(test, diagnostics.ocean_deeper_than_4000m_fraction > 0.0);
            PLANETSIM_EXPECT(test, diagnostics.ocean_elevation_std_m >= 500.0);
        }

        planetsim::PlanetState aqua(mesh);
        const auto aqua_generation = planetsim::generate_terrain(
            aqua, test_seed, planetsim::geology_parameters_for(planetsim::PlanetPreset::aqua_planet),
            4U);
        const auto aqua_fractions = planetsim::compute_surface_fractions(
            *mesh, aqua.slow().hypsometry_m, aqua.slow().sea_level_m, 4U);
        PLANETSIM_EXPECT(test, aqua_generation.sea_level.achieved_land_fraction == 0.0);
        PLANETSIM_EXPECT(test, aqua_fractions.land_area_fraction == 0.0);
        PLANETSIM_EXPECT(test, aqua_fractions.ocean_cell_count == mesh->cell_count());

        planetsim::PlanetState rock(mesh);
        const auto rock_generation = planetsim::generate_terrain(
            rock, test_seed, planetsim::geology_parameters_for(planetsim::PlanetPreset::dead_rock),
            4U);
        const auto rock_fractions = planetsim::compute_surface_fractions(
            *mesh, rock.slow().hypsometry_m, rock.slow().sea_level_m, 4U);
        PLANETSIM_EXPECT(test, rock_generation.sea_level.achieved_land_fraction == 1.0);
        PLANETSIM_EXPECT(test, rock_fractions.land_area_fraction == 1.0);
        PLANETSIM_EXPECT(test, rock_fractions.ocean_cell_count == 0U);
        PLANETSIM_EXPECT(test, rock_fractions.below_sea_level_components == 0U);
    }

    return test.result();
}
