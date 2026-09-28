#include "sim/planet/geology/plates.hpp"

#include "sim/core/random/noise.hpp"
#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/geology/geology_random.hpp"
#include "sim/planet/mesh/mesh_dijkstra.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace planetsim {
namespace {

constexpr std::uint32_t maximum_seed_attempts = 128U;

[[nodiscard]] Vec3d uniform_unit_vector(double u, double v) noexcept {
    const double z = 2.0 * u - 1.0;
    const double radial = std::sqrt(std::max(0.0, 1.0 - z * z));
    const double azimuth = 2.0 * std::numbers::pi * v;
    return {radial * std::cos(azimuth), radial * std::sin(azimuth), z};
}

[[nodiscard]] double uniform_between(double unit, double low, double high) noexcept {
    return low + unit * (high - low);
}

[[nodiscard]] CellId nearest_cell(const PlanetMesh& mesh, const Vec3d& point_unit) noexcept {
    CellId best;
    double best_dot = -2.0;
    for (const auto& cell : mesh.cells()) {
        const double alignment = dot(cell.center_unit, point_unit);
        if (alignment > best_dot) {  // strict: ties keep the lower CellId
            best_dot = alignment;
            best = cell.id;
        }
    }
    return best;
}

struct PlateTally {
    std::vector<double> area_m2;
    std::vector<std::uint32_t> cell_count;
};

}  // namespace

std::vector<CellId> choose_plate_seed_cells(const PlanetMesh& mesh, std::uint64_t world_seed,
                                            std::uint32_t plate_count,
                                            double minimum_spacing_factor) {
    if (plate_count == 0U || plate_count > mesh.cell_count()) {
        throw std::invalid_argument("plate count must be in [1, cell count]");
    }
    if (!(minimum_spacing_factor >= 0.0) || !std::isfinite(minimum_spacing_factor)) {
        throw std::invalid_argument("plate seed spacing factor must be finite and non-negative");
    }
    // sqrt(4 pi / plate_count) is the side of a square holding 1/plate_count
    // of the unit sphere: the characteristic spacing of evenly spread seeds.
    const double minimum_spacing_rad =
        minimum_spacing_factor * std::sqrt(4.0 * std::numbers::pi / static_cast<double>(plate_count));
    const double maximum_alignment = std::cos(std::min(minimum_spacing_rad, std::numbers::pi));
    std::vector<CellId> seeds;
    std::vector<std::uint8_t> taken(mesh.cell_count(), 0U);
    seeds.reserve(plate_count);
    for (std::uint32_t plate = 0; plate < plate_count; ++plate) {
        CellId chosen;
        for (std::uint32_t attempt = 0; attempt < maximum_seed_attempts; ++attempt) {
            const Vec3d point = uniform_unit_vector(
                geology_random_unit(world_seed, GeologyDraw::plate_seed_point, plate, 2U * attempt),
                geology_random_unit(world_seed, GeologyDraw::plate_seed_point, plate,
                                    2U * attempt + 1U));
            const CellId candidate = nearest_cell(mesh, point);
            const Vec3d candidate_center = mesh.cell(candidate).center_unit;
            const bool too_close =
                std::any_of(seeds.begin(), seeds.end(), [&](CellId other) {
                    return dot(mesh.cell(other).center_unit, candidate_center) > maximum_alignment;
                });
            if (taken[candidate.to_index()] == 0U && !too_close) {
                chosen = candidate;
                break;
            }
        }
        if (!chosen.is_valid()) {
            throw std::runtime_error("could not place a plate seed cell at the minimum spacing");
        }
        taken[chosen.to_index()] = 1U;
        seeds.push_back(chosen);
    }
    return seeds;
}

Vec3d plate_surface_velocity(const TectonicPlate& plate, const Vec3d& position_unit,
                             double radius_m) noexcept {
    return cross(plate.rotation_pole_unit * plate.angular_speed_rad_s, position_unit * radius_m);
}

