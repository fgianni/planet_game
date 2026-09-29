#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/terrain/drainage.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "tests/test_support.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

[[nodiscard]] bool are_neighbors(const planetsim::PlanetMesh& mesh,
                                 planetsim::CellId first,
                                 planetsim::CellId second) {
    for (const auto& edge : mesh.cell_edges(first)) {
        if (edge.neighbor == second) {
            return true;
        }
    }
    return false;
}

void check_graph(planetsim::test::Context& test,
                 const planetsim::PlanetMesh& mesh,
                 const planetsim::DrainageState& drainage) {
    const std::size_t cell_count = mesh.cell_count();
    std::vector<std::uint32_t> seen(cell_count, 0U);
    std::uint32_t stamp = 0U;

    for (std::size_t start_index = 0; start_index < cell_count;
         ++start_index) {
        ++stamp;
        planetsim::CellId current{
            static_cast<std::uint32_t>(start_index)};
        std::size_t steps = 0U;
        while (drainage.downstream[current] !=
               planetsim::no_downstream) {
            PLANETSIM_EXPECT(test, seen[current.to_index()] != stamp);
            seen[current.to_index()] = stamp;
            const std::uint32_t downstream =
                drainage.downstream[current];
            PLANETSIM_EXPECT(test, downstream < cell_count);
            if (downstream >= cell_count) {
                break;
            }
            const planetsim::CellId next{downstream};
            PLANETSIM_EXPECT(
                test, are_neighbors(mesh, current, next));
            current = next;
            ++steps;
            PLANETSIM_EXPECT(test, steps < cell_count);
            if (steps >= cell_count) {
                break;
            }
        }
        const bool terminal =
            drainage.surface.outlet[current] != 0U ||
            drainage.surface.terminal_sink == current;
        PLANETSIM_EXPECT(test, terminal);
        PLANETSIM_EXPECT(
            test, drainage.basin_id[start_index] == current.value());
    }
}

void check_depressions(planetsim::test::Context& test,
                       const planetsim::DrainageState& drainage) {
    const std::size_t cell_count = drainage.downstream.size();
    for (std::size_t index = 0; index < cell_count; ++index) {
        PLANETSIM_EXPECT(
            test, std::isfinite(
                      drainage.surface.drainage_elevation_m[index]));
        PLANETSIM_EXPECT(
            test, std::isfinite(
                      drainage.surface.filled_elevation_m[index]));
        PLANETSIM_EXPECT(
            test, drainage.surface.filled_elevation_m[index] >=
                      drainage.surface.drainage_elevation_m[index]);
        const std::uint32_t depression_id =
            drainage.surface.depression_id[index];
        if (depression_id == planetsim::no_depression) {
            continue;
        }
        PLANETSIM_EXPECT(
            test, depression_id <
                      drainage.surface.depressions.size());
        if (depression_id >= drainage.surface.depressions.size()) {
            continue;
        }
        const planetsim::CellId spill =
            drainage.surface.depressions[depression_id].spill_cell;
        planetsim::CellId current{
            static_cast<std::uint32_t>(index)};
        std::size_t steps = 0U;
        while (current != spill && steps < cell_count) {
            const std::uint32_t downstream =
                drainage.downstream[current];
            PLANETSIM_EXPECT(
                test, downstream != planetsim::no_downstream);
            if (downstream == planetsim::no_downstream) {
                break;
            }
            current = planetsim::CellId{downstream};
            ++steps;
        }
        PLANETSIM_EXPECT(test, current == spill);
    }
}

[[nodiscard]] planetsim::DrainageState make_drainage(
    std::uint32_t level,
    planetsim::PlanetPreset preset,
    std::uint64_t seed) {
    const auto mesh =
        std::make_shared<const planetsim::PlanetMesh>(
            planetsim::make_icosphere(level, 6'371'000.0));
    planetsim::PlanetState state(mesh);
    static_cast<void>(planetsim::generate_terrain(
        state, seed, planetsim::geology_parameters_for(preset), 4U));
    return planetsim::generate_drainage(
        *mesh, state.slow().hypsometry_m,
        state.slow().sea_level_m, 4U);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    constexpr std::uint64_t seed = 20'260'929U;

    const auto mesh =
        std::make_shared<const planetsim::PlanetMesh>(
            planetsim::make_icosphere(4U, 6'371'000.0));
    planetsim::PlanetState state(mesh);
    static_cast<void>(planetsim::generate_terrain(
        state, seed,
        planetsim::geology_parameters_for(
            planetsim::PlanetPreset::earth_like),
        4U));
    const planetsim::DrainageState earth =
        planetsim::generate_drainage(
            *mesh, state.slow().hypsometry_m,
            state.slow().sea_level_m, 4U);

    check_graph(test, *mesh, earth);
    check_depressions(test, earth);
    PLANETSIM_EXPECT(
        test, earth.diagnostics.invalid_downstream_count == 0U);
    PLANETSIM_EXPECT(test, earth.diagnostics.cycle_count == 0U);
    PLANETSIM_EXPECT(
        test, earth.diagnostics.unreachable_cell_count == 0U);
    PLANETSIM_EXPECT(
        test,
        earth.diagnostics.catchment_closure_relative_error <= 1e-12);
    PLANETSIM_EXPECT(
        test, earth.diagnostics.basin_count ==
                  earth.surface.outlet_count);
    for (std::size_t index = 0; index < mesh->cell_count(); ++index) {
        PLANETSIM_EXPECT(
            test, std::isfinite(earth.catchment_area_m2[index]));
        PLANETSIM_EXPECT(
            test, earth.catchment_area_m2[index] >= 0.0F);
        PLANETSIM_EXPECT(
            test, earth.basin_id[index] != planetsim::no_basin);
    }

    const planetsim::DrainageState rock = make_drainage(
        2U, planetsim::PlanetPreset::dead_rock, seed);
    PLANETSIM_EXPECT(test, rock.surface.outlet_count == 0U);
    PLANETSIM_EXPECT(test, rock.surface.terminal_sink.is_valid());
    PLANETSIM_EXPECT(test, rock.diagnostics.basin_count == 1U);
    PLANETSIM_EXPECT(
        test, rock.diagnostics.catchment_closure_relative_error <=
                  1e-12);
    for (const std::uint32_t basin : rock.basin_id.values()) {
        PLANETSIM_EXPECT(
            test, basin == rock.surface.terminal_sink.value());
    }

    const planetsim::DrainageState aqua = make_drainage(
        2U, planetsim::PlanetPreset::aqua_planet, seed);
    PLANETSIM_EXPECT(
        test, aqua.surface.outlet_count == aqua.downstream.size());
    PLANETSIM_EXPECT(
        test, aqua.diagnostics.total_routed_land_area_m2 == 0.0);
    PLANETSIM_EXPECT(
        test, aqua.diagnostics.terminal_catchment_area_m2 == 0.0);
    PLANETSIM_EXPECT(
        test, aqua.diagnostics.catchment_closure_relative_error == 0.0);
    for (const std::uint32_t downstream : aqua.downstream.values()) {
        PLANETSIM_EXPECT(
            test, downstream == planetsim::no_downstream);
    }

    return test.result();
}
