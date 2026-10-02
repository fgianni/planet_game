#include "sim/core/random/counter_rng.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/operators/c_grid.hpp"
#include "sim/planet/operators/operator_validation.hpp"
#include "tests/test_support.hpp"

#include <cmath>
#include <cstdint>

// ADR-0011 V1 (task M6-01): the discrete identities of the C-grid, and
// bit-identical operators on any worker count.
int main() {
    planetsim::test::Context test;

    for (std::uint32_t level = 2; level <= 6U; ++level) {
        const auto mesh = planetsim::make_icosphere(level, 6'371'000.0);
        const auto grid = planetsim::CGridGeometry::build(mesh);
        PLANETSIM_EXPECT(test, grid.corner_count() == mesh.corner_count());
        PLANETSIM_EXPECT(test, grid.edge_count() == mesh.edge_count());

        const auto validation = planetsim::validate_c_grid_operators(mesh);
        const auto& checks = validation.identities;
        // The TRiSK identities hold to rounding.
        PLANETSIM_EXPECT(test, checks.weight_antisymmetry <= 1.0e-14);
        PLANETSIM_EXPECT(test, checks.coriolis_work <= 1.0e-14);
        PLANETSIM_EXPECT(test, checks.curl_of_gradient <= 1.0e-14);
        PLANETSIM_EXPECT(test, checks.perp_vorticity <= 1.0e-14);
        // The kites tile the cells and the dual triangles up to the rounding
        // of the mesh's corner positions: the circumcentre is the direction
        // of a cross product of differences of length h, accurate to ε/h, so
        // the misfit's relative area grows as 4^L (1.7e-12 at L6).
        const double scaled_rounding = 1.0e-15 * std::pow(4.0, static_cast<double>(level));
        PLANETSIM_EXPECT(test, checks.kite_cell_area <= scaled_rounding);
        PLANETSIM_EXPECT(test, checks.kite_triangle_area <= scaled_rounding);

        for (const auto& corner : grid.corners()) {
            double weights = 0.0;
            for (std::size_t k = 0; k < 3U; ++k) {
                PLANETSIM_EXPECT(test, corner.kite_area_m2[k] > 0.0);
                weights += corner.interpolation_weight[k];
            }
            PLANETSIM_EXPECT_NEAR(test, weights, 1.0, 1.0e-12);
        }
    }

    // Determinism: every operator is bit-identical on 1, 2 and 8 workers.
    const auto mesh = planetsim::make_icosphere(5U, 6'371'000.0);
    const auto grid = planetsim::CGridGeometry::build(mesh);
    planetsim::EdgeField<double> velocity(mesh.edge_count());
    for (std::size_t index = 0; index < mesh.edge_count(); ++index) {
        velocity.values()[index] =
            planetsim::keyed_random_unit_double(0x0611U, planetsim::RandomStreamId::validation, 1,
                                                static_cast<std::uint32_t>(index)) -
            0.5;
    }
    planetsim::Field2D<double> scalar(mesh.cell_count());
    for (const auto& cell : mesh.cells()) {
        scalar[cell.id] = cell.center_unit.x * cell.center_unit.y + cell.center_unit.z;
    }

    struct Outputs {
        planetsim::EdgeField<double> perp;
        planetsim::Field2D<double> vorticity;
        planetsim::Field2D<double> kinetic;
        planetsim::Field2D<double> east;
        planetsim::Field2D<double> north;
        planetsim::EdgeField<double> normal;
        planetsim::Field2D<double> conservative_corner;
        planetsim::Field2D<double> interpolated_corner;
        planetsim::EdgeField<double> tangential;
    };
    const auto run = [&](std::size_t workers) {
        Outputs out{planetsim::EdgeField<double>(mesh.edge_count()),
                    planetsim::Field2D<double>(grid.corner_count()),
                    planetsim::Field2D<double>(mesh.cell_count()),
                    planetsim::Field2D<double>(mesh.cell_count()),
                    planetsim::Field2D<double>(mesh.cell_count()),
                    planetsim::EdgeField<double>(mesh.edge_count()),
                    planetsim::Field2D<double>(grid.corner_count()),
                    planetsim::Field2D<double>(grid.corner_count()),
                    planetsim::EdgeField<double>(mesh.edge_count())};
        planetsim::tangential_velocity(mesh, grid, velocity, out.perp, workers);
        planetsim::relative_vorticity(mesh, grid, velocity, out.vorticity, workers);
        planetsim::kinetic_energy(mesh, velocity, out.kinetic, workers);
        planetsim::reconstruct_cell_vector(mesh, grid, velocity, out.east, out.north, workers);
        planetsim::normal_gradient(mesh, grid, scalar, out.normal, workers);
        planetsim::cell_to_corner(grid, scalar, out.conservative_corner, workers);
        planetsim::interpolate_to_corner(grid, scalar, out.interpolated_corner, workers);
        planetsim::tangential_gradient(mesh, grid, out.interpolated_corner, out.tangential,
                                       workers);
        return out;
    };
    const auto equal = [](auto first, auto second) {
        if (first.size() != second.size()) {
            return false;
        }
        for (std::size_t index = 0; index < first.size(); ++index) {
            if (first[index] != second[index]) {
                return false;
            }
        }
        return true;
    };
    const Outputs serial = run(1U);
    for (const std::size_t workers : {2U, 8U}) {
        const Outputs parallel = run(workers);
        PLANETSIM_EXPECT(test, equal(serial.perp.values(), parallel.perp.values()));
        PLANETSIM_EXPECT(test, equal(serial.vorticity.values(), parallel.vorticity.values()));
        PLANETSIM_EXPECT(test, equal(serial.kinetic.values(), parallel.kinetic.values()));
        PLANETSIM_EXPECT(test, equal(serial.east.values(), parallel.east.values()));
        PLANETSIM_EXPECT(test, equal(serial.north.values(), parallel.north.values()));
        PLANETSIM_EXPECT(test, equal(serial.normal.values(), parallel.normal.values()));
        PLANETSIM_EXPECT(test, equal(serial.conservative_corner.values(),
                                     parallel.conservative_corner.values()));
        PLANETSIM_EXPECT(test, equal(serial.interpolated_corner.values(),
                                     parallel.interpolated_corner.values()));
        PLANETSIM_EXPECT(test, equal(serial.tangential.values(), parallel.tangential.values()));
    }

    return test.result();
}
