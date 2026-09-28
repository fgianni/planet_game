#include "sim/planet/terrain/terrain_diagnostics.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/terrain/hypsometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace planetsim {
namespace {

struct OceanMoments {
    double ocean_m2 = 0.0;
    double shallow_m2 = 0.0;
    double deep_m2 = 0.0;
    double elevation_sum = 0.0;         // area-weighted
    double elevation_square_sum = 0.0;  // area-weighted
    double continental_m2 = 0.0;
    double total_m2 = 0.0;
};

// Area fraction and first two moments of the part of a cell's quantile curve
// below `level_m`, treating each segment as uniformly distributed.
void add_submerged_moments(const HypsometryQuantiles& quantiles, double level_m, double area_m2,
                           OceanMoments& moments) {
    constexpr std::size_t segments = hypsometry_quantile_count - 1U;
    const double segment_mass = 1.0 / static_cast<double>(segments);
    for (std::size_t segment = 0; segment < segments; ++segment) {
        const double low = quantiles[segment];
        const double high = quantiles[segment + 1U];
        if (!(low < level_m)) {
            break;
        }
        double mass = segment_mass;
        double top = high;
        if (high > level_m) {
            top = level_m;
            mass *= (top - low) / (high - low);
        }
        const double weight = mass * area_m2;
        moments.ocean_m2 += weight;
        moments.elevation_sum += weight * 0.5 * (low + top);
        moments.elevation_square_sum += weight * (low * low + low * top + top * top) / 3.0;
    }
}

}  // namespace

TerrainDiagnostics compute_terrain_diagnostics(const PlanetMesh& mesh, const GeologyState& geology,
                                               const Field3D<float>& hypsometry_m,
                                               double sea_level_m, std::size_t worker_count) {
    TerrainDiagnostics diagnostics;
    const std::size_t cell_count = mesh.cell_count();
    const SurfaceFractions fractions =
        compute_surface_fractions(mesh, hypsometry_m, sea_level_m, worker_count);

    diagnostics.plate_count = static_cast<std::uint32_t>(geology.plates.size());
    for (const auto& plate : geology.plates) {
        diagnostics.plate_area_fractions.push_back(plate.area_m2 / fractions.total_area_m2);
    }
    for (const auto& boundary : geology.boundaries) {
        const auto index = static_cast<std::size_t>(boundary.boundary_class);
        diagnostics.boundary_length_m[index] += mesh.edge(boundary.edge).length_m;
        ++diagnostics.boundary_edge_count[index];
    }

    const OceanMoments moments = reduce_deterministic_blocks(
        mesh.blocks(), worker_count, OceanMoments{},
        [&](std::size_t, const CellBlock& block) {
            OceanMoments partial;
            for (std::uint32_t index = block.begin; index < block.end; ++index) {
                const CellId cell{index};
                const double area = mesh.cell(cell).area_m2;
                partial.total_m2 += area;
                if (geology.crust_type[cell] == CrustType::continental) {
                    partial.continental_m2 += area;
                }
                if (fractions.ocean_connected[index] == 0U) {
                    continue;
                }
                const HypsometryQuantiles quantiles = cell_hypsometry(hypsometry_m, cell);
                add_submerged_moments(quantiles, sea_level_m, area, partial);
                partial.shallow_m2 += area * (below_fraction(quantiles, sea_level_m) -
                                              below_fraction(quantiles, sea_level_m - 200.0));
                partial.deep_m2 += area * below_fraction(quantiles, sea_level_m - 4'000.0);
            }
            return partial;
        },
        [](OceanMoments total, const OceanMoments& partial) {
            total.ocean_m2 += partial.ocean_m2;
            total.shallow_m2 += partial.shallow_m2;
            total.deep_m2 += partial.deep_m2;
            total.elevation_sum += partial.elevation_sum;
            total.elevation_square_sum += partial.elevation_square_sum;
            total.continental_m2 += partial.continental_m2;
            total.total_m2 += partial.total_m2;
            return total;
        });
    diagnostics.continental_area_fraction = moments.continental_m2 / moments.total_m2;
    diagnostics.sea_level_m = sea_level_m;
    diagnostics.land_area_fraction = fractions.land_area_fraction;
    diagnostics.inland_depression_count =
        fractions.below_sea_level_components > 0U ? fractions.below_sea_level_components - 1U : 0U;
    diagnostics.ocean_area_fraction = moments.ocean_m2 / moments.total_m2;
    if (moments.ocean_m2 > 0.0) {
        diagnostics.ocean_shallower_than_200m_fraction = moments.shallow_m2 / moments.ocean_m2;
        diagnostics.ocean_deeper_than_4000m_fraction = moments.deep_m2 / moments.ocean_m2;
        const double mean = moments.elevation_sum / moments.ocean_m2;
        diagnostics.ocean_elevation_mean_m = mean;
        diagnostics.ocean_elevation_std_m =
            std::sqrt(std::max(0.0, moments.elevation_square_sum / moments.ocean_m2 - mean * mean));
    }

    double oceanic_min = std::numeric_limits<double>::infinity();
    double oceanic_max = -std::numeric_limits<double>::infinity();
    double continental_min = std::numeric_limits<double>::infinity();
    double continental_max = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < cell_count; ++index) {
        const double age_myr = geology.crust_age_s[index] / seconds_per_million_years;
        if (geology.crust_type[index] == CrustType::oceanic) {
            oceanic_min = std::min(oceanic_min, age_myr);
            oceanic_max = std::max(oceanic_max, age_myr);
        } else {
            continental_min = std::min(continental_min, age_myr);
            continental_max = std::max(continental_max, age_myr);
        }
    }
    diagnostics.oceanic_age_min_myr = std::isfinite(oceanic_min) ? oceanic_min : 0.0;
    diagnostics.oceanic_age_max_myr = std::isfinite(oceanic_max) ? oceanic_max : 0.0;
    diagnostics.continental_age_min_myr = std::isfinite(continental_min) ? continental_min : 0.0;
    diagnostics.continental_age_max_myr = std::isfinite(continental_max) ? continental_max : 0.0;

    // Area-weighted percentiles of the cell mean elevation (sorted, ties by CellId).
    std::vector<double> mean_elevation(cell_count);
    for (std::size_t index = 0; index < cell_count; ++index) {
        mean_elevation[index] =
            mean_elevation_m(cell_hypsometry(hypsometry_m, CellId{static_cast<std::uint32_t>(index)}));
    }
    std::vector<std::uint32_t> order(cell_count);
    std::iota(order.begin(), order.end(), 0U);
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t left, std::uint32_t right) {
        return mean_elevation[left] < mean_elevation[right];
    });
    std::size_t percentile = 0;
    double cumulative = 0.0;
    for (const std::uint32_t index : order) {
        cumulative += mesh.cell(CellId{index}).area_m2;
        while (percentile < terrain_elevation_percentiles.size() &&
               cumulative >= terrain_elevation_percentiles[percentile] / 100.0 * moments.total_m2) {
            diagnostics.mean_elevation_percentiles_m[percentile] = mean_elevation[index];
            ++percentile;
        }
    }
    for (; percentile < terrain_elevation_percentiles.size(); ++percentile) {
        diagnostics.mean_elevation_percentiles_m[percentile] = mean_elevation[order.back()];
    }
    return diagnostics;
}

}  // namespace planetsim
