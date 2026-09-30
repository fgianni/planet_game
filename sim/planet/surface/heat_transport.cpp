#include "sim/planet/surface/heat_transport.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <span>
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

// One level of the aggregation multigrid: a symmetric matrix as diagonal
// plus off-diagonal CSR, and each row's aggregate on the next level.
struct Level {
    std::size_t size = 0;
    std::vector<double> diagonal;
    std::vector<std::size_t> offset;      // off-diagonal CSR row offsets
    std::vector<std::size_t> column;
    std::vector<double> value;
    std::vector<std::size_t> aggregate;   // empty on the coarsest level
    // For each off-diagonal entry: where its value lands on the next level,
    // an off-diagonal index there, or `diagonal_target` for its aggregate's
    // diagonal.
    std::vector<std::size_t> target;
    std::vector<double> factor;           // dense Cholesky, coarsest level only
};

constexpr auto diagonal_target = static_cast<std::size_t>(-1);

// Greedy aggregation in row order: a row whose neighbours are all free
// starts an aggregate with them; left-over rows join the aggregate of their
// strongest aggregated neighbour, or form their own. Deterministic.
[[nodiscard]] std::vector<std::size_t> aggregate_rows(const Level& level, std::size_t& count) {
    constexpr auto unassigned = static_cast<std::size_t>(-1);
    std::vector<std::size_t> aggregate(level.size, unassigned);
    count = 0;
    for (std::size_t row = 0; row < level.size; ++row) {
        bool free = aggregate[row] == unassigned;
        for (std::size_t k = level.offset[row]; free && k < level.offset[row + 1U]; ++k) {
            free = aggregate[level.column[k]] == unassigned;
        }
        if (!free) {
            continue;
        }
        aggregate[row] = count;
        for (std::size_t k = level.offset[row]; k < level.offset[row + 1U]; ++k) {
            aggregate[level.column[k]] = count;
        }
        ++count;
    }
    for (std::size_t row = 0; row < level.size; ++row) {
        if (aggregate[row] != unassigned) {
            continue;
        }
        double strongest = 0.0;
        for (std::size_t k = level.offset[row]; k < level.offset[row + 1U]; ++k) {
            const std::size_t other = level.column[k];
            if (aggregate[other] != unassigned && -level.value[k] > strongest) {
                strongest = -level.value[k];
                aggregate[row] = aggregate[other];
            }
        }
        if (aggregate[row] == unassigned) {
            aggregate[row] = count++;
        }
    }
    return aggregate;
}

// The sparsity of the Galerkin coarse matrix Pᵀ A P for piecewise-constant
// aggregates, and where every fine entry lands in it. Built once per solve
// from the mesh graph; only the values change between Newton iterations.
[[nodiscard]] Level coarsen_pattern(Level& fine) {
    std::size_t count = 0;
    fine.aggregate = aggregate_rows(fine, count);
    Level coarse;
    coarse.size = count;
    std::vector<std::vector<std::size_t>> columns(count);
    for (std::size_t row = 0; row < fine.size; ++row) {
        const std::size_t source = fine.aggregate[row];
        for (std::size_t k = fine.offset[row]; k < fine.offset[row + 1U]; ++k) {
            const std::size_t other = fine.aggregate[fine.column[k]];
            if (other != source) {
                columns[source].push_back(other);
            }
        }
    }
    coarse.offset.assign(count + 1U, 0U);
    for (std::size_t row = 0; row < count; ++row) {
        auto& entries = columns[row];
        std::sort(entries.begin(), entries.end());
        entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
        coarse.column.insert(coarse.column.end(), entries.begin(), entries.end());
        coarse.offset[row + 1U] = coarse.column.size();
    }
    coarse.diagonal.assign(count, 0.0);
    coarse.value.assign(coarse.column.size(), 0.0);
    fine.target.assign(fine.column.size(), diagonal_target);
    for (std::size_t row = 0; row < fine.size; ++row) {
        const std::size_t source = fine.aggregate[row];
        for (std::size_t k = fine.offset[row]; k < fine.offset[row + 1U]; ++k) {
            const std::size_t other = fine.aggregate[fine.column[k]];
            if (other != source) {
                const auto first = coarse.column.begin() +
                                   static_cast<std::ptrdiff_t>(coarse.offset[source]);
                const auto last = coarse.column.begin() +
                                  static_cast<std::ptrdiff_t>(coarse.offset[source + 1U]);
                fine.target[k] = static_cast<std::size_t>(
                    std::lower_bound(first, last, other) - coarse.column.begin());
            }
        }
    }
    return coarse;
}

