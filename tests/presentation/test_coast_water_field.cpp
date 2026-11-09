#include "sim/presentation/coast_water_field.hpp"

#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace planetsim;
using namespace planetsim::presentation;

int main() {
    planetsim::test::Context test;
    for (const std::uint32_t level : {4U, 6U}) {
        Scenario scenario;
        scenario.seed = 20260928;
        scenario.subdivision = level;
        const PlanetRun run(scenario);
        const auto mesh = std::make_shared<const PlanetMesh>(run.state().mesh());
        const auto terrain = run.terrain_snapshot();
        const TerrainRelief relief(mesh, terrain, scenario.seed);
        const CoastWaterField field(relief, terrain);
        const CoastWaterField again(relief, terrain);
        std::size_t depressions = 0, checked_dry = 0, plain = 0;
        double max_jump = 0.0;
        for (const auto& cell : mesh->cells()) {
            const auto index = cell.id.to_index();
            const bool depressed = terrain.land_fraction[index] == 1.0F &&
                terrain.hypsometry_m[index * hypsometry_quantile_count] < terrain.sea_level_m;
            depressions += depressed;
            if (depressed) {
                // Centre and samples towards each actual fan corner/edge.
                const auto corners = mesh->cell_corners(cell.id);
                for (std::size_t j = 0; j < corners.size(); ++j) {
                    const Vec3d a = mesh->corners_unit()[corners[j]];
                    const Vec3d b = mesh->corners_unit()[corners[(j + 1U) % corners.size()]];
                    for (const Vec3d& direction : {cell.center_unit,
                         normalized(cell.center_unit * 0.5 + a * 0.25 + b * 0.25)}) {
                        if (relief.sample(direction, cell.id).elevation_m < terrain.sea_level_m) {
                            ++checked_dry;
                            PLANETSIM_EXPECT(test, field.signed_water_m(direction, cell.id) < 0.0);
                        }
                    }
                }
            }
            if (index % 137U != 0U) continue;
            const Vec3d direction = cell.center_unit;
            const double kappa = field.connectivity(direction, cell.id);
            const double signed_water = field.signed_water_m(direction, cell.id);
            PLANETSIM_EXPECT(test, kappa >= 0.0 && kappa <= 1.0);
            PLANETSIM_EXPECT(test, signed_water == again.signed_water_m(direction, CellId{0}));
            if (kappa == 1.0) {
                ++plain;
                PLANETSIM_EXPECT(test, signed_water == terrain.sea_level_m -
                    relief.sample(direction).elevation_m);
            }
            const auto corners = mesh->cell_corners(cell.id);
            const Vec3d mid = normalized(cell.center_unit + mesh->corners_unit()[corners[0]]);
            const Vec3d across = normalized(cross(cell.center_unit, mid));
            const double step = relief.cell_spacing_rad() * 1e-7;
            const double left = field.signed_water_m(normalized(mid - across * step), cell.id);
            const double right = field.signed_water_m(normalized(mid + across * step), cell.id);
            max_jump = std::max(max_jump, std::abs(right - left));
        }
        const double step = relief.cell_spacing_rad() * 1e-7;
        for (std::size_t index = 0; index < mesh->edge_count(); index += 19U) {
            const auto& edge = mesh->edge(EdgeId{static_cast<std::uint32_t>(index)});
            const Vec3d a = mesh->cell(edge.first_cell).center_unit;
            const Vec3d b = mesh->cell(edge.second_cell).center_unit;
            const Vec3d middle = normalized(a + b);
            const Vec3d across = normalized(b - a);
            const double left = field.signed_water_m(normalized(middle - across * step), edge.first_cell);
            const double right = field.signed_water_m(normalized(middle + across * step), edge.second_cell);
            max_jump = std::max(max_jump, std::abs(right - left));
        }
        std::cout << "L" << level << " depression cells " << depressions
                  << ", below-sea depression samples " << checked_dry
                  << ", plain centres " << plain << ", max edge step m " << max_jump << '\n';
        PLANETSIM_EXPECT(test, field.depression_cells() == depressions);
        PLANETSIM_EXPECT(test, plain > 10U);
        PLANETSIM_EXPECT(test, max_jump < 1.0);
        if (level == 6U) PLANETSIM_EXPECT(test, checked_dry > 0U);
        TerrainSnapshot invalid = terrain;
        invalid.hypsometry_m.clear();
        bool rejected = false;
        try { const CoastWaterField bad(relief, invalid); (void)bad; }
        catch (const std::invalid_argument&) { rejected = true; }
        PLANETSIM_EXPECT(test, rejected);
    }
    return test.result();
}
