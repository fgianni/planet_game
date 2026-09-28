#include "sim/planet/geology/structural_elevation.hpp"

#include "sim/core/random/noise.hpp"
#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/geology/crust.hpp"
#include "sim/planet/geology/geology_random.hpp"
#include "sim/planet/operators/finite_volume.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace planetsim {
namespace {

// Structures cease to matter beyond this many half-widths:
// exp(-ln 2 * 16) = 1.5e-5.
constexpr double profile_cutoff_half_widths = 4.0;

enum class CrustPair : std::uint8_t { any, continent_continent, ocean_continent, ocean_ocean };
enum class FeatureSide : std::uint8_t { both, oceanic, continental, older, younger };

struct BoundaryFeature {
    BoundaryClass boundary_class;
    CrustPair crust_pair;
    FeatureSide side;
    double amplitude_m;      // signed
    double half_width_m;
    bool noisy;              // multiplied by unit-RMS noise (transform faults)
};

[[nodiscard]] bool matches_pair(const PlateBoundaryEdge& boundary, CrustPair pair) noexcept {
    const bool first_continental = boundary.first_crust == CrustType::continental;
    const bool second_continental = boundary.second_crust == CrustType::continental;
    switch (pair) {
    case CrustPair::any:
        return true;
    case CrustPair::continent_continent:
        return first_continental && second_continental;
    case CrustPair::ocean_continent:
        return first_continental != second_continental;
    case CrustPair::ocean_ocean:
        return !first_continental && !second_continental;
    }
    return false;
}

// The older side of an ocean--ocean boundary subducts; ties go to first_cell.
[[nodiscard]] bool first_is_older(const GeologyState& geology,
                                  const PlateBoundaryEdge& boundary) noexcept {
    return geology.crust_age_s[boundary.first_cell] >= geology.crust_age_s[boundary.second_cell];
}

[[nodiscard]] std::vector<DijkstraSource> feature_sources(const GeologyState& geology,
                                                          const BoundaryFeature& feature) {
    std::vector<DijkstraSource> sources;
    for (std::uint32_t index = 0; index < geology.boundaries.size(); ++index) {
        const PlateBoundaryEdge& boundary = geology.boundaries[index];
        if (boundary.boundary_class != feature.boundary_class ||
            !matches_pair(boundary, feature.crust_pair)) {
            continue;
        }
        bool use_first = true;
        bool use_second = true;
        switch (feature.side) {
        case FeatureSide::both:
            break;
        case FeatureSide::oceanic:
            use_first = boundary.first_crust == CrustType::oceanic;
            use_second = boundary.second_crust == CrustType::oceanic;
            break;
        case FeatureSide::continental:
            use_first = boundary.first_crust == CrustType::continental;
            use_second = boundary.second_crust == CrustType::continental;
            break;
        case FeatureSide::older:
            use_first = first_is_older(geology, boundary);
            use_second = !use_first;
            break;
        case FeatureSide::younger:
            use_first = !first_is_older(geology, boundary);
            use_second = !use_first;
            break;
        }
        if (use_first) {
            sources.push_back({boundary.first_cell, 0.0, index});
        }
        if (use_second) {
            sources.push_back({boundary.second_cell, 0.0, index});
        }
    }
    return sources;
}

[[nodiscard]] double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2U;
    return values.size() % 2U == 1U ? values[middle] : 0.5 * (values[middle - 1U] + values[middle]);
}

[[nodiscard]] double relevant_speed_m_s(const PlateBoundaryEdge& boundary) noexcept {
    switch (boundary.boundary_class) {
    case BoundaryClass::convergent:
        return boundary.convergence_m_s;
    case BoundaryClass::divergent:
        return -boundary.convergence_m_s;
    case BoundaryClass::transform:
        return std::abs(boundary.tangential_m_s);
    case BoundaryClass::none:
        break;
    }
    return 0.0;
}

}  // namespace

DijkstraResult along_mesh_distance(const PlanetMesh& mesh, std::span<const DijkstraSource> sources,
                                   double maximum_distance_m) {
    return multi_source_dijkstra(
        mesh, sources,
        [&](CellId, const CellEdgeGeometry& edge, std::uint32_t) {
            return mesh.edge(edge.edge).centroid_distance_m;
        },
        maximum_distance_m);
}

std::vector<DijkstraSource>
boundary_sources(const GeologyState& geology,
                 const std::function<bool(const PlateBoundaryEdge&)>& accept) {
    std::vector<DijkstraSource> sources;
    for (std::uint32_t index = 0; index < geology.boundaries.size(); ++index) {
        const PlateBoundaryEdge& boundary = geology.boundaries[index];
        if (accept(boundary)) {
            sources.push_back({boundary.first_cell, 0.0, index});
            sources.push_back({boundary.second_cell, 0.0, index});
        }
    }
    return sources;
}