// The Galerkin values of the next level from this one's.
void coarsen_values(const Level& fine, Level& coarse) {
    std::fill(coarse.diagonal.begin(), coarse.diagonal.end(), 0.0);
    std::fill(coarse.value.begin(), coarse.value.end(), 0.0);
    for (std::size_t row = 0; row < fine.size; ++row) {
        const std::size_t source = fine.aggregate[row];
        coarse.diagonal[source] += fine.diagonal[row];
        for (std::size_t k = fine.offset[row]; k < fine.offset[row + 1U]; ++k) {
            if (fine.target[k] == diagonal_target) {
                coarse.diagonal[source] += fine.value[k];
            } else {
                coarse.value[fine.target[k]] += fine.value[k];
            }
        }
    }
}

// The multigrid hierarchy's structure on the mesh graph: every cell with all
// its neighbours, down to a dense coarsest level.
[[nodiscard]] std::vector<Level> build_hierarchy(const Stencil& stencil, std::size_t cells) {
    std::vector<Level> levels(1U);
    Level& fine = levels.front();
    fine.size = cells;
    // Aggregation follows coupling strength, so the structure is built from
    // the graph Laplacian's own weights (unit conductance).
    fine.diagonal = stencil.weight_sum;
    fine.offset = stencil.offset;
    fine.column = stencil.neighbour;
    fine.value.resize(stencil.weight.size());
    for (std::size_t k = 0; k < stencil.weight.size(); ++k) {
        fine.value[k] = -stencil.weight[k];
    }
    constexpr std::size_t coarsest_size = 400U;
    while (levels.back().size > coarsest_size) {
        Level coarse = coarsen_pattern(levels.back());
        coarsen_values(levels.back(), coarse);
        if (coarse.size >= levels.back().size) {
            levels.back().aggregate.clear();
            levels.back().target.clear();
            break;
        }
        levels.push_back(std::move(coarse));
    }
    return levels;
}

void factor_dense(Level& level) {
    const std::size_t n = level.size;
    level.factor.assign(n * n, 0.0);
    for (std::size_t row = 0; row < n; ++row) {
        level.factor[row * n + row] = level.diagonal[row];
        for (std::size_t k = level.offset[row]; k < level.offset[row + 1U]; ++k) {
            level.factor[row * n + level.column[k]] = level.value[k];
        }
    }
    for (std::size_t j = 0; j < n; ++j) {
        double pivot = level.factor[j * n + j];
        for (std::size_t k = 0; k < j; ++k) {
            pivot -= level.factor[j * n + k] * level.factor[j * n + k];
        }
        pivot = std::sqrt(pivot);
        level.factor[j * n + j] = pivot;
        for (std::size_t i = j + 1U; i < n; ++i) {
            double sum = level.factor[i * n + j];
            for (std::size_t k = 0; k < j; ++k) {
                sum -= level.factor[i * n + k] * level.factor[j * n + k];
            }
            level.factor[i * n + j] = sum / pivot;
        }
    }
}

void solve_dense(const Level& level, const std::vector<double>& rhs, std::vector<double>& x) {
    const std::size_t n = level.size;
    x = rhs;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < i; ++k) {
            x[i] -= level.factor[i * n + k] * x[k];
        }
        x[i] /= level.factor[i * n + i];
    }
    for (std::size_t i = n; i-- > 0U;) {
        for (std::size_t k = i + 1U; k < n; ++k) {
            x[i] -= level.factor[k * n + i] * x[k];
        }
        x[i] /= level.factor[i * n + i];
    }
}

// The parallel context of the finest level: the mesh's fixed blocks and a
// worker count. Coarser levels are small and run serially.
struct Parallel {
    std::span<const CellBlock> blocks;
    std::size_t workers = 1U;
};

