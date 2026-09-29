#include "sim/planet/terrain/drainage.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace planetsim {
namespace {

struct FloodNode {
    double elevation_m = 0.0;
    CellId cell;
};

struct FloodNodeGreater {
    [[nodiscard]] bool operator()(const FloodNode& left,
                                  const FloodNode& right) const noexcept {
        if (left.elevation_m != right.elevation_m) {
            return left.elevation_m > right.elevation_m;
        }
        return left.cell > right.cell;
    }
};

void validate_hypsometry(const PlanetMesh& mesh,
                         const Field3D<float>& hypsometry_m) {
    if (hypsometry_m.layer_count() != hypsometry_quantile_count ||
        hypsometry_m.cell_count() != mesh.cell_count()) {
        throw std::invalid_argument("drainage hypsometry field has the wrong shape");
    }
    for (std::size_t cell = 0; cell < mesh.cell_count(); ++cell) {
        float previous = -std::numeric_limits<float>::infinity();
        for (std::size_t layer = 0; layer < hypsometry_quantile_count; ++layer) {
            const float value =
                hypsometry_m.at(layer, CellId{static_cast<std::uint32_t>(cell)});
            if (!std::isfinite(value) || value < previous) {
                throw std::invalid_argument(
                    "drainage hypsometry must be finite and non-decreasing");
            }
            previous = value;
        }
    }
}

[[nodiscard]] bool better_spill(double candidate_elevation_m,
                                CellId candidate_cell,
                                double selected_elevation_m,
                                CellId selected_cell) noexcept {
    return !selected_cell.is_valid() ||
           candidate_elevation_m < selected_elevation_m ||
           (candidate_elevation_m == selected_elevation_m &&
            candidate_cell < selected_cell);
}

[[nodiscard]] bool cells_are_neighbors(const PlanetMesh& mesh,
                                       CellId first,
                                       CellId second) {
    const auto edges = mesh.cell_edges(first);
    return std::any_of(
        edges.begin(), edges.end(),
        [second](const CellEdgeGeometry& edge) {
            return edge.neighbor == second;
        });
}

struct CatchmentTotals {
    double sum_m2 = 0.0;
    double maximum_m2 = 0.0;
    std::uint32_t land_basin_count = 0U;
};

}  // namespace

