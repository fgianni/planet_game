#include "sim/planet/surface/heat_transport.hpp"

#include "sim/core/math/incomplete_lu.hpp"
#include "sim/core/math/vec3d.hpp"
#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace planetsim {
namespace {

[[nodiscard]] std::span<const CellBlock> blocks_of(const TransportGraph& graph) noexcept {
    return graph.blocks;
}

// Σ_e w_e (x_n − x_c) for one node: A_c times the Laplacian.
[[nodiscard]] double edge_sum(const TransportGraph& graph, std::size_t node,
                              const Field2D<double>& values) {
    const double own = values[node];
    double sum = 0.0;
    for (std::size_t k = graph.offset[node]; k < graph.offset[node + 1U]; ++k) {
        sum += (values[graph.neighbour[k]] - own) * graph.weight[k];
    }
    return sum;
}

[[nodiscard]] double max_abs(const TransportGraph& graph, const Field2D<double>& values,
                             std::size_t worker_count) {
    return reduce_deterministic_blocks<double>(
        blocks_of(graph), worker_count, 0.0,
        [&](std::size_t, const CellBlock& block) {
            double largest = 0.0;
            for (std::size_t node = block.begin; node < block.end; ++node) {
                largest = std::max(largest, std::abs(values[node]));
            }
            return largest;
        },
        [](double a, double b) { return std::max(a, b); });
}

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
[[nodiscard]] std::vector<Level> build_hierarchy(const TransportGraph& stencil, std::size_t cells) {
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
int solve_newton_system(const TransportGraph& stencil, double conductance_W_K,
                        const Field2D<double>& slope, const Field2D<double>& rhs,
                        std::vector<Level>& levels, Field2D<double>& solution,
                        const ImplicitTransportSettings& settings, std::size_t worker_count) {
    const std::size_t cells = stencil.size();
    const Parallel parallel{blocks_of(stencil), worker_count};
    const auto each_block = [&](const auto& body) {
        for_each_deterministic_block(blocks_of(stencil), worker_count,
                                     [&](std::size_t, const CellBlock& block) {
                                         body(block.begin, block.end);
                                     });
    };
    // Held cells (s = 0) become decoupled identity rows; the pattern stays.
    Level& fine = levels.front();
    each_block([&](std::size_t begin, std::size_t end) {
        for (std::size_t cell = begin; cell < end; ++cell) {
            const bool free = slope[cell] > 0.0;
            fine.diagonal[cell] = free ? stencil.area_m2[cell] / slope[cell] +
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
            blocks_of(stencil), worker_count, 0.0,
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

TransportGraph mesh_transport_graph(const PlanetMesh& mesh) {
    TransportGraph graph;
    const std::size_t cells = mesh.cell_count();
    graph.offset.assign(cells + 1U, 0U);
    for (std::size_t cell = 0; cell < cells; ++cell) {
        const CellId id{static_cast<CellId::value_type>(cell)};
        graph.area_m2.push_back(mesh.cells()[cell].area_m2);
        double sum = 0.0;
        for (const auto& cell_edge : mesh.cell_edges(id)) {
            const auto& edge = mesh.edge(cell_edge.edge);
            graph.neighbour.push_back(cell_edge.neighbor.to_index());
            graph.weight.push_back(edge.length_m / edge.centroid_distance_m);
            sum += graph.weight.back();
        }
        graph.offset[cell + 1U] = graph.neighbour.size();
        graph.weight_sum.push_back(sum);
    }
    graph.blocks.assign(mesh.blocks().begin(), mesh.blocks().end());
    return graph;
}

namespace {

struct Agglomeration {
    TransportGraph graph;
    std::vector<std::size_t> group_of_cell;
    std::shared_ptr<const PlanetMesh> coarse_mesh;   // null at level 0: the mesh itself
};

// Each fine cell joins the nearest cell of the mesh one level coarser (a
// greedy walk over the coarse mesh from the previous fine cell's answer:
// fine cells come in a space-filling order). Groups take the fine cells'
// areas, so energy stays exact, and the coarse mesh's own two-point weights,
// the ADR-0002 operator one level down.
[[nodiscard]] Agglomeration build_agglomeration(const PlanetMesh& mesh) {
    Agglomeration result;
    if (mesh.subdivision() == 0U) {
        result.graph = mesh_transport_graph(mesh);
        result.group_of_cell.resize(mesh.cell_count());
        for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
            result.group_of_cell[cell] = cell;
        }
        return result;
    }
    result.coarse_mesh = std::make_shared<const PlanetMesh>(
        make_icosphere(mesh.subdivision() - 1U, mesh.radius_m()));
    const PlanetMesh& coarse = *result.coarse_mesh;
    result.graph = mesh_transport_graph(coarse);
    std::fill(result.graph.area_m2.begin(), result.graph.area_m2.end(), 0.0);
    result.group_of_cell.resize(mesh.cell_count());
    std::size_t guess = 0;
    for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
        const Vec3d& centre = mesh.cells()[cell].center_unit;
        for (;;) {
            std::size_t best = guess;
            double best_dot = dot(centre, coarse.cells()[guess].center_unit);
            const CellId id{static_cast<CellId::value_type>(guess)};
            for (const auto& cell_edge : coarse.cell_edges(id)) {
                const std::size_t other = cell_edge.neighbor.to_index();
                const double value = dot(centre, coarse.cells()[other].center_unit);
                if (value > best_dot) {
                    best_dot = value;
                    best = other;
                }
            }
            if (best == guess) {
                break;
            }
            guess = best;
        }
        result.group_of_cell[cell] = guess;
        result.graph.area_m2[guess] += mesh.cells()[cell].area_m2;
    }
    for (const double area : result.graph.area_m2) {
        if (!(area > 0.0)) {
            throw std::logic_error("a coarse transport cell received no fine cell");
        }
    }
    return result;
}

}  // namespace

namespace {

// One agglomeration per mesh level and radius for the process: building
// the coarse mesh takes about 0.1 s at L5.
const Agglomeration& cached_agglomeration(const PlanetMesh& mesh) {
    static std::mutex mutex;
    static std::map<std::pair<std::uint32_t, double>, std::unique_ptr<Agglomeration>> cache;
    const std::lock_guard lock(mutex);
    auto& entry = cache[{mesh.subdivision(), mesh.radius_m()}];
    if (!entry || entry->group_of_cell.size() != mesh.cell_count()) {
        entry = std::make_unique<Agglomeration>(build_agglomeration(mesh));
    }
    return *entry;
}

}  // namespace

const TransportGraph& agglomerated_transport_graph(const PlanetMesh& mesh,
                                                   const std::vector<std::size_t>*& group_of_cell) {
    const Agglomeration& entry = cached_agglomeration(mesh);
    group_of_cell = &entry.group_of_cell;
    return entry.graph;
}

const PlanetMesh& agglomerated_mesh(const PlanetMesh& mesh) {
    const Agglomeration& entry = cached_agglomeration(mesh);
    return entry.coarse_mesh ? *entry.coarse_mesh : mesh;
}

struct GraphMultigrid::Impl {
    std::vector<Level> levels;
    std::vector<CellBlock> blocks;
};

GraphMultigrid::GraphMultigrid(const TransportGraph& graph) : impl_(std::make_unique<Impl>()) {
    impl_->levels = build_hierarchy(graph, graph.size());
    impl_->blocks = graph.blocks;
}

GraphMultigrid::~GraphMultigrid() = default;
GraphMultigrid::GraphMultigrid(GraphMultigrid&&) noexcept = default;
GraphMultigrid& GraphMultigrid::operator=(GraphMultigrid&&) noexcept = default;

void GraphMultigrid::set_matrix(std::span<const double> diagonal,
                                std::span<const double> off_diagonal) {
    Level& fine = impl_->levels.front();
    if (diagonal.size() != fine.size || off_diagonal.size() != fine.value.size()) {
        throw std::invalid_argument("multigrid matrix does not match the graph");
    }
    std::copy(diagonal.begin(), diagonal.end(), fine.diagonal.begin());
    std::copy(off_diagonal.begin(), off_diagonal.end(), fine.value.begin());
    for (std::size_t index = 0; index + 1U < impl_->levels.size(); ++index) {
        coarsen_values(impl_->levels[index], impl_->levels[index + 1U]);
    }
    factor_dense(impl_->levels.back());
}

void GraphMultigrid::precondition(const std::vector<double>& rhs, std::vector<double>& solution,
                                  std::size_t worker_count) const {
    const Parallel parallel{impl_->blocks, worker_count};
    v_cycle(impl_->levels, 0U, rhs, solution, parallel);
}

void diffusion_source(const TransportGraph& graph, double conductance_W_K,
                      const Field2D<double>& temperature_K, Field2D<double>& source_W_m2,
                      std::size_t worker_count) {
    if (temperature_K.size() != graph.size()) {
        throw std::invalid_argument("transport temperature does not match the mesh");
    }
    source_W_m2 = Field2D<double>(graph.size(), 0.0);
    for_each_deterministic_block(
        blocks_of(graph), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                source_W_m2[cell] = conductance_W_K * edge_sum(graph, cell, temperature_K) /
                                    graph.area_m2[cell];
            }
        });
}

ImplicitTransportResult solve_implicit_transport(const TransportGraph& graph,
                                                 double conductance_W_K,
                                                 const Field2D<double>& source_floor_W_m2,
                                                 const TransportResponse& response,
                                                 const ImplicitTransportSettings& settings,
                                                 std::size_t worker_count) {
    const std::size_t cells = graph.size();
    if (!std::isfinite(conductance_W_K) || !(conductance_W_K > 0.0)) {
        throw std::invalid_argument("transport conductance must be finite and positive");
    }
    if (source_floor_W_m2.size() != cells) {
        throw std::invalid_argument("transport floor does not match the mesh");
    }

    std::vector<Level> levels = build_hierarchy(graph, cells);
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
        diffusion_source(graph, conductance_W_K, at_mean, diffusion, worker_count);
        return reduce_deterministic_blocks<double>(
            blocks_of(graph), worker_count, 0.0,
            [&](std::size_t, const CellBlock& block) {
                double merit = 0.0;
                for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                    at_residual[cell] = at[cell] - diffusion[cell];
                    merit += graph.area_m2[cell] * at_residual[cell] * at_residual[cell];
                }
                return merit;
            },
            [](double a, double b) { return a + b; });
    };

