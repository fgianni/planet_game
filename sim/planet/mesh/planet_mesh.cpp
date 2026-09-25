#include "sim/planet/mesh/planet_mesh.hpp"

#include <utility>

namespace planetsim {

PlanetMesh::PlanetMesh(std::uint32_t subdivision, double radius_m, std::vector<CellGeometry> cells,
                       std::vector<CellEdgeGeometry> cell_edges, std::vector<EdgeGeometry> edges,
                       std::vector<Vec3d> corners_unit,
                       std::vector<CornerIndex> cell_corner_indices, std::vector<CellBlock> blocks)
    : subdivision_(subdivision), radius_m_(radius_m), cells_(std::move(cells)),
      cell_edges_(std::move(cell_edges)), edges_(std::move(edges)),
      corners_unit_(std::move(corners_unit)), cell_corner_indices_(std::move(cell_corner_indices)),
      blocks_(std::move(blocks)) {}

const CellGeometry& PlanetMesh::cell(CellId id) const { return cells_.at(id.to_index()); }

const EdgeGeometry& PlanetMesh::edge(EdgeId id) const { return edges_.at(id.to_index()); }

std::span<const CellEdgeGeometry> PlanetMesh::cell_edges(CellId id) const {
    const auto& geometry = cell(id);
    return {cell_edges_.data() + geometry.edge_offset, geometry.edge_count};
}

std::span<const CornerIndex> PlanetMesh::cell_corners(CellId id) const {
    const auto& geometry = cell(id);
    return {cell_corner_indices_.data() + geometry.edge_offset, geometry.edge_count};
}

std::size_t PlanetMesh::allocated_bytes() const noexcept {
    return cells_.capacity() * sizeof(CellGeometry) +
           cell_edges_.capacity() * sizeof(CellEdgeGeometry) +
           edges_.capacity() * sizeof(EdgeGeometry) + corners_unit_.capacity() * sizeof(Vec3d) +
           cell_corner_indices_.capacity() * sizeof(CornerIndex) +
           blocks_.capacity() * sizeof(CellBlock);
}

}  // namespace planetsim
