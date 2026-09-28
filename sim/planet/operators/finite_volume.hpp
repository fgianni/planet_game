#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>

namespace planetsim {

// Finite-volume operators on the centroidal Voronoi mesh (ADR-0002 §4.2).
//
// Edge quantities are oriented from EdgeGeometry::first_cell towards
// EdgeGeometry::second_cell. An edge normal flux is a flux density per metre
// of edge length; divergence multiplies it by the edge length. Cell vectors
// are stored as separate East and North components in the cell's local
// tangent basis. All arithmetic is carried out in double precision and every
// output value depends only on its own cell, so results are identical for any
// worker count.

// +1 when `cell` is the edge's first cell (flux leaves it), -1 otherwise.
[[nodiscard]] double edge_orientation(const PlanetMesh& mesh, CellId cell, EdgeId edge);

// div(F)_i = (1/A_i) Σ_e s_ie F_e l_e. Conservative: Σ_i A_i div_i = 0 to
// rounding for any edge fluxes.
template <typename T>
void divergence(const PlanetMesh& mesh, const EdgeField<T>& edge_normal_flux,
                Field2D<T>& result, std::size_t worker_count = 1U);

// Least-squares gradient over the edge neighbours. On the Voronoi mesh the
// outward edge normal points at the neighbour's centre, so the displacement to
// neighbour j is d_e n_e and the fit needs no further geometry:
// grad(phi)_i = M^-1 Σ_e (phi_j - phi_i) n_e / d_e with M = Σ_e n_e n_e^T.
// Exact for linear fields on every cell, including those next to pentagons.
template <typename T>
void gradient(const PlanetMesh& mesh, const Field2D<T>& scalar, Field2D<T>& east,
              Field2D<T>& north, std::size_t worker_count = 1U);

// Two-point Laplacian: lap(phi)_i = (1/A_i) Σ_e (phi_j - phi_i) l_e / d_e.
// Conservative and negative semi-definite. Its pointwise truncation error does
// not vanish on the distorted hexagons around pentagons, but discrete
// solutions converge at second order (ADR-0002 §9).
template <typename T>
void laplacian(const PlanetMesh& mesh, const Field2D<T>& scalar, Field2D<T>& result,
               std::size_t worker_count = 1U);

}  // namespace planetsim