    double merit = evaluate(source, mean, slope, residual);
    int non_monotone_steps = 0;
    for (int iteration = 0; iteration < settings.max_newton_iterations; ++iteration) {
        if (max_abs(graph, residual, worker_count) <= settings.newton_tolerance_W_m2) {
            break;
        }
        // Newton: (A / S) δT̄ − K Q δT̄ = −A F, then δh = −F + K ∇² δT̄.
        for_each_deterministic_block(
            blocks_of(graph), worker_count, [&](std::size_t, const CellBlock& block) {
                for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                    rhs[cell] = -residual[cell] * graph.area_m2[cell];
                }
            });
        result.cg_iterations += solve_newton_system(graph, conductance_W_K, slope, rhs,
                                                    levels, delta_mean, settings, worker_count);
        diffusion_source(graph, conductance_W_K, delta_mean, delta_source, worker_count);
        for (std::size_t cell = 0; cell < cells; ++cell) {
            delta_source[cell] -= residual[cell];
        }
        // Backtracking: the tile response is concave in h, so a full step
        // can overshoot; halve it until the merit falls (ADR-0009 §9).
        double step = 1.0;
        double trial_merit = 0.0;
        for (int halving = 0;; ++halving) {
            for_each_deterministic_block(
                blocks_of(graph), worker_count, [&](std::size_t, const CellBlock& block) {
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
                if (max_abs(graph, residual, worker_count) <=
                        settings.rounding_floor_W_m2 ||
                    non_monotone_steps == settings.max_non_monotone_steps) {
                    step = 0.0;
                } else {
                    ++non_monotone_steps;
                    step = 1.0;
                    for_each_deterministic_block(
                        blocks_of(graph), worker_count, [&](std::size_t, const CellBlock& block) {
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
    diffusion_source(graph, conductance_W_K, mean, result.source_W_m2, worker_count);
    result.mean_K = mean;
    result.consistency_residual_W_m2 = max_abs(graph, residual, worker_count);

    struct Sums {
        double sum = 0.0;
        double absolute = 0.0;
        double dissipation = 0.0;
    };
    const Sums sums = reduce_deterministic_blocks<Sums>(
        blocks_of(graph), worker_count, Sums{},
        [&](std::size_t, const CellBlock& block) {
            Sums partial;
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                const double flux = graph.area_m2[cell] * result.source_W_m2[cell];
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

void advection_diffusion_source(const TransportGraph& graph, const AdvectionDiffusion& transport,
                                const Field2D<double>& mean_K,
                                const std::vector<double>& energy_J_kg,
                                Field2D<double>& eddy_W_m2, Field2D<double>& advective_W_m2,
                                std::size_t worker_count) {
    const std::size_t nodes = graph.size();
    const std::size_t entries = graph.neighbour.size();
    const std::size_t layers = transport.layers;
    if (transport.conductance_W_K.size() != entries ||
        transport.outflow_kg_s.size() != layers * entries || mean_K.size() != nodes ||
        energy_J_kg.size() != layers * nodes) {
        throw std::invalid_argument("advection-diffusion inputs do not match the graph");
    }
    if (eddy_W_m2.size() != nodes) {
        eddy_W_m2 = Field2D<double>(nodes, 0.0);
    }
    if (advective_W_m2.size() != nodes) {
        advective_W_m2 = Field2D<double>(nodes, 0.0);
    }
    for_each_deterministic_block(
        blocks_of(graph), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t a = block.begin; a < block.end; ++a) {
                double eddy = 0.0;
                double advective = 0.0;
                for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
                    const std::size_t b = graph.neighbour[k];
                    eddy += transport.conductance_W_K[k] * (mean_K[b] - mean_K[a]);
                    for (std::size_t l = 0; l < layers; ++l) {
                        const double out = transport.outflow_kg_s[l * entries + k];
                        const std::size_t upwind = out > 0.0 ? a : b;
                        advective -= out * energy_J_kg[l * nodes + upwind];
                    }
                }
                eddy_W_m2[a] = eddy / graph.area_m2[a];
                advective_W_m2[a] = advective / graph.area_m2[a];
            }
        });
}

namespace {

// Right-preconditioned BiCGSTAB on a transport graph's nodes: x ← solution
// of A x = b from x = 0. Returns the iterations; stops at the tolerance, the
// cap, or a breakdown (an inexact Newton step is still a descent candidate).
template <typename Apply, typename Precondition>
int bicgstab(const TransportGraph& graph, const Apply& apply, const Precondition& precondition,
             const std::vector<double>& b, std::vector<double>& x, double relative_tolerance,
             int max_iterations, std::size_t worker_count) {
    const std::size_t n = graph.size();
    const auto dot = [&](const std::vector<double>& u, const std::vector<double>& v) {
        return reduce_deterministic_blocks<double>(
            blocks_of(graph), worker_count, 0.0,
            [&](std::size_t, const CellBlock& block) {
                double sum = 0.0;
                for (std::size_t i = block.begin; i < block.end; ++i) {
                    sum += u[i] * v[i];
                }
                return sum;
            },
            [](double p, double q) { return p + q; });
    };
    x.assign(n, 0.0);
    std::vector<double> r = b;
    const std::vector<double> shadow = r;
    std::vector<double> p(n, 0.0);
    std::vector<double> v(n, 0.0);
    std::vector<double> p_hat(n, 0.0);
    std::vector<double> s(n, 0.0);
    std::vector<double> s_hat(n, 0.0);
    std::vector<double> t(n, 0.0);
    const double b_norm = std::sqrt(dot(b, b));
    if (!(b_norm > 0.0)) {
        return 0;
    }
    double rho = 1.0;
    double alpha = 1.0;
    double omega = 1.0;
    int iteration = 0;
    while (iteration < max_iterations) {
        const double rho_next = dot(shadow, r);
        if (rho_next == 0.0 || omega == 0.0) {
            break;
        }
        const double beta = rho_next / rho * (alpha / omega);
        rho = rho_next;
        for (std::size_t i = 0; i < n; ++i) {
            p[i] = r[i] + beta * (p[i] - omega * v[i]);
        }
        precondition(p, p_hat);
        apply(p_hat, v);
        const double shadow_v = dot(shadow, v);
        if (shadow_v == 0.0) {
            break;
        }
        alpha = rho / shadow_v;
        for (std::size_t i = 0; i < n; ++i) {
            s[i] = r[i] - alpha * v[i];
        }
        ++iteration;
        if (std::sqrt(dot(s, s)) <= relative_tolerance * b_norm) {
            for (std::size_t i = 0; i < n; ++i) {
                x[i] += alpha * p_hat[i];
            }
            break;
        }
        precondition(s, s_hat);
        apply(s_hat, t);
        const double t_t = dot(t, t);
        omega = t_t > 0.0 ? dot(t, s) / t_t : 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            x[i] += alpha * p_hat[i] + omega * s_hat[i];
            r[i] = s[i] - omega * t[i];
        }
        if (std::sqrt(dot(r, r)) <= relative_tolerance * b_norm) {
            break;
        }
    }
    return iteration;
}

}  // namespace

TracerTransportResult solve_implicit_tracer(const TransportGraph& graph,
                                            const AdvectionDiffusion& transport,
                                            std::span<const double> layer_mass_kg,
                                            const std::vector<double>& tracer, double dt_s,
                                            double relative_tolerance, int max_iterations,
                                            std::size_t worker_count) {
    const std::size_t nodes = graph.size();
    const std::size_t entries = graph.neighbour.size();
    const std::size_t layers = transport.layers;
    if (layers == 0U || layer_mass_kg.size() != nodes || tracer.size() != layers * nodes ||
        transport.conductance_W_K.size() != entries ||
        transport.outflow_kg_s.size() != layers * entries || !(dt_s > 0.0)) {
        throw std::invalid_argument("tracer transport inputs do not match the graph");
    }
    const double exchange_scale =
        1.0 / (transport.heat_capacity_J_kg_K * static_cast<double>(layers));
    // Each node's vertical fluxes W_{l+½}, l < N − 1 (kg/s, upward).
    std::vector<double> vertical(nodes * layers, 0.0);
    TracerTransportResult result;
    for (std::size_t a = 0; a < nodes; ++a) {
        double w = 0.0;
        for (std::size_t l = 0; l < layers; ++l) {
            for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
                w -= transport.outflow_kg_s[l * entries + k];
            }
            vertical[a * layers + l] = l + 1U < layers ? w : 0.0;
        }
        result.column_divergence_kg_s = std::max(result.column_divergence_kg_s, std::abs(w));
    }

    // The stacked system, node-major: row a N + l. Its CSR has each
    // horizontal entry of the node's graph row for the layer, then the
    // layers below and above.
    TransportGraph stacked;
    stacked.area_m2.assign(nodes * layers, 0.0);
    stacked.offset.assign(nodes * layers + 1U, 0U);
    for (const CellBlock& block : graph.blocks) {
        stacked.blocks.push_back(CellBlock{static_cast<std::uint32_t>(block.begin * layers),
                                           static_cast<std::uint32_t>(block.end * layers)});
    }
    for (std::size_t a = 0; a < nodes; ++a) {
        for (std::size_t l = 0; l < layers; ++l) {
            const std::size_t row = a * layers + l;
            for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
                stacked.neighbour.push_back(graph.neighbour[k] * layers + l);
            }
            if (l > 0U) {
                stacked.neighbour.push_back(row - 1U);
            }
            if (l + 1U < layers) {
                stacked.neighbour.push_back(row + 1U);
            }
            stacked.offset[row + 1U] = stacked.neighbour.size();
        }
    }
    std::vector<double> diagonal(nodes * layers, 0.0);
    std::vector<double> off(stacked.neighbour.size(), 0.0);
    std::vector<double> rhs(nodes * layers, 0.0);
    for_each_deterministic_block(
        blocks_of(graph), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t a = block.begin; a < block.end; ++a) {
                const double rate = layer_mass_kg[a] / dt_s;
                for (std::size_t l = 0; l < layers; ++l) {
                    const std::size_t row = a * layers + l;
                    double own = rate;
                    std::size_t at = stacked.offset[row];
                    for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k, ++at) {
                        const double flow = transport.outflow_kg_s[l * entries + k];
                        const double eddy = transport.conductance_W_K[k] * exchange_scale;
                        own += std::max(flow, 0.0) + eddy;
                        off[at] = -std::max(-flow, 0.0) - eddy;
                    }
                    const double below = l > 0U ? vertical[a * layers + l - 1U] : 0.0;   // W_{l−½}
                    const double above = vertical[row];                                // W_{l+½}
                    if (l > 0U) {
                        // Upward from below carries the lower layer's c.
                        own += std::max(-below, 0.0);
                        off[at++] = -std::max(below, 0.0);
                    }
                    if (l + 1U < layers) {
                        own += std::max(above, 0.0);
                        off[at++] = -std::max(-above, 0.0);
                    }
                    diagonal[row] = own;
                    rhs[row] = rate * tracer[l * nodes + a];
                }
            }
        });
    std::vector<std::size_t> block_start{0U};
    for (const CellBlock& block : stacked.blocks) {
        block_start.push_back(block.end);
    }
    const IncompleteLU factor(stacked.offset, stacked.neighbour, diagonal, off, block_start, 1.0,
                              worker_count);
    const auto apply = [&](const std::vector<double>& x, std::vector<double>& y) {
        for_each_deterministic_block(
            blocks_of(stacked), worker_count, [&](std::size_t, const CellBlock& block) {
                for (std::size_t row = block.begin; row < block.end; ++row) {
                    double sum = diagonal[row] * x[row];
                    for (std::size_t k = stacked.offset[row]; k < stacked.offset[row + 1U]; ++k) {
                        sum += off[k] * x[stacked.neighbour[k]];
                    }
                    y[row] = sum;
                }
            });
    };
    const auto precondition = [&](const std::vector<double>& x, std::vector<double>& y) {
        y = x;
        factor.solve(y);
    };
    // Solve for the change from the start, A δ = b − A c⁰, which is small.
    std::vector<double> start(nodes * layers, 0.0);
    for (std::size_t a = 0; a < nodes; ++a) {
        for (std::size_t l = 0; l < layers; ++l) {
            start[a * layers + l] = tracer[l * nodes + a];
        }
    }
    std::vector<double> applied(nodes * layers, 0.0);
    apply(start, applied);
    std::vector<double> residual(nodes * layers, 0.0);
    for (std::size_t row = 0; row < residual.size(); ++row) {
        residual[row] = rhs[row] - applied[row];
    }
    std::vector<double> delta;
    result.iterations = bicgstab(stacked, apply, precondition, residual, delta,
                                 relative_tolerance, max_iterations, worker_count);
    std::vector<double> solved(nodes * layers, 0.0);
    for (std::size_t row = 0; row < solved.size(); ++row) {
        solved[row] = std::max(0.0, start[row] + (delta.empty() ? 0.0 : delta[row]));
    }
    apply(solved, applied);
    double residual_norm = 0.0;
    double rhs_norm = 0.0;
    for (std::size_t row = 0; row < solved.size(); ++row) {
        residual_norm += (rhs[row] - applied[row]) * (rhs[row] - applied[row]);
        rhs_norm += rhs[row] * rhs[row];
    }
    result.relative_residual = rhs_norm > 0.0 ? std::sqrt(residual_norm / rhs_norm) : 0.0;

