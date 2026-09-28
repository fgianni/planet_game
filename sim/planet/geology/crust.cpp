#include "sim/planet/geology/crust.hpp"

#include "sim/core/random/noise.hpp"
#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/geology/geology_random.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace planetsim {
namespace {

[[nodiscard]] double total_area_m2(const PlanetMesh& mesh, std::size_t worker_count) {
    return reduce_deterministic_blocks(
        mesh.blocks(), worker_count, 0.0,
        [&](std::size_t, const CellBlock& block) {
            double area = 0.0;
            for (std::uint32_t index = block.begin; index < block.end; ++index) {
                area += mesh.cell(CellId{index}).area_m2;
            }
            return area;
        },
        [](double total, double partial) { return total + partial; });
}

}  // namespace

void assign_continental_crust(const PlanetMesh& mesh, std::uint64_t world_seed,
                              const GeologyParameters& parameters, GeologyState& geology,
                              std::size_t worker_count) {
    for (std::uint32_t plate = 0; plate < geology.plates.size(); ++plate) {
        const double unit = geology_random_unit(world_seed, GeologyDraw::continental_bias, plate);
        geology.plates[plate].continental_bias =
            (2.0 * unit - 1.0) * parameters.continental_plate_bias_amplitude;
    }

    const std::size_t cell_count = mesh.cell_count();
    std::vector<double> propensity(cell_count, 0.0);
    const std::uint64_t salt = geology_noise_salt(GeologyNoise::continental_propensity);
    for_each_deterministic_block(mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
        for (std::uint32_t index = block.begin; index < block.end; ++index) {
            const CellGeometry& cell = mesh.cell(CellId{index});
            propensity[index] = fbm(world_seed, salt, cell.center_unit, parameters.continental_noise) +
                                geology.plates[geology.plate_id[index].to_index()].continental_bias;
        }
    });

    std::vector<std::uint32_t> order(cell_count);
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(), [&](std::uint32_t left, std::uint32_t right) {
        if (propensity[left] != propensity[right]) {
            return propensity[left] > propensity[right];
        }
        return left < right;
    });

    geology.crust_type = Field2D<CrustType>(cell_count, CrustType::oceanic);
    const double target_area_m2 = parameters.continental_area_fraction * total_area_m2(mesh, worker_count);
    double continental_area_m2 = 0.0;
    for (const std::uint32_t index : order) {
        if (!(continental_area_m2 < target_area_m2)) {
            break;
        }
        geology.crust_type[index] = CrustType::continental;
        continental_area_m2 += mesh.cell(CellId{index}).area_m2;
    }

    for (auto& boundary : geology.boundaries) {
        boundary.first_crust = geology.crust_type[boundary.first_cell];
        boundary.second_crust = geology.crust_type[boundary.second_cell];
    }
}

double oceanic_depth_m(double crust_age_s, const GeologyParameters& parameters) {
    const double age_myr = std::max(0.0, crust_age_s) / seconds_per_million_years;
    const double transition_myr = parameters.age_depth_transition_s / seconds_per_million_years;
    if (age_myr <= transition_myr) {
        return parameters.ridge_depth_m + parameters.subsidence_m_per_sqrt_myr * std::sqrt(age_myr);
    }
    const double transition_depth_m =
        parameters.ridge_depth_m + parameters.subsidence_m_per_sqrt_myr * std::sqrt(transition_myr);
    const double transition_slope_m_per_myr =
        parameters.subsidence_m_per_sqrt_myr / (2.0 * std::sqrt(transition_myr));
    const double remaining_m = parameters.abyssal_depth_limit_m - transition_depth_m;
    const double timescale_myr = remaining_m / transition_slope_m_per_myr;
    return parameters.abyssal_depth_limit_m -
           remaining_m * std::exp(-(age_myr - transition_myr) / timescale_myr);
}

DijkstraResult assign_crust_age(const PlanetMesh& mesh, std::uint64_t world_seed,
                                const GeologyParameters& parameters, GeologyState& geology) {
    const std::size_t cell_count = mesh.cell_count();

    std::vector<DijkstraSource> sources;
    for (std::uint32_t index = 0; index < geology.boundaries.size(); ++index) {
        const PlateBoundaryEdge& boundary = geology.boundaries[index];
        if (boundary.boundary_class != BoundaryClass::divergent) {
            continue;
        }
        sources.push_back({boundary.first_cell, 0.0, index});
        sources.push_back({boundary.second_cell, 0.0, index});
    }
    DijkstraResult spreading = multi_source_dijkstra(
        mesh, sources, [&](CellId from, const CellEdgeGeometry& edge, std::uint32_t) {
            if (geology.plate_id[from] != geology.plate_id[edge.neighbor]) {
                return std::numeric_limits<double>::infinity();
            }
            return mesh.edge(edge.edge).centroid_distance_m;
        });

    geology.crust_age_s = Field2D<float>(cell_count, 0.0F);
    for (std::size_t index = 0; index < cell_count; ++index) {
        if (geology.crust_type[index] != CrustType::oceanic) {
            continue;
        }
        double age_s = parameters.oceanic_age_cap_s;
        if (spreading.label[index] != no_dijkstra_label) {
            const PlateBoundaryEdge& ridge = geology.boundaries[spreading.label[index]];
            const double half_spreading_rate_m_s = -0.5 * ridge.convergence_m_s;
            age_s = std::min(age_s, spreading.cost[index] / half_spreading_rate_m_s);
        }
        geology.crust_age_s[index] = static_cast<float>(age_s);
    }

    // Continental regions, found in CellId order; each is keyed by its lowest
    // CellId, so the age does not depend on traversal order.
    std::vector<std::uint8_t> visited(cell_count, 0U);
    std::vector<CellId> stack;
    for (std::uint32_t start = 0; start < cell_count; ++start) {
        if (visited[start] != 0U || geology.crust_type[start] != CrustType::continental) {
            continue;
        }
        const double unit = geology_random_unit(world_seed, GeologyDraw::continental_age, start);
        const auto age_s = static_cast<float>(
            parameters.continental_age_min_s +
            unit * (parameters.continental_age_max_s - parameters.continental_age_min_s));
        visited[start] = 1U;
        stack.assign(1U, CellId{start});
        while (!stack.empty()) {
            const CellId cell = stack.back();
            stack.pop_back();
            geology.crust_age_s[cell] = age_s;
            for (const auto& edge : mesh.cell_edges(cell)) {
                const std::size_t neighbor = edge.neighbor.to_index();
                if (visited[neighbor] == 0U &&
                    geology.crust_type[neighbor] == CrustType::continental) {
                    visited[neighbor] = 1U;
                    stack.push_back(edge.neighbor);
                }
            }
        }
    }
    return spreading;
}

}  // namespace planetsim
