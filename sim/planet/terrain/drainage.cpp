#include "sim/planet/terrain/drainage.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
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

}  // namespace planetsim