double boundary_profile(double distance_m, double half_width_m) noexcept {
    const double ratio = distance_m / half_width_m;
    return std::exp(-std::numbers::ln2 * ratio * ratio);
}

std::vector<double> boundary_speed_scales(const GeologyState& geology,
                                          const GeologyParameters& parameters) {
    std::vector<double> speeds[4];
    for (const auto& boundary : geology.boundaries) {
        speeds[static_cast<std::size_t>(boundary.boundary_class)].push_back(
            relevant_speed_m_s(boundary));
    }
    double medians[4];
    for (std::size_t index = 0; index < 4U; ++index) {
        medians[index] = median(speeds[index]);
    }
    std::vector<double> scales;
    scales.reserve(geology.boundaries.size());
    for (const auto& boundary : geology.boundaries) {
        const double reference = medians[static_cast<std::size_t>(boundary.boundary_class)];
        const double scale = reference > 0.0 ? relevant_speed_m_s(boundary) / reference : 0.0;
        scales.push_back(std::clamp(scale, 0.0, parameters.boundary_speed_scale_max));
    }
    return scales;
}

double stable_diffusion_step_m2(const PlanetMesh& mesh, std::size_t worker_count) {
    const double minimum_ratio_m2 = reduce_deterministic_blocks(
        mesh.blocks(), worker_count, std::numeric_limits<double>::infinity(),
        [&](std::size_t, const CellBlock& block) {
            double minimum = std::numeric_limits<double>::infinity();
            for (std::uint32_t index = block.begin; index < block.end; ++index) {
                const CellId cell{index};
                double conductance = 0.0;
                for (const auto& edge : mesh.cell_edges(cell)) {
                    const EdgeGeometry& geometry = mesh.edge(edge.edge);
                    conductance += geometry.length_m / geometry.centroid_distance_m;
                }
                minimum = std::min(minimum, mesh.cell(cell).area_m2 / conductance);
            }
            return minimum;
        },
        [](double total, double partial) { return std::min(total, partial); });
    return 0.5 * minimum_ratio_m2;
}