void generate_plates(const PlanetMesh& mesh, std::uint64_t world_seed,
                     const GeologyParameters& parameters, GeologyState& geology,
                     std::size_t worker_count) {
    validate_geology_parameters(parameters);
    const std::uint32_t plate_count = parameters.plate_count;
    const std::vector<CellId> seeds = choose_plate_seed_cells(mesh, world_seed, plate_count, parameters.plate_seed_min_spacing_factor);

    geology.plates.assign(plate_count, TectonicPlate{});
    for (std::uint32_t plate = 0; plate < plate_count; ++plate) {
        auto& tectonic = geology.plates[plate];
        tectonic.seed_cell = seeds[plate];
        tectonic.growth_cost_factor = uniform_between(
            geology_random_unit(world_seed, GeologyDraw::plate_growth_factor, plate),
            parameters.plate_growth_factor_min, parameters.plate_growth_factor_max);
        tectonic.rotation_pole_unit = uniform_unit_vector(
            geology_random_unit(world_seed, GeologyDraw::plate_rotation_pole, plate, 0U),
            geology_random_unit(world_seed, GeologyDraw::plate_rotation_pole, plate, 1U));
        // The drawn speed is the Euler rotation's equatorial surface speed.
        const double speed_m_s =
            uniform_between(geology_random_unit(world_seed, GeologyDraw::plate_speed, plate),
                            parameters.plate_speed_min_m_s, parameters.plate_speed_max_m_s);
        tectonic.angular_speed_rad_s = speed_m_s / mesh.radius_m();
    }

    // Per-edge growth-noise factor (1 + a fbm(edge midpoint)), written once
    // per edge by the block owning its first cell.
    std::vector<double> edge_noise_factor(mesh.edge_count(), 1.0);
    const std::uint64_t growth_salt = geology_noise_salt(GeologyNoise::plate_growth);
    for_each_deterministic_block(mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
        for (std::uint32_t index = block.begin; index < block.end; ++index) {
            const CellId cell{index};
            for (const auto& cell_edge : mesh.cell_edges(cell)) {
                const EdgeGeometry& edge = mesh.edge(cell_edge.edge);
                if (edge.first_cell != cell) {
                    continue;
                }
                const Vec3d midpoint = normalized(mesh.cell(edge.first_cell).center_unit +
                                                  mesh.cell(edge.second_cell).center_unit);
                edge_noise_factor[cell_edge.edge.to_index()] =
                    1.0 + parameters.plate_growth_noise_amplitude *
                              fbm(world_seed, growth_salt, midpoint, parameters.plate_growth_noise);
            }
        }
    });

    std::vector<DijkstraSource> sources;
    sources.reserve(plate_count);
    for (std::uint32_t plate = 0; plate < plate_count; ++plate) {
        sources.push_back({seeds[plate], 0.0, plate});
    }
    const DijkstraResult growth = multi_source_dijkstra(
        mesh, sources, [&](CellId, const CellEdgeGeometry& edge, std::uint32_t plate) {
            return mesh.edge(edge.edge).centroid_distance_m *
                   geology.plates[plate].growth_cost_factor *
                   edge_noise_factor[edge.edge.to_index()];
        });

    const std::size_t cell_count = mesh.cell_count();
    geology.plate_id = Field2D<PlateId>(cell_count, PlateId::invalid());
    geology.plate_velocity_east_m_s = Field2D<float>(cell_count, 0.0F);
    geology.plate_velocity_north_m_s = Field2D<float>(cell_count, 0.0F);
    for (std::size_t index = 0; index < cell_count; ++index) {
        if (growth.label[index] == no_dijkstra_label) {
            throw std::runtime_error("plate growth left a cell unassigned");
        }
        geology.plate_id[index] = PlateId{static_cast<PlateId::value_type>(growth.label[index])};
    }

    for_each_deterministic_block(mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
        for (std::uint32_t index = block.begin; index < block.end; ++index) {
            const CellGeometry& cell = mesh.cell(CellId{index});
            const TectonicPlate& plate = geology.plates[geology.plate_id[index].to_index()];
            const Vec3d velocity = plate_surface_velocity(plate, cell.center_unit, mesh.radius_m());
            geology.plate_velocity_east_m_s[index] = static_cast<float>(dot(velocity, cell.east_unit));
            geology.plate_velocity_north_m_s[index] =
                static_cast<float>(dot(velocity, cell.north_unit));
        }
    });

    const PlateTally identity{std::vector<double>(plate_count, 0.0),
                              std::vector<std::uint32_t>(plate_count, 0U)};
    const PlateTally tally = reduce_deterministic_blocks(
        mesh.blocks(), worker_count, identity,
        [&](std::size_t, const CellBlock& block) {
            PlateTally partial = identity;
            for (std::uint32_t index = block.begin; index < block.end; ++index) {
                const std::size_t plate = geology.plate_id[index].to_index();
                partial.area_m2[plate] += mesh.cell(CellId{index}).area_m2;
                ++partial.cell_count[plate];
            }
            return partial;
        },
        [](PlateTally total, const PlateTally& partial) {
            for (std::size_t plate = 0; plate < total.area_m2.size(); ++plate) {
                total.area_m2[plate] += partial.area_m2[plate];
                total.cell_count[plate] += partial.cell_count[plate];
            }
            return total;
        });
    for (std::uint32_t plate = 0; plate < plate_count; ++plate) {
        geology.plates[plate].area_m2 = tally.area_m2[plate];
        geology.plates[plate].cell_count = tally.cell_count[plate];
    }
}

