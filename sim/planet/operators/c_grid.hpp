#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/core/math/vec3d.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace planetsim {

// The C-grid of the Voronoi mesh (ADR-0011 §3.2 B, §4.2): velocities are
// scalars normal to the edges, vorticity lives at the corners (the dual
// triangles), mass at the cells. The geometry is built once from a mesh and
// is immutable; the mesh itself is unchanged.
//
// Conventions (task M6-01):
// - n_e points from the edge's first cell to its second (ADR-0002);
// - t_e = k × n_e at the edge's velocity point, the midpoint of the Voronoi
//   edge, and points from vertex[0] to vertex[1];
// - corner fields are Field2D sized corner_count(), indexed by CornerIndex.

struct CGridEdge {
    std::array<CornerIndex, 2> vertex{};
    Vec3d midpoint_unit;   // velocity point: midpoint of the Voronoi edge
    Vec3d normal_unit;     // n_e there, first cell → second cell
    Vec3d tangent_unit;    // t_e = k × n_e
    std::uint32_t weight_offset = 0;   // into the tangential weights
    std::uint32_t weight_count = 0;
};

struct CGridCorner {
    std::array<CellId, 3> cell{};
    // R_iv: the part of cell[k] nearest the corner (two spherical triangles:
    // cell centre, bisector point of each edge, corner).
    std::array<double, 3> kite_area_m2{};
    std::array<EdgeId, 3> edge{};
    // +1 when n_e turns counter-clockwise about the corner (the corner is
    // the edge's vertex[1]), −1 otherwise: the sign of u_e in the corner's
    // circulation.
    std::array<std::int8_t, 3> edge_sign{};
    double area_m2 = 0.0;   // A_v = Σ kites
    // Barycentric weights of the corner point in the triangle of its three
    // cell centres (gnomonic projection about the corner): exact for linear
    // fields, for interpolating values whose gradient is needed.
    std::array<double, 3> interpolation_weight{};
};

// The TRiSK terms of u⊥_e = (1/d_e) Σ W_ee' l_e' u_e', stored as two
// parallel arrays (edges and weights). The kite fractions in the weights
// are taken over the cell's kite sum rather than its stored area, so they
// sum to one exactly and the weights are antisymmetric to rounding.

inline constexpr std::size_t deterministic_edge_block_size = 256U;

class CGridGeometry {
  public:
    [[nodiscard]] static CGridGeometry build(const PlanetMesh& mesh);

    [[nodiscard]] std::size_t edge_count() const noexcept { return edges_.size(); }
    [[nodiscard]] std::size_t corner_count() const noexcept { return corners_.size(); }
    [[nodiscard]] std::span<const CGridEdge> edges() const noexcept { return edges_; }
    [[nodiscard]] std::span<const CGridCorner> corners() const noexcept { return corners_; }
    [[nodiscard]] const CGridEdge& edge(EdgeId id) const { return edges_.at(id.to_index()); }
    // e' and W_ee' for the terms of edge `id`, in matching order.
    [[nodiscard]] std::span<const EdgeId> tangential_weight_edges(EdgeId id) const;
    [[nodiscard]] std::span<const double> tangential_weights(EdgeId id) const;
    // Fixed blocks of edges and of corners for deterministic parallel work.
    [[nodiscard]] std::span<const CellBlock> edge_blocks() const noexcept { return edge_blocks_; }
    [[nodiscard]] std::span<const CellBlock> corner_blocks() const noexcept {
        return corner_blocks_;
    }
    [[nodiscard]] std::size_t allocated_bytes() const noexcept;

  private:
    std::vector<CGridEdge> edges_;
    std::vector<CGridCorner> corners_;
    std::vector<EdgeId> weight_edges_;
    std::vector<double> weights_;
    std::vector<CellBlock> edge_blocks_;
    std::vector<CellBlock> corner_blocks_;
};

// u⊥_e: the tangential (t_e) component of the velocity whose normal
// components are u (TRiSK).
void tangential_velocity(const PlanetMesh& mesh, const CGridGeometry& grid,
                         const EdgeField<double>& normal_velocity,
                         EdgeField<double>& tangential, std::size_t worker_count = 1U);

// ζ_v = (1/A_v) Σ_e s_ve d_e u_e, at the corners.
void relative_vorticity(const PlanetMesh& mesh, const CGridGeometry& grid,
                        const EdgeField<double>& normal_velocity, Field2D<double>& vorticity,
                        std::size_t worker_count = 1U);

// K_i = (1/A_i) Σ_e (l_e d_e / 4) u_e², at the cells.
void kinetic_energy(const PlanetMesh& mesh, const EdgeField<double>& normal_velocity,
                    Field2D<double>& kinetic, std::size_t worker_count = 1U);

// Perot: U_i = (1/A_i) Σ_e l_e n_e,i u_e (x_e − x_i), as east/north components.
void reconstruct_cell_vector(const PlanetMesh& mesh, const CGridGeometry& grid,
                             const EdgeField<double>& normal_velocity, Field2D<double>& east,
                             Field2D<double>& north, std::size_t worker_count = 1U);

// (φ_second − φ_first) / d_e.
void normal_gradient(const PlanetMesh& mesh, const CGridGeometry& grid,
                     const Field2D<double>& scalar,
                     EdgeField<double>& gradient, std::size_t worker_count = 1U);

// (ψ_vertex[1] − ψ_vertex[0]) / l_e for a corner field ψ.
void tangential_gradient(const PlanetMesh& mesh, const CGridGeometry& grid,
                         const Field2D<double>& corner_scalar, EdgeField<double>& gradient,
                         std::size_t worker_count = 1U);

// ψ_v = Σ_i R_iv ψ_i / A_v: the conservative, area-weighted corner value
// (TRiSK's layer thickness at corners). It sits at the kites' centroid, not
// at the corner, so its differences are not gradients; use
// interpolate_to_corner for those.
void cell_to_corner(const CGridGeometry& grid, const Field2D<double>& scalar,
                    Field2D<double>& corner_scalar, std::size_t worker_count = 1U);

// ψ_v = Σ_k w_k ψ_cell[k] with the barycentric weights: exact for linear
// fields, so tangential_gradient of it is a consistent gradient.
void interpolate_to_corner(const CGridGeometry& grid, const Field2D<double>& scalar,
                           Field2D<double>& corner_scalar, std::size_t worker_count = 1U);

}  // namespace planetsim