void compute_structural_elevation(const PlanetMesh& mesh, std::uint64_t world_seed,
                                  const GeologyParameters& parameters, GeologyState& geology,
                                  std::size_t worker_count) {
    const std::size_t cell_count = mesh.cell_count();
    if (geology.plate_id.size() != cell_count || geology.crust_type.size() != cell_count ||
        geology.crust_age_s.size() != cell_count) {
        throw std::invalid_argument("structural elevation requires plates, crust type and age");
    }

    // Crust base.
    Field2D<double> elevation(cell_count, 0.0);
    for (std::size_t index = 0; index < cell_count; ++index) {
        elevation[index] = geology.crust_type[index] == CrustType::continental
                               ? parameters.continental_base_elevation_m
                               : -oceanic_depth_m(geology.crust_age_s[index], parameters);
    }

    // Passive margins: distance within the plate and the crust type to the
    // nearest cell of the same plate with the other crust type.
    std::vector<DijkstraSource> margin_sources;
    for (std::uint32_t index = 0; index < cell_count; ++index) {
        const CellId cell{index};
        for (const auto& edge : mesh.cell_edges(cell)) {
            if (geology.plate_id[edge.neighbor] == geology.plate_id[cell] &&
                geology.crust_type[edge.neighbor] != geology.crust_type[cell]) {
                margin_sources.push_back({cell, 0.0, 0U});
                break;
            }
        }
    }
    const double margin_reach_m =
        std::max(parameters.passive_margin_width_m, parameters.continental_slope_width_m);
    const DijkstraResult margin = multi_source_dijkstra(
        mesh, margin_sources,
        [&](CellId from, const CellEdgeGeometry& edge, std::uint32_t) {
            if (geology.plate_id[edge.neighbor] != geology.plate_id[from] ||
                geology.crust_type[edge.neighbor] != geology.crust_type[from]) {
                return std::numeric_limits<double>::infinity();
            }
            return mesh.edge(edge.edge).centroid_distance_m;
        },
        margin_reach_m);
    for (std::size_t index = 0; index < cell_count; ++index) {
        const double distance_m = margin.cost[index];
        if (!std::isfinite(distance_m)) {
            continue;
        }
        const bool continental = geology.crust_type[index] == CrustType::continental;
        const double width_m =
            continental ? parameters.passive_margin_width_m : parameters.continental_slope_width_m;
        if (distance_m < width_m) {
            elevation[index] = parameters.shelf_edge_elevation_m +
                               (elevation[index] - parameters.shelf_edge_elevation_m) *
                                   (distance_m / width_m);
        }
    }

    // Boundary structures.
    const BoundaryFeature features[] = {
        {BoundaryClass::convergent, CrustPair::continent_continent, FeatureSide::both,
         parameters.collision_mountain_height_m, parameters.collision_mountain_half_width_m, false},
        {BoundaryClass::convergent, CrustPair::ocean_continent, FeatureSide::oceanic,
         -parameters.trench_depth_m, parameters.trench_half_width_m, false},
        {BoundaryClass::convergent, CrustPair::ocean_continent, FeatureSide::continental,
         parameters.cordillera_height_m, parameters.cordillera_half_width_m, false},
        {BoundaryClass::convergent, CrustPair::ocean_ocean, FeatureSide::older,
         -parameters.trench_depth_m, parameters.trench_half_width_m, false},
        {BoundaryClass::convergent, CrustPair::ocean_ocean, FeatureSide::younger,
         parameters.island_arc_height_m, parameters.island_arc_half_width_m, false},
        {BoundaryClass::divergent, CrustPair::continent_continent, FeatureSide::both,
         -parameters.rift_depth_m, parameters.rift_half_width_m, false},
        {BoundaryClass::transform, CrustPair::any, FeatureSide::both,
         parameters.transform_fault_amplitude_m, parameters.transform_fault_half_width_m, true},
    };
    const std::vector<double> scales = boundary_speed_scales(geology, parameters);
    const std::uint64_t fault_salt = geology_noise_salt(GeologyNoise::transform_fault);
    for (const auto& feature : features) {
        const std::vector<DijkstraSource> sources = feature_sources(geology, feature);
        if (sources.empty()) {
            continue;
        }
        const DijkstraResult distance = along_mesh_distance(
            mesh, sources, profile_cutoff_half_widths * feature.half_width_m);
        for_each_deterministic_block(mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::uint32_t index = block.begin; index < block.end; ++index) {
                if (distance.label[index] == no_dijkstra_label) {
                    continue;
                }
                double contribution = feature.amplitude_m * scales[distance.label[index]] *
                                      boundary_profile(distance.cost[index], feature.half_width_m);
                if (feature.noisy) {
                    contribution *= normalized_fbm(world_seed, fault_salt,
                                                   mesh.cell(CellId{index}).center_unit,
                                                   parameters.transform_fault_noise);
                }
                elevation[index] += contribution;
            }
        });
    }

    // Roughness.
    const std::uint64_t roughness_salt = geology_noise_salt(GeologyNoise::roughness);
    for_each_deterministic_block(mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
        for (std::uint32_t index = block.begin; index < block.end; ++index) {
            const double amplitude_m = geology.crust_type[index] == CrustType::continental
                                           ? parameters.continental_roughness_amplitude_m
                                           : parameters.oceanic_roughness_amplitude_m;
            elevation[index] += amplitude_m * normalized_fbm(world_seed, roughness_salt,
                                                             mesh.cell(CellId{index}).center_unit,
                                                             parameters.roughness_noise);
        }
    });

    // Diffusive erosion approximation: explicit steps of
    // h += kappa dt lap(h), each split into equal stable sub-steps.
    const double step_m2 = parameters.erosion_diffusivity_m2_s * parameters.erosion_step_s;
    if (parameters.erosion_step_count > 0U && step_m2 > 0.0) {
        const double stable_m2 = stable_diffusion_step_m2(mesh, worker_count);
        const auto substeps = static_cast<std::uint32_t>(std::max(1.0, std::ceil(step_m2 / stable_m2)));
        const double substep_m2 = step_m2 / static_cast<double>(substeps);
        Field2D<double> tendency(cell_count, 0.0);
        for (std::uint32_t step = 0; step < parameters.erosion_step_count * substeps; ++step) {
            laplacian(mesh, elevation, tendency, worker_count);
            for (std::size_t index = 0; index < cell_count; ++index) {
                elevation[index] += substep_m2 * tendency[index];
            }
        }
    }

    geology.structural_elevation_m = Field2D<float>(cell_count, 0.0F);
    for (std::size_t index = 0; index < cell_count; ++index) {
        if (!std::isfinite(elevation[index])) {
            throw std::runtime_error("structural elevation is not finite");
        }
        geology.structural_elevation_m[index] = static_cast<float>(elevation[index]);
    }

    // Nearest plate boundary of any class, for diagnostics and export.
    const std::vector<DijkstraSource> all_boundaries =
        boundary_sources(geology, [](const PlateBoundaryEdge&) { return true; });
    const DijkstraResult nearest = along_mesh_distance(mesh, all_boundaries);
    geology.nearest_boundary_class = Field2D<BoundaryClass>(cell_count, BoundaryClass::none);
    geology.nearest_boundary_distance_m =
        Field2D<float>(cell_count, std::numeric_limits<float>::infinity());
    for (std::size_t index = 0; index < cell_count; ++index) {
        if (nearest.label[index] == no_dijkstra_label) {
            continue;
        }
        geology.nearest_boundary_class[index] = geology.boundaries[nearest.label[index]].boundary_class;
        geology.nearest_boundary_distance_m[index] = static_cast<float>(nearest.cost[index]);
    }
}

}  // namespace planetsim