// Symmetric Gauss–Seidel half-sweeps; forward before and backward after the
// coarse correction keep the V-cycle a symmetric preconditioner. On the
// finest level the sweep is block-hybrid: Gauss–Seidel inside each mesh
// block, the previous values across block boundaries. Its result depends on
// the fixed blocks only, never on the worker count, and the backward sweep
// is still the transpose of the forward one.
void gauss_seidel(const Level& level, const std::vector<double>& rhs, std::vector<double>& x,
                  bool forward, const Parallel* parallel) {
    if (parallel == nullptr) {
        for (std::size_t step = 0; step < level.size; ++step) {
            const std::size_t row = forward ? step : level.size - 1U - step;
            double sum = rhs[row];
            for (std::size_t k = level.offset[row]; k < level.offset[row + 1U]; ++k) {
                sum -= level.value[k] * x[level.column[k]];
            }
            x[row] = sum / level.diagonal[row];
        }
        return;
    }
    const std::vector<double> previous = x;
    for_each_deterministic_block(
        parallel->blocks, parallel->workers, [&](std::size_t, const CellBlock& block) {
            const std::size_t length = block.end - block.begin;
            for (std::size_t step = 0; step < length; ++step) {
                const std::size_t row = forward ? block.begin + step : block.end - 1U - step;
                double sum = rhs[row];
                for (std::size_t k = level.offset[row]; k < level.offset[row + 1U]; ++k) {
                    const std::size_t column = level.column[k];
                    const bool inside = column >= block.begin && column < block.end;
                    sum -= level.value[k] * (inside ? x[column] : previous[column]);
                }
                x[row] = sum / level.diagonal[row];
            }
        });
}

void v_cycle(const std::vector<Level>& levels, std::size_t index, const std::vector<double>& rhs,
             std::vector<double>& x, const Parallel& parallel) {
    const Level& level = levels[index];
    if (index + 1U == levels.size()) {
        solve_dense(level, rhs, x);
        return;
    }
    const Parallel* sweep = index == 0U ? &parallel : nullptr;
    x.assign(level.size, 0.0);
    gauss_seidel(level, rhs, x, true, sweep);
    std::vector<double> residual(level.size, 0.0);
    const auto residual_rows = [&](std::size_t begin, std::size_t end) {
        for (std::size_t row = begin; row < end; ++row) {
            double value = rhs[row] - level.diagonal[row] * x[row];
            for (std::size_t k = level.offset[row]; k < level.offset[row + 1U]; ++k) {
                value -= level.value[k] * x[level.column[k]];
            }
            residual[row] = value;
        }
    };
    if (sweep != nullptr) {
        for_each_deterministic_block(parallel.blocks, parallel.workers,
                                     [&](std::size_t, const CellBlock& block) {
                                         residual_rows(block.begin, block.end);
                                     });
    } else {
        residual_rows(0U, level.size);
    }
    std::vector<double> coarse_rhs(levels[index + 1U].size, 0.0);
    for (std::size_t row = 0; row < level.size; ++row) {
        coarse_rhs[level.aggregate[row]] += residual[row];
    }
    std::vector<double> correction;
    v_cycle(levels, index + 1U, coarse_rhs, correction, parallel);
    for (std::size_t row = 0; row < level.size; ++row) {
        x[row] += correction[level.aggregate[row]];
    }
    gauss_seidel(level, rhs, x, false, sweep);
}