PlateRelativeMotion plate_relative_motion(const PlanetMesh& mesh, const GeologyState& geology,
                                          CellId first, CellId second) {
    const Vec3d first_center = mesh.cell(first).center_unit;
    const Vec3d second_center = mesh.cell(second).center_unit;
    const Vec3d midpoint = normalized(first_center + second_center);
    const Vec3d chord = second_center - first_center;
    const Vec3d normal = normalized(chord - midpoint * dot(chord, midpoint));
    const TectonicPlate& first_plate = geology.plates.at(geology.plate_id[first].to_index());
    const TectonicPlate& second_plate = geology.plates.at(geology.plate_id[second].to_index());
    const Vec3d relative = plate_surface_velocity(second_plate, midpoint, mesh.radius_m()) -
                           plate_surface_velocity(first_plate, midpoint, mesh.radius_m());
    return {relative, -dot(relative, normal), dot(relative, cross(midpoint, normal))};
}

BoundaryClass classify_boundary(double convergence_m_s, double tangential_m_s) noexcept {
    if (convergence_m_s == 0.0 || std::abs(convergence_m_s) < std::abs(tangential_m_s)) {
        return BoundaryClass::transform;
    }
    return convergence_m_s > 0.0 ? BoundaryClass::convergent : BoundaryClass::divergent;
}

std::vector<PlateBoundaryEdge> classify_plate_boundaries(const PlanetMesh& mesh,
                                                         const GeologyState& geology) {
    std::vector<PlateBoundaryEdge> boundaries;
    for (std::size_t index = 0; index < mesh.edge_count(); ++index) {
        const EdgeId edge_id{static_cast<EdgeId::value_type>(index)};
        const EdgeGeometry& edge = mesh.edge(edge_id);
        const PlateId first_plate = geology.plate_id[edge.first_cell];
        const PlateId second_plate = geology.plate_id[edge.second_cell];
        if (first_plate == second_plate) {
            continue;
        }
        const PlateRelativeMotion motion =
            plate_relative_motion(mesh, geology, edge.first_cell, edge.second_cell);
        PlateBoundaryEdge boundary;
        boundary.edge = edge_id;
        boundary.first_cell = edge.first_cell;
        boundary.second_cell = edge.second_cell;
        boundary.first_plate = first_plate;
        boundary.second_plate = second_plate;
        boundary.convergence_m_s = motion.convergence_m_s;
        boundary.tangential_m_s = motion.tangential_m_s;
        boundary.boundary_class = classify_boundary(motion.convergence_m_s, motion.tangential_m_s);
        boundaries.push_back(boundary);
    }
    return boundaries;
}

}  // namespace planetsim
