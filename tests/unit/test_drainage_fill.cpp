#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/terrain/drainage.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

[[nodiscard]] planetsim::Field3D<float> constant_cell_hypsometry(
    const planetsim::PlanetMesh& mesh,
    const std::vector<float>& elevation_m) {
    planetsim::Field3D<float> result(
        planetsim::hypsometry_quantile_count, mesh.cell_count());
    for (std::size_t layer = 0;
         layer < planetsim::hypsometry_quantile_count; ++layer) {
        for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
            result.at(
                layer,
                planetsim::CellId{static_cast<std::uint32_t>(cell)}) =
                elevation_m[cell];
        }
    }
    return result;
}

[[nodiscard]] bool are_neighbors(const planetsim::PlanetMesh& mesh,
                                 planetsim::CellId first,
                                 planetsim::CellId second) {
    const auto edges = mesh.cell_edges(first);
    return std::any_of(
        edges.begin(), edges.end(),
        [second](const planetsim::CellEdgeGeometry& edge) {
            return edge.neighbor == second;
        });
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const planetsim::PlanetMesh mesh =
        planetsim::make_icosphere(1U, 6'371'000.0);

    // One isolated below-sea low is dry because it is disconnected from the
    // ocean. The priority-flood raises it to its surrounding spill surface.
    {
        std::vector<float> elevation_m(mesh.cell_count(), 1'000.0F);
        const planetsim::CellId outlet{0U};
        elevation_m[outlet.to_index()] = -100.0F;

        planetsim::CellId low = planetsim::CellId::invalid();
        for (std::size_t index = mesh.cell_count(); index-- > 0U;) {
            const planetsim::CellId candidate{
                static_cast<std::uint32_t>(index)};
            if (candidate != outlet &&
                !are_neighbors(mesh, candidate, outlet)) {
                low = candidate;
                break;
            }
        }
        PLANETSIM_EXPECT(test, low.is_valid());
        elevation_m[low.to_index()] = -50.0F;

        const auto hypsometry =
            constant_cell_hypsometry(mesh, elevation_m);
        const planetsim::DrainageSurface surface =
            planetsim::fill_drainage_depressions(mesh, hypsometry, 0.0, 4U);

        PLANETSIM_EXPECT(test, surface.outlet_count == 1U);
        PLANETSIM_EXPECT(test, !surface.terminal_sink.is_valid());
        PLANETSIM_EXPECT(test, surface.outlet[outlet] == 1U);
        PLANETSIM_EXPECT(test, surface.land_fraction[outlet] == 0.0F);
        PLANETSIM_EXPECT(test, surface.land_fraction[low] == 1.0F);
        PLANETSIM_EXPECT(test, surface.depressions.size() == 1U);
        PLANETSIM_EXPECT(test, surface.depression_id[low] == 0U);
        PLANETSIM_EXPECT_NEAR(
            test, surface.drainage_elevation_m[low], -50.0, 0.0);
        PLANETSIM_EXPECT_NEAR(
            test, surface.filled_elevation_m[low], 1'000.0, 0.0);
        PLANETSIM_EXPECT_NEAR(
            test, surface.maximum_fill_depth_m, 1'050.0, 0.0);

        const planetsim::DepressionRecord& depression =
            surface.depressions.front();
        PLANETSIM_EXPECT(test, depression.minimum_cell == low);
        PLANETSIM_EXPECT(test, depression.cell_count == 1U);
        PLANETSIM_EXPECT_NEAR(
            test, depression.spill_level_m, 1'000.0, 0.0);
        PLANETSIM_EXPECT_NEAR(
            test, depression.maximum_fill_depth_m, 1'050.0, 0.0);
        PLANETSIM_EXPECT(
            test, are_neighbors(mesh, low, depression.spill_cell));

        for (std::size_t index = 0; index < mesh.cell_count(); ++index) {
            PLANETSIM_EXPECT(
                test, std::isfinite(surface.drainage_elevation_m[index]));
            PLANETSIM_EXPECT(
                test, std::isfinite(surface.filled_elevation_m[index]));
            PLANETSIM_EXPECT(
                test, surface.filled_elevation_m[index] >=
                          surface.drainage_elevation_m[index]);
            if (index != low.to_index()) {
                PLANETSIM_EXPECT(
                    test, surface.depression_id[index] ==
                              planetsim::no_depression);
            }
        }
    }

    // With no ocean, a flat planet chooses the lowest CellId as its only sink.
    {
        const std::vector<float> elevation_m(mesh.cell_count(), 5.0F);
        const auto hypsometry =
            constant_cell_hypsometry(mesh, elevation_m);
        const planetsim::DrainageSurface surface =
            planetsim::fill_drainage_depressions(
                mesh, hypsometry, -1'000.0, 2U);
        PLANETSIM_EXPECT(test, surface.outlet_count == 0U);
        PLANETSIM_EXPECT(
            test, surface.terminal_sink == planetsim::CellId{0U});
        PLANETSIM_EXPECT(test, surface.depressions.empty());
        PLANETSIM_EXPECT(test, surface.maximum_fill_depth_m == 0.0);
    }

    // Shape, ordering and finite-value failures are rejected.
    {
        planetsim::Field3D<float> wrong_shape(
            planetsim::hypsometry_quantile_count - 1U,
            mesh.cell_count(), 0.0F);
        PLANETSIM_EXPECT_THROWS(
            test, std::invalid_argument,
            planetsim::fill_drainage_depressions(
                mesh, wrong_shape, 0.0));

        std::vector<float> elevation_m(mesh.cell_count(), 0.0F);
        auto invalid = constant_cell_hypsometry(mesh, elevation_m);
        invalid.at(0U, planetsim::CellId{0U}) = 1.0F;
        PLANETSIM_EXPECT_THROWS(
            test, std::invalid_argument,
            planetsim::fill_drainage_depressions(mesh, invalid, 0.0));

        invalid.at(0U, planetsim::CellId{0U}) =
            -std::numeric_limits<float>::infinity();
        PLANETSIM_EXPECT_THROWS(
            test, std::invalid_argument,
            planetsim::fill_drainage_depressions(mesh, invalid, 0.0));
        PLANETSIM_EXPECT_THROWS(
            test, std::invalid_argument,
            planetsim::fill_drainage_depressions(
                mesh, constant_cell_hypsometry(mesh, elevation_m),
                std::numeric_limits<double>::quiet_NaN()));
    }

    return test.result();
}
