#include "sim/core/random/counter_rng.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/operators/finite_volume.hpp"
#include "tests/test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// ADR-0002 V6: operator results are bit-identical for any worker count.
int main() {
    planetsim::test::Context test;
    const auto mesh = planetsim::make_icosphere(5, 6'371'000.0);

    planetsim::Field2D<float> scalar(mesh.cell_count());
    for (const auto& cell : mesh.cells()) {
        scalar[cell.id] = static_cast<float>(planetsim::keyed_random_unit_double(
            11U, planetsim::RandomStreamId::validation, 0, cell.id.value()));
    }
    planetsim::EdgeField<float> flux(mesh.edge_count());
    for (std::size_t index = 0; index < mesh.edge_count(); ++index) {
        flux[planetsim::EdgeId{static_cast<planetsim::EdgeId::value_type>(index)}] =
            static_cast<float>(planetsim::keyed_random_unit_double(
                11U, planetsim::RandomStreamId::validation, 1,
                static_cast<std::uint32_t>(index)));
    }

    struct Outputs {
        planetsim::Field2D<float> east;
        planetsim::Field2D<float> north;
        planetsim::Field2D<float> divergence;
        planetsim::Field2D<float> laplacian;
    };
    const auto run = [&](std::size_t workers) {
        Outputs outputs{planetsim::Field2D<float>(mesh.cell_count()),
                        planetsim::Field2D<float>(mesh.cell_count()),
                        planetsim::Field2D<float>(mesh.cell_count()),
                        planetsim::Field2D<float>(mesh.cell_count())};
        planetsim::gradient(mesh, scalar, outputs.east, outputs.north, workers);
        planetsim::divergence(mesh, flux, outputs.divergence, workers);
        planetsim::laplacian(mesh, scalar, outputs.laplacian, workers);
        return outputs;
    };

    const auto reference = run(1U);
    for (const std::size_t workers : std::array<std::size_t, 3>{2U, 8U, 16U}) {
        const auto candidate = run(workers);
        bool identical = true;
        for (const auto& cell : mesh.cells()) {
            identical = identical && candidate.east[cell.id] == reference.east[cell.id] &&
                        candidate.north[cell.id] == reference.north[cell.id] &&
                        candidate.divergence[cell.id] == reference.divergence[cell.id] &&
                        candidate.laplacian[cell.id] == reference.laplacian[cell.id];
        }
        PLANETSIM_EXPECT(test, identical);
    }
    return test.result();
}
