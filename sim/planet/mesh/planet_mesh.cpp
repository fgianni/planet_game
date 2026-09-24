#include "sim/planet/mesh/planet_mesh.hpp"

#include <utility>

namespace planetsim {

PlanetMesh::PlanetMesh(std::uint32_t subdivision,
                       double radius_m,
                       std::vector<Vec3d> vertices_unit,
                       std::vector<CellGeometry> cells)
    : subdivision_(subdivision),
      radius_m_(radius_m),
      vertices_unit_(std::move(vertices_unit)),
      cells_(std::move(cells)) {}

const CellGeometry& PlanetMesh::cell(CellId id) const {
    return cells_.at(id.to_index());
}

}  // namespace planetsim