// Conjugate gradients on
//   (A_c / s_c) x_c + K Σ_e w_e (x_c − x_n) = rhs_c
// over the cells with s_c > 0; the others are decoupled rows x_c = 0.
// Symmetric positive definite: a positive diagonal plus K times a graph
// Laplacian. Preconditioned by an aggregation-multigrid V-cycle (the plain
// Jacobi preconditioner needed hundreds of iterations at L6, where the
// Laplacian dominates the diagonal by three orders of magnitude). Fine-level
// work runs over the mesh's fixed blocks with block-ordered reductions, so
// the result is the same for any worker count.
int solve_newton_system(const PlanetMesh& mesh, const Stencil& stencil, double conductance_W_K,
                        const Field2D<double>& slope, const Field2D<double>& rhs,
                        std::vector<Level>& levels, Field2D<double>& solution,
                        const ImplicitTransportSettings& settings, std::size_t worker_count) {
    const std::size_t cells = mesh.cell_count();
    const Parallel parallel{mesh.blocks(), worker_count};
    const auto each_block = [&](const auto& body) {
        for_each_deterministic_block(mesh.blocks(), worker_count,
                                     [&](std::size_t, const CellBlock& block) {
                                         body(block.begin, block.end);
                                     });
    };
    // Held cells (s = 0) become decoupled identity rows; the pattern stays.
    Level& fine = levels.front();
    each_block([&](std::size_t begin, std::size_t end) {
        for (std::size_t cell = begin; cell < end; ++cell) {
            const bool free = slope[cell] > 0.0;
            fine.diagonal[cell] = free ? mesh.cells()[cell].area_m2 / slope[cell] +
                                             conductance_W_K * stencil.weight_sum[cell]
                                       : 1.0;
            for (std::size_t k = stencil.offset[cell]; k < stencil.offset[cell + 1U]; ++k) {
                fine.value[k] = free && slope[stencil.neighbour[k]] > 0.0
                                    ? -conductance_W_K * stencil.weight[k]
                                    : 0.0;
            }
        }
    });
    for (std::size_t index = 0; index + 1U < levels.size(); ++index) {
        coarsen_values(levels[index], levels[index + 1U]);
    }
    factor_dense(levels.back());

    const Level& matrix = levels.front();
    const auto apply = [&](const std::vector<double>& input, std::vector<double>& output) {
        each_block([&](std::size_t begin, std::size_t end) {
            for (std::size_t cell = begin; cell < end; ++cell) {
                double sum = matrix.diagonal[cell] * input[cell];
                for (std::size_t k = matrix.offset[cell]; k < matrix.offset[cell + 1U]; ++k) {
                    sum += matrix.value[k] * input[matrix.column[k]];
                }
                output[cell] = sum;
            }
        });
    };
    const auto dot = [&](const std::vector<double>& first, const std::vector<double>& second) {
        return reduce_deterministic_blocks<double>(
            mesh.blocks(), worker_count, 0.0,
            [&](std::size_t, const CellBlock& block) {
                double sum = 0.0;
                for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                    sum += first[cell] * second[cell];
                }
                return sum;
            },
            [](double a, double b) { return a + b; });
    };

    std::vector<double> x(cells, 0.0);
    std::vector<double> residual(cells, 0.0);
    for (std::size_t cell = 0; cell < cells; ++cell) {
        residual[cell] = slope[cell] > 0.0 ? rhs[cell] : 0.0;
    }
    std::vector<double> preconditioned;
    v_cycle(levels, 0U, residual, preconditioned, parallel);
    std::vector<double> direction = preconditioned;
    std::vector<double> product(cells, 0.0);
    double rho = dot(residual, preconditioned);
    const double target = settings.cg_relative_tolerance * std::sqrt(dot(residual, residual));
    int iteration = 0;
    while (iteration < settings.max_cg_iterations && std::sqrt(dot(residual, residual)) > target &&
           rho > 0.0) {
        apply(direction, product);
        const double alpha = rho / dot(direction, product);
        each_block([&](std::size_t begin, std::size_t end) {
            for (std::size_t cell = begin; cell < end; ++cell) {
                x[cell] += alpha * direction[cell];
                residual[cell] -= alpha * product[cell];
            }
        });
        v_cycle(levels, 0U, residual, preconditioned, parallel);
        const double next_rho = dot(residual, preconditioned);
        const double beta = next_rho / rho;
        rho = next_rho;
        each_block([&](std::size_t begin, std::size_t end) {
            for (std::size_t cell = begin; cell < end; ++cell) {
                direction[cell] = preconditioned[cell] + beta * direction[cell];
            }
        });
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
                                                 std::size_t worker_count) {
    const std::size_t cells = mesh.cell_count();
    if (!std::isfinite(conductance_W_K) || !(conductance_W_K > 0.0)) {
        throw std::invalid_argument("transport conductance must be finite and positive");
    }
    if (source_floor_W_m2.size() != cells) {
        throw std::invalid_argument("transport floor does not match the mesh");
    }

    const Stencil stencil(mesh);
    std::vector<Level> levels = build_hierarchy(stencil, cells);
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
    int non_monotone_steps = 0;
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
        result.cg_iterations += solve_newton_system(mesh, stencil, conductance_W_K, slope, rhs,
                                                    levels, delta_mean, settings, worker_count);
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
                // No descent along the Newton direction. Near the solution
                // that is the rounding floor. Farther out it is a kink (ice
                // forming or melting away, snow starting to melt) where the
                // Jacobian from one side does not describe the other: take
                // the full step, whose next Jacobian sees the other side,
                // a bounded number of times.
                if (max_abs(mesh, residual, worker_count) <=
                        settings.rounding_floor_W_m2 ||
                    non_monotone_steps == settings.max_non_monotone_steps) {
                    step = 0.0;
                } else {
                    ++non_monotone_steps;
                    step = 1.0;
                    for_each_deterministic_block(
                        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                                trial[cell] = std::max(source[cell] + delta_source[cell],
                                                       source_floor_W_m2[cell]);
                            }
                        });
                    trial_merit = evaluate(trial, trial_mean, trial_slope, trial_residual);
                    residual = std::move(trial_residual);
                }
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
