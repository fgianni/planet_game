#pragma once

#include "sim/planet/mesh/planet_mesh.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace planetsim {

inline constexpr std::uint32_t no_dijkstra_label = std::numeric_limits<std::uint32_t>::max();

struct DijkstraSource {
    CellId cell;
    double initial_cost = 0.0;
    std::uint32_t label = 0;
};

struct DijkstraResult {
    std::vector<double> cost;           // +infinity where unreached
    std::vector<std::uint32_t> label;   // label of the source that reached the cell
    std::vector<CellId> predecessor;    // invalid for sources and unreached cells
};

// Multi-source shortest paths over the cell-edge graph.
//
// The queue is ordered by (cost, CellId) and a cell is only relabelled on a
// strict improvement, so the result is a pure function of the mesh, the source
// list and the edge costs. When a cell appears in several sources the lowest
// initial cost wins, then the earliest entry. Sources on distinct cells may be
// given in any order.
//
// edge_cost(CellId from, const CellEdgeGeometry& edge, std::uint32_t label)
// returns the cost of the step from `from` to edge.neighbor for a path grown
// from a source with `label`. It must be non-negative; +infinity blocks the
// step. Cells whose cost would exceed maximum_cost are left unreached.
template <typename EdgeCost>
[[nodiscard]] DijkstraResult
multi_source_dijkstra(const PlanetMesh& mesh, std::span<const DijkstraSource> sources,
                      EdgeCost&& edge_cost,
                      double maximum_cost = std::numeric_limits<double>::infinity()) {
    constexpr double unreached = std::numeric_limits<double>::infinity();
    const std::size_t cell_count = mesh.cell_count();
    DijkstraResult result{std::vector<double>(cell_count, unreached),
                          std::vector<std::uint32_t>(cell_count, no_dijkstra_label),
                          std::vector<CellId>(cell_count, CellId::invalid())};

    using QueueEntry = std::pair<double, CellId::value_type>;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> queue;

    for (const auto& source : sources) {
        if (source.cell.to_index() >= cell_count) {
            throw std::out_of_range("Dijkstra source cell is outside the mesh");
        }
        if (!(source.initial_cost >= 0.0) || std::isinf(source.initial_cost)) {
            throw std::invalid_argument("Dijkstra source cost must be finite and non-negative");
        }
        const std::size_t index = source.cell.to_index();
        if (source.initial_cost > maximum_cost || !(source.initial_cost < result.cost[index])) {
            continue;
        }
        result.cost[index] = source.initial_cost;
        result.label[index] = source.label;
        queue.emplace(source.initial_cost, source.cell.value());
    }

    std::vector<std::uint8_t> settled(cell_count, 0U);
    while (!queue.empty()) {
        const auto [cost, cell_value] = queue.top();
        queue.pop();
        const CellId cell{cell_value};
        const std::size_t index = cell.to_index();
        if (settled[index] != 0U || cost > result.cost[index]) {
            continue;
        }
        settled[index] = 1U;
        const std::uint32_t label = result.label[index];
        for (const auto& edge : mesh.cell_edges(cell)) {
            const std::size_t neighbor = edge.neighbor.to_index();
            if (settled[neighbor] != 0U) {
                continue;
            }
            const double step = edge_cost(cell, edge, label);
            if (std::isnan(step) || step < 0.0) {
                throw std::invalid_argument("Dijkstra edge cost must be non-negative");
            }
            const double candidate = cost + step;
            if (candidate > maximum_cost || !(candidate < result.cost[neighbor])) {
                continue;
            }
            result.cost[neighbor] = candidate;
            result.label[neighbor] = label;
            result.predecessor[neighbor] = cell;
            queue.emplace(candidate, edge.neighbor.value());
        }
    }
    return result;
}

}  // namespace planetsim
