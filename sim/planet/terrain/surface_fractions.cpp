#include "sim/planet/terrain/surface_fractions.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/terrain/hypsometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace planetsim {
namespace {

constexpr std::uint32_t no_component = std::numeric_limits<std::uint32_t>::max();

void require_shape(const PlanetMesh& mesh, const Field3D<float>& hypsometry_m) {
    if (hypsometry_m.layer_count() != hypsometry_quantile_count ||
        hypsometry_m.cell_count() != mesh.cell_count()) {
        throw std::invalid_argument("hypsometry field has the wrong shape");
    }
}

struct OceanComponent {
    std::vector<std::uint8_t> connected;
    std::uint32_t component_count = 0;
    std::uint32_t cell_count = 0;
};

// The deepest cell: lowest bottom quantile, ties to the lower CellId.
[[nodiscard]] CellId find_ocean_anchor(const PlanetMesh& mesh, std::span<const float> lowest_quantile,
                                       std::size_t worker_count) {
    struct Deepest {
        float elevation_m = std::numeric_limits<float>::infinity();
        std::uint32_t cell = std::numeric_limits<std::uint32_t>::max();
    };
    const auto deeper = [](const Deepest& left, const Deepest& right) {
        return left.elevation_m < right.elevation_m ||
               (left.elevation_m == right.elevation_m && left.cell < right.cell);
    };
    const Deepest deepest = reduce_deterministic_blocks(
        mesh.blocks(), worker_count, Deepest{},
        [&](std::size_t, const CellBlock& block) {
            Deepest partial;
            for (std::uint32_t index = block.begin; index < block.end; ++index) {
                const Deepest candidate{lowest_quantile[index], index};
                if (deeper(candidate, partial)) {
                    partial = candidate;
                }
            }
            return partial;
        },
        [&](Deepest total, const Deepest& partial) { return deeper(partial, total) ? partial : total; });
    return CellId{deepest.cell};
}

// Labels the components of below-sea-level cells in CellId order; the ocean
// is the one containing the anchor (the deepest cell).
[[nodiscard]] OceanComponent find_ocean_component(const PlanetMesh& mesh,
                                                  std::span<const float> lowest_quantile,
                                                  double sea_level_m, CellId anchor) {
    const std::size_t cell_count = mesh.cell_count();
    std::vector<std::uint32_t> component(cell_count, no_component);
    std::uint32_t component_count = 0;
    std::vector<CellId> stack;
    for (std::uint32_t start = 0; start < cell_count; ++start) {
        if (component[start] != no_component ||
            !(static_cast<double>(lowest_quantile[start]) < sea_level_m)) {
            continue;
        }
        component[start] = component_count;
        stack.assign(1U, CellId{start});
        while (!stack.empty()) {
            const CellId cell = stack.back();
            stack.pop_back();
            for (const auto& edge : mesh.cell_edges(cell)) {
                const std::size_t neighbor = edge.neighbor.to_index();
                if (component[neighbor] == no_component &&
                    static_cast<double>(lowest_quantile[neighbor]) < sea_level_m) {
                    component[neighbor] = component_count;
                    stack.push_back(edge.neighbor);
                }
            }
        }
        ++component_count;
    }

    OceanComponent ocean{std::vector<std::uint8_t>(cell_count, 0U), component_count, 0U};
    const std::uint32_t ocean_label = component[anchor.to_index()];
    if (ocean_label == no_component) {
        return ocean;  // nothing is below sea level
    }
    for (std::size_t index = 0; index < cell_count; ++index) {
        if (component[index] == ocean_label) {
            ocean.connected[index] = 1U;
            ++ocean.cell_count;
        }
    }
    return ocean;
}

struct AreaTotals {
    double land_m2 = 0.0;
    double ocean_m2 = 0.0;
    double total_m2 = 0.0;
};

[[nodiscard]] AreaTotals sum_areas(const PlanetMesh& mesh, const Field3D<float>& hypsometry_m,
                                   const std::vector<std::uint8_t>& connected, double sea_level_m,
                                   std::size_t worker_count, Field2D<float>* land_fraction,
                                   Field2D<float>* ocean_fraction) {
    return reduce_deterministic_blocks(
        mesh.blocks(), worker_count, AreaTotals{},
        [&](std::size_t, const CellBlock& block) {
            AreaTotals partial;
            for (std::uint32_t index = block.begin; index < block.end; ++index) {
                const CellId cell{index};
                const double ocean =
                    connected[index] != 0U
                        ? below_fraction(cell_hypsometry(hypsometry_m, cell), sea_level_m)
                        : 0.0;
                const double area = mesh.cell(cell).area_m2;
                partial.ocean_m2 += ocean * area;
                partial.land_m2 += (1.0 - ocean) * area;
                partial.total_m2 += area;
                if (land_fraction != nullptr) {
                    (*land_fraction)[index] = static_cast<float>(1.0 - ocean);
                    (*ocean_fraction)[index] = static_cast<float>(ocean);
                }
            }
            return partial;
        },
        [](AreaTotals total, const AreaTotals& partial) {
            total.land_m2 += partial.land_m2;
            total.ocean_m2 += partial.ocean_m2;
            total.total_m2 += partial.total_m2;
            return total;
        });
}

}  // namespace