    // The flux form of the solution: each flux leaves its donor and enters
    // its receiver with the same value, so Σ M c is conserved exactly.
    result.tracer.assign(layers * nodes, 0.0);
    std::vector<double> change(nodes * layers, 0.0);   // kg/s of tracer, per row
    for (std::size_t a = 0; a < nodes; ++a) {
        for (std::size_t l = 0; l < layers; ++l) {
            const std::size_t row = a * layers + l;
            for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
                const std::size_t b = graph.neighbour[k];
                if (b < a) {
                    continue;   // each face once, from its lower node
                }
                const std::size_t other = b * layers + l;
                const double flow = transport.outflow_kg_s[l * entries + k];
                const double eddy = transport.conductance_W_K[k] * exchange_scale;
                const double flux =
                    (flow > 0.0 ? flow * solved[row] : flow * solved[other]) +
                    eddy * (solved[row] - solved[other]);   // a → b
                change[row] -= flux;
                change[other] += flux;
            }
            if (l + 1U < layers) {
                const double w = vertical[row];
                const double flux = w > 0.0 ? w * solved[row] : w * solved[row + 1U];   // up
                change[row] -= flux;
                change[row + 1U] += flux;
            }
        }
    }
    for (std::size_t a = 0; a < nodes; ++a) {
        for (std::size_t l = 0; l < layers; ++l) {
            const std::size_t row = a * layers + l;
            double value = tracer[l * nodes + a] + dt_s * change[row] / layer_mass_kg[a];
            if (value < 0.0) {
                result.clipped_kg -= value * layer_mass_kg[a];
                value = 0.0;
            }
            result.tracer[l * nodes + a] = value;
        }
    }
    return result;
}

