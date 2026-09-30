#include "sim/planet/surface/heat_transport.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace planetsim {
namespace {

// Σ_e (l_e / d_e) (x_n − x_c) for one cell: A_c times the Laplacian.
[[nodiscard]] double edge_sum(const PlanetMesh& mesh, std::size_t cell,
                              const Field2D<double>& values) {
    const CellId id{static_cast<CellId::value_type>(cell)};
    const double own = values[cell];
    double sum = 0.0;
    for (const auto& cell_edge : mesh.cell_edges(id)) {
        const auto& edge = mesh.edge(cell_edge.edge);
        sum += (values[cell_edge.neighbor] - own) * edge.length_m / edge.centroid_distance_m;
    }
    return sum;
}

[[nodiscard]] double max_abs(const PlanetMesh& mesh, const Field2D<double>& values,
                             std::size_t worker_count) {
    return reduce_deterministic_blocks<double>(
        mesh.blocks(), worker_count, 0.0,
        [&](std::size_t, const CellBlock& block) {
            double largest = 0.0;
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                largest = std::max(largest, std::abs(values[cell]));
            }
            return largest;
        },
        [](double a, double b) { return std::max(a, b); });
}

// The mesh's cell–edge incidence flattened for the solver: for cell c, the
// neighbours and weights l_e / d_e in [offset[c], offset[c + 1]).
struct Stencil {
    std::vector<std::size_t> offset;
    std::vector<std::size_t> neighbour;
    std::vector<double> weight;
    std::vector<double> weight_sum;

    explicit Stencil(const PlanetMesh& mesh) : offset(mesh.cell_count() + 1U, 0U) {
        for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
            const CellId id{static_cast<CellId::value_type>(cell)};
            double sum = 0.0;
            for (const auto& cell_edge : mesh.cell_edges(id)) {
                const auto& edge = mesh.edge(cell_edge.edge);
                neighbour.push_back(cell_edge.neighbor.to_index());
                weight.push_back(edge.length_m / edge.centroid_distance_m);
                sum += weight.back();
            }
            offset[cell + 1U] = neighbour.size();
            weight_sum.push_back(sum);
        }
    }
};

// Jacobi-preconditioned conjugate gradients on
//   (A_c / s_c) x_c + K Σ_e w_e (x_c − x_n) = rhs_c
// over the cells with s_c > 0; the others hold x_c = 0. Symmetric positive
// definite: a positive diagonal plus K times a graph Laplacian. Serial, in
// cell order, so every sum has a fixed order.
int solve_newton_system(const PlanetMesh& mesh, const Stencil& stencil, double conductance_W_K,
                        const Field2D<double>& slope, const Field2D<double>& rhs,
                        Field2D<double>& solution, const ImplicitTransportSettings& settings) {
    const std::size_t cells = mesh.cell_count();
    std::vector<double> diagonal(cells, 0.0);
    std::vector<double> local(cells, 0.0);   // A_c / s_c, or 0 on held cells
    for (std::size_t cell = 0; cell < cells; ++cell) {
        if (slope[cell] > 0.0) {
            local[cell] = mesh.cells()[cell].area_m2 / slope[cell];
            diagonal[cell] = local[cell] + conductance_W_K * stencil.weight_sum[cell];
        }
    }
    const auto apply = [&](const std::vector<double>& input, std::vector<double>& output) {
        for (std::size_t cell = 0; cell < cells; ++cell) {
            if (!(diagonal[cell] > 0.0)) {
                output[cell] = 0.0;
                continue;
            }
            double sum = 0.0;
            for (std::size_t k = stencil.offset[cell]; k < stencil.offset[cell + 1U]; ++k) {
                sum += (input[stencil.neighbour[k]] - input[cell]) * stencil.weight[k];
            }
            output[cell] = local[cell] * input[cell] - conductance_W_K * sum;
        }
    };
    const auto dot = [cells](const std::vector<double>& first, const std::vector<double>& second) {
        double sum = 0.0;
        for (std::size_t cell = 0; cell < cells; ++cell) {
            sum += first[cell] * second[cell];
        }
        return sum;
    };

    std::vector<double> x(cells, 0.0);
    std::vector<double> residual(cells, 0.0);
    std::vector<double> preconditioned(cells, 0.0);
    for (std::size_t cell = 0; cell < cells; ++cell) {
        if (diagonal[cell] > 0.0) {
            residual[cell] = rhs[cell];
            preconditioned[cell] = residual[cell] / diagonal[cell];
        }
    }
    std::vector<double> direction = preconditioned;
    std::vector<double> product(cells, 0.0);
    double rho = dot(residual, preconditioned);
    const double target = settings.cg_relative_tolerance * std::sqrt(dot(residual, residual));
    int iteration = 0;
    while (iteration < settings.max_cg_iterations && std::sqrt(dot(residual, residual)) > target &&
           rho > 0.0) {
        apply(direction, product);
        const double alpha = rho / dot(direction, product);
        for (std::size_t cell = 0; cell < cells; ++cell) {
            x[cell] += alpha * direction[cell];
            residual[cell] -= alpha * product[cell];
            preconditioned[cell] = diagonal[cell] > 0.0 ? residual[cell] / diagonal[cell] : 0.0;
        }
        const double next_rho = dot(residual, preconditioned);
        const double beta = next_rho / rho;
        rho = next_rho;
        for (std::size_t cell = 0; cell < cells; ++cell) {
            direction[cell] = preconditioned[cell] + beta * direction[cell];
        }
        ++iteration;
    }
    solution = Field2D<double>(x);
    return iteration;
}

}  // namespace

