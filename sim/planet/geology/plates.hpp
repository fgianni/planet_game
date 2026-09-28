#pragma once

#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/geology/geology_state.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace planetsim {

// Seed cells for plate_count plates: keyed random points on the sphere, each
// mapped to the nearest cell centre (ties to the lower CellId). A point that
// lands on an already chosen cell, or whose cell centre is closer to an
// earlier seed than minimum_spacing_factor * sqrt(4 pi / plate_count)
// radians, is redrawn with the next attempt's samples. The spacing rule
// amends task M2-02 §4.3 (approved 2026-09-28) so that no plate is squeezed
// between two nearby seeds (acceptance A3).
[[nodiscard]] std::vector<CellId> choose_plate_seed_cells(const PlanetMesh& mesh,
                                                          std::uint64_t world_seed,
                                                          std::uint32_t plate_count,
                                                          double minimum_spacing_factor);

// Plate regions, rotation vectors and per-cell surface velocity (task M2-02
// §4.3, §4.4). Fills plates, plate_id and the plate velocity fields.
void generate_plates(const PlanetMesh& mesh, std::uint64_t world_seed,
                     const GeologyParameters& parameters, GeologyState& geology,
                     std::size_t worker_count = 1U);

// Surface velocity (m/s, planet-fixed Cartesian) of a plate at a unit position.
[[nodiscard]] Vec3d plate_surface_velocity(const TectonicPlate& plate, const Vec3d& position_unit,
                                           double radius_m) noexcept;

struct PlateRelativeMotion {
    Vec3d relative_velocity;   // second minus first, m/s
    double convergence_m_s = 0.0;
    double tangential_m_s = 0.0;
};

// Relative motion of the plates of two neighbouring cells at their edge
// midpoint, seen from `first` (see PlateBoundaryEdge). Swapping the cells
// negates relative_velocity and the edge normal, so convergence_m_s and
// tangential_m_s (the sense of shear) are unchanged.
[[nodiscard]] PlateRelativeMotion plate_relative_motion(const PlanetMesh& mesh,
                                                        const GeologyState& geology, CellId first,
                                                        CellId second);

// Normal-dominant motion (|convergence| >= |tangential|) is convergent or
// divergent by sign; otherwise transform. Zero relative motion is transform.
[[nodiscard]] BoundaryClass classify_boundary(double convergence_m_s,
                                              double tangential_m_s) noexcept;

// Every edge between two plates, in EdgeId order, classified (§4.5). Crust
// types are filled in later by assign_crust.
[[nodiscard]] std::vector<PlateBoundaryEdge> classify_plate_boundaries(const PlanetMesh& mesh,
                                                                       const GeologyState& geology);

}  // namespace planetsim