DrainageSurface fill_drainage_depressions(const PlanetMesh& mesh,
                                           const Field3D<float>& hypsometry_m,
                                           double sea_level_m,
                                           std::size_t worker_count) {
    if (!std::isfinite(sea_level_m)) {
        throw std::invalid_argument("drainage sea level must be finite");
    }
    validate_hypsometry(mesh, hypsometry_m);

    const std::size_t cell_count = mesh.cell_count();
    const SurfaceFractions fractions =
        compute_surface_fractions(mesh, hypsometry_m, sea_level_m, worker_count);

    DrainageSurface surface;
    surface.land_fraction = fractions.land_fraction;
    surface.outlet = Field2D<std::uint8_t>(cell_count, 0U);
    surface.drainage_elevation_m = Field2D<float>(cell_count);
    surface.filled_elevation_m = Field2D<float>(cell_count);
    surface.depression_id =
        Field2D<std::uint32_t>(cell_count, no_depression);

    std::vector<double> drainage_elevation_m(cell_count, 0.0);
    for_each_deterministic_block(
        mesh.blocks(), worker_count,
        [&](std::size_t, const CellBlock& block) {
            for (std::uint32_t value = block.begin; value < block.end; ++value) {
                const CellId cell{value};
                const HypsometryQuantiles quantiles =
                    cell_hypsometry(hypsometry_m, cell);
                const bool is_outlet =
                    fractions.ocean_fraction[cell] > 0.0F;
                const double elevation_m =
                    is_outlet
                        ? mean_elevation_above_m(quantiles, sea_level_m)
                        : mean_elevation_m(quantiles);
                if (!std::isfinite(elevation_m)) {
                    throw std::invalid_argument(
                        "drainage elevation must be finite");
                }
                drainage_elevation_m[value] = elevation_m;
                surface.drainage_elevation_m[value] =
                    static_cast<float>(elevation_m);
                surface.outlet[value] =
                    static_cast<std::uint8_t>(is_outlet ? 1U : 0U);
            }
        });

    std::priority_queue<FloodNode, std::vector<FloodNode>, FloodNodeGreater>
        frontier;
    std::vector<double> filled_elevation_m(
        cell_count, std::numeric_limits<double>::infinity());
    std::vector<std::uint8_t> visited(cell_count, 0U);

    for (std::size_t index = 0; index < cell_count; ++index) {
        if (surface.outlet[index] == 0U) {
            continue;
        }
        const CellId cell{static_cast<std::uint32_t>(index)};
        visited[index] = 1U;
        filled_elevation_m[index] = drainage_elevation_m[index];
        frontier.push({filled_elevation_m[index], cell});
        ++surface.outlet_count;
    }

    if (surface.outlet_count == 0U) {
        CellId sink{0U};
        for (std::size_t index = 1U; index < cell_count; ++index) {
            const CellId candidate{static_cast<std::uint32_t>(index)};
            if (drainage_elevation_m[index] <
                drainage_elevation_m[sink.to_index()]) {
                sink = candidate;
            }
        }
        surface.terminal_sink = sink;
        visited[sink.to_index()] = 1U;
        filled_elevation_m[sink.to_index()] =
            drainage_elevation_m[sink.to_index()];
        frontier.push({filled_elevation_m[sink.to_index()], sink});
    }

    std::size_t visited_count =
        surface.outlet_count == 0U ? 1U : surface.outlet_count;
    while (!frontier.empty()) {
        const FloodNode current = frontier.top();
        frontier.pop();
        for (const CellEdgeGeometry& cell_edge :
             mesh.cell_edges(current.cell)) {
            const std::size_t neighbor_index =
                cell_edge.neighbor.to_index();
            if (visited[neighbor_index] != 0U) {
                continue;
            }
            visited[neighbor_index] = 1U;
            ++visited_count;
            filled_elevation_m[neighbor_index] =
                std::max(drainage_elevation_m[neighbor_index],
                         current.elevation_m);
            frontier.push(
                {filled_elevation_m[neighbor_index], cell_edge.neighbor});
        }
    }
    if (visited_count != cell_count) {
        throw std::logic_error("priority-flood did not visit every cell");
    }

    for (std::size_t index = 0; index < cell_count; ++index) {
        surface.filled_elevation_m[index] =
            static_cast<float>(filled_elevation_m[index]);
        surface.maximum_fill_depth_m =
            std::max(surface.maximum_fill_depth_m,
                     filled_elevation_m[index] -
                         drainage_elevation_m[index]);
    }

    std::deque<CellId> pending;
    std::vector<CellId> members;
    for (std::size_t start_index = 0; start_index < cell_count;
         ++start_index) {
        if (surface.depression_id[start_index] != no_depression ||
            !(filled_elevation_m[start_index] >
              drainage_elevation_m[start_index])) {
            continue;
        }

        const std::uint32_t depression_id =
            static_cast<std::uint32_t>(surface.depressions.size());
        const CellId minimum_cell{
            static_cast<std::uint32_t>(start_index)};
        const double spill_level_m = filled_elevation_m[start_index];
        pending.clear();
        members.clear();
        pending.push_back(minimum_cell);
        surface.depression_id[start_index] = depression_id;

        while (!pending.empty()) {
            const CellId cell = pending.front();
            pending.pop_front();
            members.push_back(cell);
            for (const CellEdgeGeometry& cell_edge :
                 mesh.cell_edges(cell)) {
                const std::size_t neighbor =
                    cell_edge.neighbor.to_index();
                if (surface.depression_id[neighbor] == no_depression &&
                    filled_elevation_m[neighbor] >
                        drainage_elevation_m[neighbor] &&
                    filled_elevation_m[neighbor] == spill_level_m) {
                    surface.depression_id[neighbor] = depression_id;
                    pending.push_back(cell_edge.neighbor);
                }
            }
        }

        CellId spill_cell = CellId::invalid();
        double spill_candidate_elevation_m =
            std::numeric_limits<double>::infinity();
        double raised_area_m2 = 0.0;
        double maximum_fill_depth_m = 0.0;
        for (const CellId cell : members) {
            const std::size_t index = cell.to_index();
            raised_area_m2 += mesh.cell(cell).area_m2;
            maximum_fill_depth_m =
                std::max(maximum_fill_depth_m,
                         filled_elevation_m[index] -
                             drainage_elevation_m[index]);
            for (const CellEdgeGeometry& cell_edge :
                 mesh.cell_edges(cell)) {
                const std::size_t neighbor =
                    cell_edge.neighbor.to_index();
                if (surface.depression_id[neighbor] == depression_id ||
                    filled_elevation_m[neighbor] > spill_level_m) {
                    continue;
                }
                if (better_spill(filled_elevation_m[neighbor],
                                 cell_edge.neighbor,
                                 spill_candidate_elevation_m, spill_cell)) {
                    spill_candidate_elevation_m =
                        filled_elevation_m[neighbor];
                    spill_cell = cell_edge.neighbor;
                }
            }
        }
        if (!spill_cell.is_valid()) {
            throw std::logic_error(
                "filled depression has no spill-cell candidate");
        }

        surface.depressions.push_back(
            {depression_id,
             minimum_cell,
             spill_cell,
             spill_level_m,
             raised_area_m2,
             maximum_fill_depth_m,
             static_cast<std::uint32_t>(members.size())});
    }

    return surface;
}