void diffusion_source(const PlanetMesh& mesh, double conductance_W_K,
                      const Field2D<double>& temperature_K, Field2D<double>& source_W_m2,
                      std::size_t worker_count) {
    if (temperature_K.size() != mesh.cell_count()) {
        throw std::invalid_argument("transport temperature does not match the mesh");
    }
    source_W_m2 = Field2D<double>(mesh.cell_count(), 0.0);
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                source_W_m2[cell] = conductance_W_K * edge_sum(mesh, cell, temperature_K) /
                                    mesh.cells()[cell].area_m2;
            }
        });
}

ImplicitTransportResult solve_implicit_transport(const PlanetMesh& mesh, double conductance_W_K,
                                                 const Field2D<double>& source_floor_W_m2,
                                                 const TransportResponse& response,
                                                 const ImplicitTransportSettings& settings,
                                                 std::size_t /*worker_count*/) {
    // The vector work here is light and called thousands of times per step;
    // it runs on one worker, which gives the same bits as any other count
    // (fixed block order). The response callback parallelises the tile solves.
    constexpr std::size_t worker_count = 1U;
    const std::size_t cells = mesh.cell_count();
    if (!std::isfinite(conductance_W_K) || !(conductance_W_K > 0.0)) {
        throw std::invalid_argument("transport conductance must be finite and positive");
    }
    if (source_floor_W_m2.size() != cells) {
        throw std::invalid_argument("transport floor does not match the mesh");
    }

    const Stencil stencil(mesh);
    ImplicitTransportResult result;
    Field2D<double> source(cells, 0.0);
    for (std::size_t cell = 0; cell < cells; ++cell) {
        source[cell] = std::max(0.0, source_floor_W_m2[cell]);
    }
    Field2D<double> mean(cells, 0.0);
    Field2D<double> slope(cells, 0.0);
    Field2D<double> diffusion(cells, 0.0);
    Field2D<double> residual(cells, 0.0);
    Field2D<double> rhs(cells, 0.0);
    Field2D<double> delta_mean(cells, 0.0);
    Field2D<double> delta_source(cells, 0.0);
    Field2D<double> trial(cells, 0.0);
    Field2D<double> trial_mean(cells, 0.0);
    Field2D<double> trial_slope(cells, 0.0);

    // F(h) = h − K ∇² T̄(h), and the merit Σ A F².
    const auto evaluate = [&](const Field2D<double>& at, Field2D<double>& at_mean,
                              Field2D<double>& at_slope, Field2D<double>& at_residual) {
        response(at, at_mean, at_slope);
        diffusion_source(mesh, conductance_W_K, at_mean, diffusion, worker_count);
        return reduce_deterministic_blocks<double>(
            mesh.blocks(), worker_count, 0.0,
            [&](std::size_t, const CellBlock& block) {
                double merit = 0.0;
                for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                    at_residual[cell] = at[cell] - diffusion[cell];
                    merit += mesh.cells()[cell].area_m2 * at_residual[cell] * at_residual[cell];
                }
                return merit;
            },
            [](double a, double b) { return a + b; });
    };

    double merit = evaluate(source, mean, slope, residual);
    for (int iteration = 0; iteration < settings.max_newton_iterations; ++iteration) {
        if (max_abs(mesh, residual, worker_count) <= settings.newton_tolerance_W_m2) {
            break;
        }
        // Newton: (A / S) δT̄ − K Q δT̄ = −A F, then δh = −F + K ∇² δT̄.
        for_each_deterministic_block(
            mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                    rhs[cell] = -residual[cell] * mesh.cells()[cell].area_m2;
                }
            });
        result.cg_iterations +=
            solve_newton_system(mesh, stencil, conductance_W_K, slope, rhs, delta_mean, settings);
        diffusion_source(mesh, conductance_W_K, delta_mean, delta_source, worker_count);
        for (std::size_t cell = 0; cell < cells; ++cell) {
            delta_source[cell] -= residual[cell];
        }
        // Backtracking: the tile response is concave in h, so a full step
        // can overshoot; halve it until the merit falls (ADR-0009 §9).
        double step = 1.0;
        double trial_merit = 0.0;
        for (int halving = 0;; ++halving) {
            for_each_deterministic_block(
                mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                    for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                        trial[cell] = std::max(source[cell] + step * delta_source[cell],
                                               source_floor_W_m2[cell]);
                    }
                });
            Field2D<double> trial_residual(cells, 0.0);
            trial_merit = evaluate(trial, trial_mean, trial_slope, trial_residual);
            if (trial_merit < merit) {
                residual = std::move(trial_residual);
                break;
            }
            if (halving == settings.max_line_search_halvings) {
                // No descent left: the iterate is at the rounding floor.
                step = 0.0;
                break;
            }
            step *= 0.5;
        }
        if (step == 0.0) {
            break;
        }
        std::swap(source, trial);
        std::swap(mean, trial_mean);
        std::swap(slope, trial_slope);
        merit = trial_merit;
        ++result.newton_iterations;
    }

    // The transport applied is the conservative diffusion of the last cell
    // temperatures; |F| there is the consistency residual.
    diffusion_source(mesh, conductance_W_K, mean, result.source_W_m2, worker_count);
    result.mean_K = mean;
    result.consistency_residual_W_m2 = max_abs(mesh, residual, worker_count);

    struct Sums {
        double sum = 0.0;
        double absolute = 0.0;
        double dissipation = 0.0;
    };
    const Sums sums = reduce_deterministic_blocks<Sums>(
        mesh.blocks(), worker_count, Sums{},
        [&](std::size_t, const CellBlock& block) {
            Sums partial;
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                const double flux = mesh.cells()[cell].area_m2 * result.source_W_m2[cell];
                partial.sum += flux;
                partial.absolute += std::abs(flux);
                partial.dissipation += flux * mean[cell];
            }
            return partial;
        },
        [](Sums a, const Sums& b) {
            a.sum += b.sum;
            a.absolute += b.absolute;
            a.dissipation += b.dissipation;
            return a;
        });
    result.sum_W = sums.sum;
    result.absolute_sum_W = sums.absolute;
    result.dissipation_W_K = sums.dissipation;
    return result;
}

}  // namespace planetsim