SurfaceFractions compute_surface_fractions(const PlanetMesh& mesh,
                                           const Field3D<float>& hypsometry_m, double sea_level_m,
                                           std::size_t worker_count) {
    require_shape(mesh, hypsometry_m);
    const std::span<const float> lowest = hypsometry_m.layer(0);
    const OceanComponent ocean = find_ocean_component(
        mesh, lowest, sea_level_m, find_ocean_anchor(mesh, lowest, worker_count));
    SurfaceFractions fractions;
    fractions.land_fraction = Field2D<float>(mesh.cell_count(), 1.0F);
    fractions.ocean_fraction = Field2D<float>(mesh.cell_count(), 0.0F);
    fractions.ocean_connected = Field2D<std::uint8_t>(std::vector<std::uint8_t>(ocean.connected));
    const AreaTotals totals =
        sum_areas(mesh, hypsometry_m, ocean.connected, sea_level_m, worker_count,
                  &fractions.land_fraction, &fractions.ocean_fraction);
    fractions.land_area_fraction = totals.land_m2 / totals.total_m2;
    fractions.ocean_area_m2 = totals.ocean_m2;
    fractions.total_area_m2 = totals.total_m2;
    fractions.below_sea_level_components = ocean.component_count;
    fractions.ocean_cell_count = ocean.cell_count;
    return fractions;
}

double land_area_fraction(const PlanetMesh& mesh, const Field3D<float>& hypsometry_m,
                          double sea_level_m, std::size_t worker_count) {
    require_shape(mesh, hypsometry_m);
    const std::span<const float> lowest = hypsometry_m.layer(0);
    const OceanComponent ocean = find_ocean_component(
        mesh, lowest, sea_level_m, find_ocean_anchor(mesh, lowest, worker_count));
    const AreaTotals totals = sum_areas(mesh, hypsometry_m, ocean.connected, sea_level_m,
                                        worker_count, nullptr, nullptr);
    return totals.land_m2 / totals.total_m2;
}

SeaLevelSolution solve_sea_level(const PlanetMesh& mesh, const Field3D<float>& hypsometry_m,
                                 double target_land_fraction, std::size_t worker_count) {
    require_shape(mesh, hypsometry_m);
    if (!(target_land_fraction >= 0.0 && target_land_fraction <= 1.0)) {
        throw std::invalid_argument("target land fraction must be in [0, 1]");
    }
    struct Extremes {
        double lowest = std::numeric_limits<double>::infinity();
        double highest = -std::numeric_limits<double>::infinity();
    };
    const std::span<const float> lowest_layer = hypsometry_m.layer(0);
    const std::span<const float> highest_layer = hypsometry_m.layer(hypsometry_quantile_count - 1U);
    const Extremes extremes = reduce_deterministic_blocks(
        mesh.blocks(), worker_count, Extremes{},
        [&](std::size_t, const CellBlock& block) {
            Extremes partial;
            for (std::uint32_t index = block.begin; index < block.end; ++index) {
                partial.lowest = std::min(partial.lowest, static_cast<double>(lowest_layer[index]));
                partial.highest =
                    std::max(partial.highest, static_cast<double>(highest_layer[index]));
            }
            return partial;
        },
        [](Extremes total, const Extremes& partial) {
            total.lowest = std::min(total.lowest, partial.lowest);
            total.highest = std::max(total.highest, partial.highest);
            return total;
        });

    SeaLevelSolution solution;
    solution.target_land_fraction = target_land_fraction;
    double low = extremes.lowest - 1.0;   // no cell below: land fraction 1
    double high = extremes.highest + 1.0; // every cell submerged and connected: 0
    if (target_land_fraction <= 0.0) {
        solution.sea_level_m = high;
    } else if (target_land_fraction >= 1.0) {
        solution.sea_level_m = low;
    } else {
        for (std::uint32_t iteration = 0; iteration < sea_level_bisection_iterations; ++iteration) {
            const double middle = low + 0.5 * (high - low);
            if (land_area_fraction(mesh, hypsometry_m, middle, worker_count) >= target_land_fraction) {
                low = middle;
            } else {
                high = middle;
            }
        }
        solution.sea_level_m = low;
        const double above = land_area_fraction(mesh, hypsometry_m, high, worker_count);
        const double below = land_area_fraction(mesh, hypsometry_m, low, worker_count);
        if (below - above > sea_level_target_tolerance) {
            solution.target_in_jump = true;
            solution.land_fraction_above_jump = above;
            const CellId anchor = find_ocean_anchor(mesh, lowest_layer, worker_count);
            const OceanComponent low_ocean = find_ocean_component(mesh, lowest_layer, low, anchor);
            const OceanComponent high_ocean = find_ocean_component(mesh, lowest_layer, high, anchor);
            solution.jump_changes_ocean_connectivity = low_ocean.connected != high_ocean.connected;
        }
    }
    solution.achieved_land_fraction =
        land_area_fraction(mesh, hypsometry_m, solution.sea_level_m, worker_count);
    return solution;
}

}  // namespace planetsim
