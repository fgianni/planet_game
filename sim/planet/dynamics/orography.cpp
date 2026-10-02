#include "sim/planet/dynamics/orography.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace planetsim {

Field2D<double> smooth_dynamics_orography(const PlanetMesh& mesh, const Field2D<double>& height_m,
                                          std::size_t passes, std::size_t worker_count) {
    if (height_m.size() != mesh.cell_count()) {
        throw std::invalid_argument("height size does not match the mesh");
    }
    constexpr double beta = 0.125;
    Field2D<double> current = height_m;
    Field2D<double> next(mesh.cell_count());
    for (std::size_t pass = 0; pass < passes; ++pass) {
        for_each_deterministic_block(
            mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                for (std::size_t i = block.begin; i < block.end; ++i) {
                    const auto& cell = mesh.cells()[i];
                    double change = 0.0;
                    for (const auto& cell_edge : mesh.cell_edges(cell.id)) {
                        const auto& edge = mesh.edge(cell_edge.edge);
                        change += beta * edge.length_m * edge.centroid_distance_m *
                                  (current[cell_edge.neighbor] - current[i]);
                    }
                    next[i] = current[i] + change / cell.area_m2;
                }
            });
        std::swap(current, next);
    }
    return current;
}

namespace {

[[nodiscard]] double steepest_step(const PlanetMesh& mesh, const Field2D<double>& height) {
    double steepest = 0.0;
    for (const auto& edge : mesh.edges()) {
        steepest = std::max(steepest, std::abs(height[edge.first_cell] - height[edge.second_cell]));
    }
    return steepest;
}

}  // namespace

DynamicsOrography limit_dynamics_orography_steps(const PlanetMesh& mesh,
                                                 const Field2D<double>& height_m,
                                                 double max_step_m, std::size_t max_passes,
                                                 std::size_t worker_count) {
    if (!(max_step_m > 0.0)) {
        throw std::invalid_argument("the orography step limit must be positive");
    }
    DynamicsOrography result{height_m, 0U, steepest_step(mesh, height_m)};
    while (result.max_step_m > max_step_m) {
        if (result.passes >= max_passes) {
            throw std::runtime_error("orography steps exceed the limit after the maximum passes");
        }
        result.height_m = smooth_dynamics_orography(mesh, result.height_m, 1U, worker_count);
        ++result.passes;
        result.max_step_m = steepest_step(mesh, result.height_m);
    }
    return result;
}

}  // namespace planetsim
