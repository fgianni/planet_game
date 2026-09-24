#pragma once

#include "sim/core/math/vec3d.hpp"
#include "sim/planet/mesh/cell_id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace planetsim {

class PlanetMesh;

[[nodiscard]] PlanetMesh make_icosphere(std::uint32_t subdivision, double radius_m);

using VertexIndex = std::uint32_t;

struct CellGeometry {
    CellId id;
    Vec3d center_unit;
    double area_m2 = 0.0;
    std::array<CellId, 3> neighbors{CellId::invalid(), CellId::invalid(), CellId::invalid()};
    std::array<double, 3> edge_length_m{};
    std::array<VertexIndex, 3> vertex_indices{};
};

class PlanetMesh {
public:
    PlanetMesh(const PlanetMesh&) = default;
    PlanetMesh(PlanetMesh&&) noexcept = default;
    PlanetMesh& operator=(const PlanetMesh&) = default;
    PlanetMesh& operator=(PlanetMesh&&) noexcept = default;
    ~PlanetMesh() = default;

    [[nodiscard]] std::uint32_t subdivision() const noexcept { return subdivision_; }
    [[nodiscard]] double radius_m() const noexcept { return radius_m_; }
    [[nodiscard]] std::size_t cell_count() const noexcept { return cells_.size(); }
    [[nodiscard]] std::size_t vertex_count() const noexcept { return vertices_unit_.size(); }

    [[nodiscard]] const CellGeometry& cell(CellId id) const;
    [[nodiscard]] std::span<const CellGeometry> cells() const noexcept { return cells_; }
    [[nodiscard]] std::span<const Vec3d> vertices_unit() const noexcept {
        return vertices_unit_;
    }

private:
    PlanetMesh(std::uint32_t subdivision,
               double radius_m,
               std::vector<Vec3d> vertices_unit,
               std::vector<CellGeometry> cells);

    std::uint32_t subdivision_ = 0;
    double radius_m_ = 0.0;
    std::vector<Vec3d> vertices_unit_;
    std::vector<CellGeometry> cells_;

    friend PlanetMesh make_icosphere(std::uint32_t subdivision, double radius_m);
};

}  // namespace planetsim
