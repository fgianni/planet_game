#include "sim/core/random/counter_rng.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/operators/finite_volume.hpp"
#include "sim/planet/operators/operator_validation.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace {

[[nodiscard]] planetsim::Field2D<double> random_cell_field(const planetsim::PlanetMesh& mesh,
                                                           std::uint32_t sample) {
    planetsim::Field2D<double> field(mesh.cell_count());
    for (const auto& cell : mesh.cells()) {
        field[cell.id] = planetsim::keyed_random_unit_double(
                             7U, planetsim::RandomStreamId::validation, 0, cell.id.value(), sample) -
                         0.5;
    }
    return field;
}

void check_nondivergent_and_global_balance(planetsim::test::Context& test) {
    // ADR-0002 V4: divergence of a discretely non-divergent field is rounding,
    // and any edge fluxes leave the global integral unchanged.
    for (std::uint32_t level = 0; level <= 6U; ++level) {
        const auto check =
            planetsim::check_nondivergent_flux(planetsim::make_icosphere(level, 6'371'000.0));
        PLANETSIM_EXPECT(test, check.relative_max_divergence <= 1.0e-14);
        PLANETSIM_EXPECT(test, check.relative_global_imbalance <= 1.0e-14);
    }
}

void check_constant_fields(planetsim::test::Context& test, const planetsim::PlanetMesh& mesh) {
    const planetsim::Field2D<double> constant(mesh.cell_count(), 287.5);
    planetsim::Field2D<double> east(mesh.cell_count(), 1.0);
    planetsim::Field2D<double> north(mesh.cell_count(), 1.0);
    planetsim::Field2D<double> result(mesh.cell_count(), 1.0);
    planetsim::gradient(mesh, constant, east, north);
    planetsim::laplacian(mesh, constant, result);
    bool all_zero = true;
    for (const auto& cell : mesh.cells()) {
        all_zero = all_zero && east[cell.id] == 0.0 && north[cell.id] == 0.0 &&
                   result[cell.id] == 0.0;
    }
    PLANETSIM_EXPECT(test, all_zero);

    const planetsim::EdgeField<double> zero_flux(mesh.edge_count(), 0.0);
    planetsim::divergence(mesh, zero_flux, result);
    bool divergence_zero = true;
    for (const auto& cell : mesh.cells()) {
        divergence_zero = divergence_zero && result[cell.id] == 0.0;
    }
    PLANETSIM_EXPECT(test, divergence_zero);
}

void check_laplacian_structure(planetsim::test::Context& test, const planetsim::PlanetMesh& mesh) {
    // Conservative: Σ A lap(phi) = 0. Dissipative: Σ A phi lap(phi) <= 0.
    const auto field = random_cell_field(mesh, 1U);
    planetsim::Field2D<double> result(mesh.cell_count());
    planetsim::laplacian(mesh, field, result);
    double net = 0.0;
    double gross = 0.0;
    double energy = 0.0;
    for (const auto& cell : mesh.cells()) {
        net += cell.area_m2 * result[cell.id];
        gross += cell.area_m2 * std::abs(result[cell.id]);
        energy += cell.area_m2 * field[cell.id] * result[cell.id];
    }
    PLANETSIM_EXPECT(test, std::abs(net) <= 1.0e-13 * gross);
    PLANETSIM_EXPECT(test, energy < 0.0);
}

void check_single_precision(planetsim::test::Context& test, const planetsim::PlanetMesh& mesh) {
    const auto field = random_cell_field(mesh, 2U);
    planetsim::Field2D<float> field_float(mesh.cell_count());
    for (const auto& cell : mesh.cells()) {
        field_float[cell.id] = static_cast<float>(field[cell.id]);
    }
    planetsim::Field2D<double> reference(mesh.cell_count());
    planetsim::Field2D<float> single(mesh.cell_count());
    planetsim::laplacian(mesh, field, reference);
    planetsim::laplacian(mesh, field_float, single);
    double max_reference = 0.0;
    double max_difference = 0.0;
    for (const auto& cell : mesh.cells()) {
        max_reference = std::max(max_reference, std::abs(reference[cell.id]));
        max_difference = std::max(
            max_difference, std::abs(static_cast<double>(single[cell.id]) - reference[cell.id]));
    }
    PLANETSIM_EXPECT(test, max_difference <= 1.0e-5 * max_reference);
}

void check_argument_validation(planetsim::test::Context& test, const planetsim::PlanetMesh& mesh) {
    const planetsim::Field2D<double> short_field(mesh.cell_count() - 1U);
    planetsim::Field2D<double> east(mesh.cell_count());
    planetsim::Field2D<double> north(mesh.cell_count());
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::gradient(mesh, short_field, east, north));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::laplacian(mesh, short_field, east));
    const planetsim::EdgeField<double> short_flux(mesh.edge_count() - 1U);
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::divergence(mesh, short_flux, east));

    const auto& edge = mesh.edge(planetsim::EdgeId{0});
    PLANETSIM_EXPECT(test,
                     planetsim::edge_orientation(mesh, edge.first_cell, planetsim::EdgeId{0}) ==
                         1.0);
    PLANETSIM_EXPECT(test,
                     planetsim::edge_orientation(mesh, edge.second_cell, planetsim::EdgeId{0}) ==
                         -1.0);
    planetsim::CellId unrelated{0};
    while (unrelated == edge.first_cell || unrelated == edge.second_cell) {
        unrelated = planetsim::CellId{unrelated.value() + 1U};
    }
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::edge_orientation(mesh, unrelated, planetsim::EdgeId{0}));
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_nondivergent_and_global_balance(test);

    const auto mesh = planetsim::make_icosphere(4, 6'371'000.0);
    check_constant_fields(test, mesh);
    check_laplacian_structure(test, mesh);
    check_single_precision(test, mesh);
    check_argument_validation(test, mesh);
    return test.result();
}
