#pragma once

#include "sim/core/math/vec3d.hpp"
#include "sim/planet/mesh/cell_id.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace planetsim {

class PlanetMesh;

[[nodiscard]] PlanetMesh make_icosphere(std::uint32_t subdivision, double radius_m);

using CornerIndex = std::uint32_t;
using SourceVertexIndex = std::uint32_t;

class EdgeId {
  public:
    using value_type = std::uint32_t;

    constexpr EdgeId() noexcept = default;
    explicit constexpr EdgeId(value_type value) noexcept : value_(value) {}

    [[nodiscard]] static constexpr EdgeId invalid() noexcept {
        return EdgeId{static_cast<value_type>(-1)};
    }

    [[nodiscard]] constexpr value_type value() const noexcept { return value_; }
    [[nodiscard]] constexpr std::size_t to_index() const noexcept {
        return static_cast<std::size_t>(value_);
    }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return *this != invalid(); }

    friend constexpr bool operator==(EdgeId, EdgeId) noexcept = default;
    friend constexpr auto operator<=>(EdgeId, EdgeId) noexcept = default;

  private:
    value_type value_ = static_cast<value_type>(-1);
};

struct CellGeometry {
    Vec3d center_unit;
    Vec3d east_unit;
    Vec3d north_unit;
    double area_m2 = 0.0;
    CellId id;
    SourceVertexIndex source_vertex_index = 0;
    std::uint32_t edge_offset = 0;
    std::uint8_t edge_count = 0;

    [[nodiscard]] bool is_pentagon() const noexcept { return edge_count == 5U; }
};

struct CellEdgeGeometry {
    CellId neighbor;
    EdgeId edge;
    float outward_normal_east = 0.0F;
    float outward_normal_north = 0.0F;
};

struct EdgeGeometry {
    CellId first_cell;
    CellId second_cell;
    double length_m = 0.0;
    double centroid_distance_m = 0.0;
};

struct CellBlock {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

inline constexpr std::size_t deterministic_cell_block_size = 256U;

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
    [[nodiscard]] std::size_t corner_count() const noexcept { return corners_unit_.size(); }
    [[nodiscard]] std::size_t edge_count() const noexcept { return edges_.size(); }
    [[nodiscard]] std::size_t directed_edge_count() const noexcept { return cell_edges_.size(); }

    [[nodiscard]] const CellGeometry& cell(CellId id) const;
    [[nodiscard]] const EdgeGeometry& edge(EdgeId id) const;
    [[nodiscard]] std::span<const CellGeometry> cells() const noexcept { return cells_; }
    [[nodiscard]] std::span<const EdgeGeometry> edges() const noexcept { return edges_; }
    [[nodiscard]] std::span<const Vec3d> corners_unit() const noexcept { return corners_unit_; }
    [[nodiscard]] std::span<const CellBlock> blocks() const noexcept { return blocks_; }
    [[nodiscard]] std::span<const CellEdgeGeometry> cell_edges(CellId id) const;
    [[nodiscard]] std::span<const CornerIndex> cell_corners(CellId id) const;
    [[nodiscard]] std::size_t allocated_bytes() const noexcept;

  private:
    PlanetMesh(std::uint32_t subdivision, double radius_m, std::vector<CellGeometry> cells,
               std::vector<CellEdgeGeometry> cell_edges, std::vector<EdgeGeometry> edges,
               std::vector<Vec3d> corners_unit, std::vector<CornerIndex> cell_corner_indices,
               std::vector<CellBlock> blocks);

    std::uint32_t subdivision_ = 0;
    double radius_m_ = 0.0;
    std::vector<CellGeometry> cells_;
    std::vector<CellEdgeGeometry> cell_edges_;
    std::vector<EdgeGeometry> edges_;
    std::vector<Vec3d> corners_unit_;
    std::vector<CornerIndex> cell_corner_indices_;
    std::vector<CellBlock> blocks_;

    friend PlanetMesh make_icosphere(std::uint32_t subdivision, double radius_m);
};

}  // namespace planetsim