DrainageState generate_drainage(const PlanetMesh& mesh,
                                const Field3D<float>& hypsometry_m,
                                double sea_level_m,
                                std::size_t worker_count) {
    DrainageState state;
    state.surface = fill_drainage_depressions(
        mesh, hypsometry_m, sea_level_m, worker_count);
    const std::size_t cell_count = mesh.cell_count();
    state.downstream =
        Field2D<std::uint32_t>(cell_count, no_downstream);
    state.basin_id = Field2D<std::uint32_t>(cell_count, no_basin);
    state.catchment_area_m2 = Field2D<float>(cell_count, 0.0F);

    const auto is_terminal = [&](CellId cell) {
        return state.surface.outlet[cell] != 0U ||
               state.surface.terminal_sink == cell;
    };

    // Depression flats have a prescribed, unique exit. Multi-source BFS from
    // every member adjacent to that exit gives the minimum edge distance to
    // the spill; CellId resolves equal-distance choices.
    std::vector<std::vector<CellId>> depression_members(
        state.surface.depressions.size());
    for (std::size_t index = 0; index < cell_count; ++index) {
        const std::uint32_t id = state.surface.depression_id[index];
        if (id != no_depression) {
            depression_members.at(id).push_back(
                CellId{static_cast<std::uint32_t>(index)});
        }
    }
    constexpr std::uint32_t infinite_distance =
        std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> flat_distance(
        cell_count, infinite_distance);
    std::deque<CellId> pending;

    for (const DepressionRecord& depression :
         state.surface.depressions) {
        const auto& members = depression_members[depression.id];
        if (members.empty() || !depression.spill_cell.is_valid()) {
            throw std::logic_error("invalid depression record");
        }
        pending.clear();
        for (const CellId cell : members) {
            flat_distance[cell.to_index()] = infinite_distance;
            if (cells_are_neighbors(
                    mesh, cell, depression.spill_cell)) {
                flat_distance[cell.to_index()] = 0U;
                state.downstream[cell] =
                    depression.spill_cell.value();
                pending.push_back(cell);
            }
        }
        if (pending.empty()) {
            throw std::logic_error(
                "depression spill is not adjacent to a member");
        }
        while (!pending.empty()) {
            const CellId cell = pending.front();
            pending.pop_front();
            const std::uint32_t next_distance =
                flat_distance[cell.to_index()] + 1U;
            for (const CellEdgeGeometry& edge :
                 mesh.cell_edges(cell)) {
                if (state.surface.depression_id[edge.neighbor] ==
                        depression.id &&
                    flat_distance[edge.neighbor.to_index()] ==
                        infinite_distance) {
                    flat_distance[edge.neighbor.to_index()] =
                        next_distance;
                    pending.push_back(edge.neighbor);
                }
            }
        }
        for (const CellId cell : members) {
            const std::uint32_t distance =
                flat_distance[cell.to_index()];
            if (distance == 0U) {
                continue;
            }
            CellId selected = CellId::invalid();
            for (const CellEdgeGeometry& edge :
                 mesh.cell_edges(cell)) {
                if (state.surface.depression_id[edge.neighbor] ==
                        depression.id &&
                    flat_distance[edge.neighbor.to_index()] + 1U ==
                        distance &&
                    (!selected.is_valid() ||
                     edge.neighbor < selected)) {
                    selected = edge.neighbor;
                }
            }
            if (!selected.is_valid()) {
                throw std::logic_error(
                    "depression flat has no route to its spill");
            }
            state.downstream[cell] = selected.value();
        }
    }

    // Strict descent takes priority over flat routing. The comparison is the
    // filled-surface drop per metre across the shared edge.
    for (std::size_t index = 0; index < cell_count; ++index) {
        const CellId cell{static_cast<std::uint32_t>(index)};
        if (is_terminal(cell) ||
            state.surface.depression_id[index] != no_depression) {
            continue;
        }
        const double here = state.surface.filled_elevation_m[index];
        double steepest_slope = -1.0;
        CellId selected = CellId::invalid();
        for (const CellEdgeGeometry& cell_edge :
             mesh.cell_edges(cell)) {
            const double neighbor =
                state.surface
                    .filled_elevation_m[cell_edge.neighbor];
            if (!(neighbor < here)) {
                continue;
            }
            const double distance_m =
                mesh.edge(cell_edge.edge).centroid_distance_m;
            const double slope = (here - neighbor) / distance_m;
            if (slope > steepest_slope ||
                (slope == steepest_slope &&
                 cell_edge.neighbor < selected)) {
                steepest_slope = slope;
                selected = cell_edge.neighbor;
            }
        }
        if (selected.is_valid()) {
            state.downstream[cell] = selected.value();
            continue;
        }

        // A same-level terminal is an exit seed for its plateau.
        for (const CellEdgeGeometry& cell_edge :
             mesh.cell_edges(cell)) {
            if (is_terminal(cell_edge.neighbor) &&
                state.surface.filled_elevation_m[
                    cell_edge.neighbor] <= here &&
                (!selected.is_valid() ||
                 cell_edge.neighbor < selected)) {
                selected = cell_edge.neighbor;
            }
        }
        if (selected.is_valid()) {
            state.downstream[cell] = selected.value();
        }
    }

    // Route each remaining non-depression plateau by BFS distance to any cell
    // in that plateau that already descends or reaches a terminal.
    std::vector<std::uint8_t> plateau_seen(cell_count, 0U);
    std::vector<CellId> plateau_members;
    for (std::size_t start_index = 0; start_index < cell_count;
         ++start_index) {
        const CellId start{static_cast<std::uint32_t>(start_index)};
        if (plateau_seen[start_index] != 0U || is_terminal(start) ||
            state.surface.depression_id[start_index] != no_depression) {
            continue;
        }
        const float plateau_elevation =
            state.surface.filled_elevation_m[start_index];
        plateau_members.clear();
        pending.clear();
        pending.push_back(start);
        plateau_seen[start_index] = 1U;
        while (!pending.empty()) {
            const CellId cell = pending.front();
            pending.pop_front();
            plateau_members.push_back(cell);
            for (const CellEdgeGeometry& edge :
                 mesh.cell_edges(cell)) {
                const std::size_t neighbor =
                    edge.neighbor.to_index();
                if (plateau_seen[neighbor] == 0U &&
                    !is_terminal(edge.neighbor) &&
                    state.surface.depression_id[neighbor] ==
                        no_depression &&
                    state.surface.filled_elevation_m[neighbor] ==
                        plateau_elevation) {
                    plateau_seen[neighbor] = 1U;
                    pending.push_back(edge.neighbor);
                }
            }
        }

        pending.clear();
        for (const CellId cell : plateau_members) {
            flat_distance[cell.to_index()] = infinite_distance;
            if (state.downstream[cell] != no_downstream) {
                flat_distance[cell.to_index()] = 0U;
                pending.push_back(cell);
            }
        }
        if (pending.empty()) {
            throw std::logic_error(
                "filled plateau has no lower route or terminal");
        }
        while (!pending.empty()) {
            const CellId cell = pending.front();
            pending.pop_front();
            const std::uint32_t next_distance =
                flat_distance[cell.to_index()] + 1U;
            for (const CellEdgeGeometry& edge :
                 mesh.cell_edges(cell)) {
                const std::size_t neighbor =
                    edge.neighbor.to_index();
                if (state.surface.depression_id[neighbor] ==
                        no_depression &&
                    !is_terminal(edge.neighbor) &&
                    state.surface.filled_elevation_m[neighbor] ==
                        plateau_elevation &&
                    flat_distance[neighbor] == infinite_distance) {
                    flat_distance[neighbor] = next_distance;
                    pending.push_back(edge.neighbor);
                }
            }
        }
        for (const CellId cell : plateau_members) {
            if (state.downstream[cell] != no_downstream) {
                continue;
            }
            const std::uint32_t distance =
                flat_distance[cell.to_index()];
            CellId selected = CellId::invalid();
            for (const CellEdgeGeometry& edge :
                 mesh.cell_edges(cell)) {
                const std::size_t neighbor =
                    edge.neighbor.to_index();
                if (state.surface.depression_id[neighbor] ==
                        no_depression &&
                    state.surface.filled_elevation_m[neighbor] ==
                        plateau_elevation &&
                    flat_distance[neighbor] + 1U == distance &&
                    (!selected.is_valid() ||
                     edge.neighbor < selected)) {
                    selected = edge.neighbor;
                }
            }
            if (!selected.is_valid()) {
                throw std::logic_error(
                    "filled flat has no breadth-first downstream");
            }
            state.downstream[cell] = selected.value();
        }
    }

    // Validate references while constructing a fixed source-to-terminal
    // topological order. A short order would prove a cycle.
    std::vector<std::uint32_t> upstream_count(cell_count, 0U);
    std::uint32_t terminal_count = 0U;
    for (std::size_t index = 0; index < cell_count; ++index) {
        const CellId cell{static_cast<std::uint32_t>(index)};
        const std::uint32_t downstream = state.downstream[index];
        if (is_terminal(cell)) {
            if (downstream != no_downstream) {
                ++state.diagnostics.invalid_downstream_count;
            }
            ++terminal_count;
            continue;
        }
        if (downstream >= cell_count ||
            !cells_are_neighbors(
                mesh, cell, CellId{downstream})) {
            ++state.diagnostics.invalid_downstream_count;
            continue;
        }
        ++upstream_count[downstream];
    }
    if (state.diagnostics.invalid_downstream_count != 0U) {
        throw std::logic_error("drainage has invalid downstream references");
    }

    std::priority_queue<std::uint32_t,
                        std::vector<std::uint32_t>,
                        std::greater<>>
        ready;
    for (std::size_t index = 0; index < cell_count; ++index) {
        if (upstream_count[index] == 0U) {
            ready.push(static_cast<std::uint32_t>(index));
        }
    }
    std::vector<CellId> topological_order;
    topological_order.reserve(cell_count);
    while (!ready.empty()) {
        const CellId cell{ready.top()};
        ready.pop();
        topological_order.push_back(cell);
        const std::uint32_t downstream = state.downstream[cell];
        if (downstream == no_downstream) {
            continue;
        }
        if (--upstream_count[downstream] == 0U) {
            ready.push(downstream);
        }
    }
    if (topological_order.size() != cell_count) {
        state.diagnostics.cycle_count = 1U;
        throw std::logic_error("drainage graph contains a cycle");
    }

    // Terminal-first is the graph's natural root order. Reversing the
    // source-to-terminal order above assigns basin IDs with every downstream
    // basin already known.
    for (std::size_t index = 0; index < cell_count; ++index) {
        const CellId cell{static_cast<std::uint32_t>(index)};
        if (is_terminal(cell)) {
            state.basin_id[cell] = cell.value();
        }
    }
    for (auto iterator = topological_order.rbegin();
         iterator != topological_order.rend(); ++iterator) {
        const CellId cell = *iterator;
        if (is_terminal(cell)) {
            continue;
        }
        const std::uint32_t downstream = state.downstream[cell];
        if (state.basin_id[downstream] == no_basin) {
            ++state.diagnostics.unreachable_cell_count;
        } else {
            state.basin_id[cell] = state.basin_id[downstream];
        }
    }
    if (state.diagnostics.unreachable_cell_count != 0U) {
        throw std::logic_error("drainage cell does not reach a terminal");
    }

    std::vector<double> catchment_area_m2(cell_count, 0.0);
    state.diagnostics.total_routed_land_area_m2 =
        reduce_deterministic_blocks<double>(
            mesh.blocks(), worker_count, 0.0,
            [&](std::size_t, const CellBlock& block) {
                double subtotal = 0.0;
                for (std::uint32_t index = block.begin;
                     index < block.end; ++index) {
                    const double area_m2 =
                        mesh.cell(CellId{index}).area_m2 *
                        static_cast<double>(
                            state.surface.land_fraction[index]);
                    catchment_area_m2[index] = area_m2;
                    subtotal += area_m2;
                }
                return subtotal;
            },
            [](double accumulated, const double& next) {
                return accumulated + next;
            });

    // This source-to-terminal order is the reverse of the root-first
    // topological order and therefore accumulates every child before parent.
    for (const CellId cell : topological_order) {
        const std::uint32_t downstream = state.downstream[cell];
        if (downstream != no_downstream) {
            catchment_area_m2[downstream] +=
                catchment_area_m2[cell.to_index()];
        }
    }

    const CatchmentTotals terminal_totals =
        reduce_deterministic_blocks<CatchmentTotals>(
            mesh.blocks(), worker_count, {},
            [&](std::size_t, const CellBlock& block) {
                CatchmentTotals subtotal;
                for (std::uint32_t index = block.begin;
                     index < block.end; ++index) {
                    const CellId cell{index};
                    if (is_terminal(cell)) {
                        subtotal.sum_m2 += catchment_area_m2[index];
                        subtotal.maximum_m2 =
                            std::max(subtotal.maximum_m2,
                                     catchment_area_m2[index]);
                        if (catchment_area_m2[index] > 0.0) {
                            ++subtotal.land_basin_count;
                        }
                    }
                }
                return subtotal;
            },
            [](CatchmentTotals accumulated,
               const CatchmentTotals& next) {
                accumulated.sum_m2 += next.sum_m2;
                accumulated.maximum_m2 =
                    std::max(accumulated.maximum_m2, next.maximum_m2);
                accumulated.land_basin_count += next.land_basin_count;
                return accumulated;
            });
    state.diagnostics.basin_count = terminal_count;
    state.diagnostics.land_basin_count = terminal_totals.land_basin_count;
    state.diagnostics.terminal_catchment_area_m2 =
        terminal_totals.sum_m2;
    state.diagnostics.largest_catchment_area_m2 =
        terminal_totals.maximum_m2;
    const double closure_scale =
        std::max(state.diagnostics.total_routed_land_area_m2, 1.0);
    state.diagnostics.catchment_closure_relative_error =
        std::abs(state.diagnostics.terminal_catchment_area_m2 -
                 state.diagnostics.total_routed_land_area_m2) /
        closure_scale;

    for (std::size_t index = 0; index < cell_count; ++index) {
        const float stored =
            static_cast<float>(catchment_area_m2[index]);
        state.catchment_area_m2[index] = stored;
        state.diagnostics.maximum_catchment_storage_error_m2 =
            std::max(
                state.diagnostics.maximum_catchment_storage_error_m2,
                std::abs(static_cast<double>(stored) -
                         catchment_area_m2[index]));
    }
    return state;
}

}  // namespace planetsim