AdvectionDiffusionResult solve_implicit_advection_diffusion(
    const TransportGraph& graph, const AdvectionDiffusion& transport,
    const Field2D<double>& source_floor_W_m2, const AdvectionResponse& response,
    const ImplicitTransportSettings& settings, std::size_t worker_count) {
    const std::size_t nodes = graph.size();
    const std::size_t entries = graph.neighbour.size();
    const std::size_t layers = transport.layers;
    if (source_floor_W_m2.size() != nodes || transport.conductance_W_K.size() != entries ||
        transport.outflow_kg_s.size() != layers * entries) {
        throw std::invalid_argument("advection-diffusion inputs do not match the graph");
    }
    for (const double g : transport.conductance_W_K) {
        if (!(g >= 0.0) || !std::isfinite(g)) {
            throw std::invalid_argument("eddy conductances must be finite and non-negative");
        }
    }

    struct Iterate {
        Field2D<double> source;
        Field2D<double> mean;
        Field2D<double> slope;
        std::vector<double> energy;
        std::vector<double> energy_slope;
        Field2D<double> residual;
        double merit = 0.0;
    };
    Field2D<double> eddy(nodes, 0.0);
    Field2D<double> advective(nodes, 0.0);
    // F(h) = h − H(θ(h), s(h)), and the merit Σ A F².
    const auto evaluate = [&](Iterate& at) {
        response(at.source, at.mean, at.slope, at.energy, at.energy_slope);
        if (at.mean.size() != nodes || at.slope.size() != nodes ||
            at.energy.size() != layers * nodes || at.energy_slope.size() != layers * nodes) {
            throw std::logic_error("advection-diffusion response has the wrong size");
        }
        advection_diffusion_source(graph, transport, at.mean, at.energy, eddy, advective,
                                   worker_count);
        if (at.residual.size() != nodes) {
            at.residual = Field2D<double>(nodes, 0.0);
        }
        at.merit = reduce_deterministic_blocks<double>(
            blocks_of(graph), worker_count, 0.0,
            [&](std::size_t, const CellBlock& block) {
                double merit = 0.0;
                for (std::size_t a = block.begin; a < block.end; ++a) {
                    at.residual[a] = at.source[a] - (eddy[a] + advective[a]);
                    merit += graph.area_m2[a] * at.residual[a] * at.residual[a];
                }
                return merit;
            },
            [](double a, double b) { return a + b; });
    };

    AdvectionDiffusionResult result;
    Iterate current;
    current.source = Field2D<double>(nodes, 0.0);
    for (std::size_t a = 0; a < nodes; ++a) {
        current.source[a] = std::max(0.0, source_floor_W_m2[a]);
    }
    evaluate(current);
    Iterate trial;
    std::vector<std::size_t> block_start{0U};
    for (const CellBlock& block : graph.blocks) {
        block_start.push_back(block.end);
    }
    std::vector<double> diagonal(nodes, 0.0);
    std::vector<double> off(entries, 0.0);
    GraphMultigrid multigrid(graph);
    std::vector<double> symmetric_diagonal(nodes, 0.0);
    std::vector<double> symmetric_off(entries, 0.0);
    std::vector<double> scaled(nodes, 0.0);
    std::vector<double> leftover(nodes, 0.0);
    // θ' is positive for a column with a response; this guards one without.
    constexpr double minimum_slope = 1.0e-12;
    std::vector<double> rhs(nodes, 0.0);
    std::vector<double> delta(nodes, 0.0);
    int non_monotone_steps = 0;
    for (int iteration = 0; iteration < settings.max_newton_iterations; ++iteration) {
        if (max_abs(graph, current.residual, worker_count) <= settings.newton_tolerance_W_m2) {
            break;
        }
        // J = I − ∂H/∂h on the graph's pattern: H_a depends on h_a and on its
        // neighbours' h through θ and the upwind s.
        for_each_deterministic_block(
            blocks_of(graph), worker_count, [&](std::size_t, const CellBlock& block) {
                for (std::size_t a = block.begin; a < block.end; ++a) {
                    const double inverse_area = 1.0 / graph.area_m2[a];
                    double own = 0.0;
                    for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
                        const std::size_t b = graph.neighbour[k];
                        const double g = transport.conductance_W_K[k];
                        own -= g * current.slope[a];
                        double neighbour = g * current.slope[b];
                        for (std::size_t l = 0; l < layers; ++l) {
                            const double out = transport.outflow_kg_s[l * entries + k];
                            if (out > 0.0) {
                                own -= out * current.energy_slope[l * nodes + a];
                            } else {
                                neighbour -= out * current.energy_slope[l * nodes + b];
                            }
                        }
                        off[k] = -neighbour * inverse_area;
                    }
                    diagonal[a] = 1.0 - own * inverse_area;
                    rhs[a] = -current.residual[a];
                }
            });
        // Block ILU(0) on the graph's fixed blocks: the blocks factor and
        // solve in parallel, the same for any worker count.
        const IncompleteLU factor(graph.offset, graph.neighbour, diagonal, off, block_start, 1.0,
                                  worker_count);
        const auto apply = [&](const std::vector<double>& x, std::vector<double>& y) {
            for_each_deterministic_block(
                blocks_of(graph), worker_count, [&](std::size_t, const CellBlock& block) {
                    for (std::size_t a = block.begin; a < block.end; ++a) {
                        double sum = diagonal[a] * x[a];
                        for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
                            sum += off[k] * x[graph.neighbour[k]];
                        }
                        y[a] = sum;
                    }
                });
        };
        // The eddies' part of J factors as A⁻¹ (A Θ⁻¹ + K) Θ, Θ = diag(θ'),
        // K the conductances' Laplacian; A Θ⁻¹ + K is symmetric positive
        // definite, ADR-0009's Newton matrix. Its V-cycle, then the block ILU
        // of the full J on what is left (the advection).
        for_each_deterministic_block(
            blocks_of(graph), worker_count, [&](std::size_t, const CellBlock& block) {
                for (std::size_t a = block.begin; a < block.end; ++a) {
                    double sum = 0.0;
                    for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
                        sum += transport.conductance_W_K[k];
                        symmetric_off[k] = -transport.conductance_W_K[k];
                    }
                    const double slope = std::max(current.slope[a], minimum_slope);
                    symmetric_diagonal[a] = graph.area_m2[a] / slope + sum;
                }
            });
        multigrid.set_matrix(symmetric_diagonal, symmetric_off);
        const auto precondition = [&](const std::vector<double>& x, std::vector<double>& y) {
            for (std::size_t a = 0; a < nodes; ++a) {
                scaled[a] = graph.area_m2[a] * x[a];
            }
            multigrid.precondition(scaled, y, worker_count);
            for (std::size_t a = 0; a < nodes; ++a) {
                y[a] /= std::max(current.slope[a], minimum_slope);
            }
            apply(y, leftover);
            for (std::size_t a = 0; a < nodes; ++a) {
                leftover[a] = x[a] - leftover[a];
            }
            factor.solve(leftover);
            for (std::size_t a = 0; a < nodes; ++a) {
                y[a] += leftover[a];
            }
        };
        result.linear_iterations +=
            bicgstab(graph, apply, precondition, rhs, delta, settings.cg_relative_tolerance,
                     settings.max_cg_iterations, worker_count);

        // Backtracking on the merit, as ADR-0009 §9.
        double step = 1.0;
        bool accepted = false;
        for (int halving = 0;; ++halving) {
            trial.source = Field2D<double>(nodes, 0.0);
            for (std::size_t a = 0; a < nodes; ++a) {
                trial.source[a] =
                    std::max(current.source[a] + step * delta[a], source_floor_W_m2[a]);
            }
            evaluate(trial);
            if (trial.merit < current.merit) {
                accepted = true;
                break;
            }
            if (halving == settings.max_line_search_halvings) {
                if (max_abs(graph, current.residual, worker_count) >
                        settings.rounding_floor_W_m2 &&
                    non_monotone_steps < settings.max_non_monotone_steps) {
                    // A kink in the columns' response: take the full step a
                    // bounded number of times (ADR-0009 §9).
                    ++non_monotone_steps;
                    for (std::size_t a = 0; a < nodes; ++a) {
                        trial.source[a] =
                            std::max(current.source[a] + delta[a], source_floor_W_m2[a]);
                    }
                    evaluate(trial);
                    accepted = true;
                }
                break;
            }
            step *= 0.5;
        }
        if (!accepted) {
            break;
        }
        std::swap(current, trial);
        ++result.newton_iterations;
    }

    // The transport applied is the conservative source of the last iterate's
    // θ and s; |F| there is the consistency residual.
    advection_diffusion_source(graph, transport, current.mean, current.energy, eddy, advective,
                               worker_count);
    result.source_W_m2 = Field2D<double>(nodes, 0.0);
    double sum = 0.0;
    double absolute = 0.0;
    double eddy_absolute = 0.0;
    double advective_absolute = 0.0;
    for (std::size_t a = 0; a < nodes; ++a) {
        result.source_W_m2[a] = eddy[a] + advective[a];
        const double area = graph.area_m2[a];
        sum += area * result.source_W_m2[a];
        absolute += area * std::abs(result.source_W_m2[a]);
        eddy_absolute += area * std::abs(eddy[a]);
        advective_absolute += area * std::abs(advective[a]);
    }
    result.mean_K = current.mean;
    result.energy_J_kg = current.energy;
    result.consistency_residual_W_m2 = max_abs(graph, current.residual, worker_count);
    result.sum_W = sum;
    result.absolute_sum_W = absolute;
    result.eddy_absolute_sum_W = eddy_absolute;
    result.advective_absolute_sum_W = advective_absolute;
    return result;
}

}  // namespace planetsim
