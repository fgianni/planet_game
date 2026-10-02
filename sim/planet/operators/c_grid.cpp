#include "sim/planet/operators/c_grid.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace planetsim {
namespace {

// Spherical triangle area on the unit sphere. The triple product is taken
// over differences, a · ((b − a) × (c − a)), which equals a · (b × c) but
// keeps its relative precision for small triangles.
[[nodiscard]] double spherical_triangle_area_unit(const Vec3d& first, const Vec3d& second,
                                                  const Vec3d& third) noexcept {
    const double numerator = std::abs(dot(first, cross(second - first, third - first)));
    const double denominator = 1.0 + dot(first, second) + dot(second, third) + dot(third, first);
    return 2.0 * std::atan2(numerator, denominator);
}

[[nodiscard]] std::vector<CellBlock> make_blocks(std::size_t count) {
    std::vector<CellBlock> blocks;
    for (std::size_t begin = 0; begin < count; begin += deterministic_edge_block_size) {
        blocks.push_back({static_cast<std::uint32_t>(begin),
                          static_cast<std::uint32_t>(
                              std::min(begin + deterministic_edge_block_size, count))});
    }
    return blocks;
}

// +1 when the edge's normal leaves `cell` (it is the first cell).
[[nodiscard]] double outward_sign(const EdgeGeometry& edge, CellId cell) noexcept {
    return edge.first_cell == cell ? 1.0 : -1.0;
}

template <typename FieldType>
void require_size(const FieldType& field, std::size_t expected, const char* message) {
    if (field.size() != expected) {
        throw std::invalid_argument(message);
    }
}

}  // namespace

CGridGeometry CGridGeometry::build(const PlanetMesh& mesh) {
    const double radius = mesh.radius_m();
    const auto corners_unit = mesh.corners_unit();
    CGridGeometry grid;
    grid.edges_.resize(mesh.edge_count());
    grid.corners_.resize(mesh.corner_count());
    std::vector<std::uint8_t> corner_cells(mesh.corner_count(), 0U);
    std::vector<std::uint8_t> corner_edges(mesh.corner_count(), 0U);

    // Edges: corners counter-clockwise around the first cell, so that
    // t_e = k × n_e runs from vertex[0] to vertex[1].
    for (const auto& cell : mesh.cells()) {
        const auto cell_edges = mesh.cell_edges(cell.id);
        const auto corners = mesh.cell_corners(cell.id);
        for (std::size_t local = 0; local < cell_edges.size(); ++local) {
            const auto& geometry = mesh.edge(cell_edges[local].edge);
            if (geometry.first_cell != cell.id) {
                continue;
            }
            auto& edge = grid.edges_[cell_edges[local].edge.to_index()];
            edge.vertex = {corners[local], corners[(local + 1U) % corners.size()]};
            const Vec3d& a = corners_unit[edge.vertex[0]];
            const Vec3d& b = corners_unit[edge.vertex[1]];
            edge.midpoint_unit = normalized(a + b);
            const Vec3d& first = cell.center_unit;
            const Vec3d& second = mesh.cell(geometry.second_cell).center_unit;
            edge.normal_unit = normalized(cross(cross(first, second), edge.midpoint_unit));
            edge.tangent_unit = cross(edge.midpoint_unit, edge.normal_unit);
            if (!(dot(b - a, edge.tangent_unit) > 0.0)) {
                throw std::logic_error("C-grid edge tangent does not run from vertex 0 to 1");
            }
        }
    }

    // Corners: their cells with kite areas, and their edges with the
    // circulation sign.
    for (const auto& cell : mesh.cells()) {
        const auto cell_edges = mesh.cell_edges(cell.id);
        const auto corners = mesh.cell_corners(cell.id);
        const std::size_t count = corners.size();
        for (std::size_t local = 0; local < count; ++local) {
            // Corner `local` lies between edge local − 1 and edge local.
            const CornerIndex corner_index = corners[local];
            const Vec3d& v = corners_unit[corner_index];
            const auto& before = cell_edges[(local + count - 1U) % count];
            const auto& after = cell_edges[local];
            const Vec3d m_before =
                normalized(cell.center_unit + mesh.cell(before.neighbor).center_unit);
            const Vec3d m_after =
                normalized(cell.center_unit + mesh.cell(after.neighbor).center_unit);
            const double kite =
                (spherical_triangle_area_unit(cell.center_unit, m_before, v) +
                 spherical_triangle_area_unit(cell.center_unit, v, m_after)) *
                radius * radius;
            auto& corner = grid.corners_[corner_index];
            auto& slot = corner_cells[corner_index];
            if (slot >= 3U) {
                throw std::logic_error("C-grid corner has more than three cells");
            }
            corner.cell[slot] = cell.id;
            corner.kite_area_m2[slot] = kite;
            ++slot;
        }
    }
    for (std::size_t index = 0; index < grid.edges_.size(); ++index) {
        const EdgeId edge_id{static_cast<EdgeId::value_type>(index)};
        const auto& edge = grid.edges_[index];
        for (std::size_t end = 0; end < 2U; ++end) {
            const CornerIndex corner_index = edge.vertex[end];
            auto& slot = corner_edges[corner_index];
            if (slot >= 3U) {
                throw std::logic_error("C-grid corner has more than three edges");
            }
            auto& corner = grid.corners_[corner_index];
            corner.edge[slot] = edge_id;
            corner.edge_sign[slot] = end == 1U ? std::int8_t{1} : std::int8_t{-1};
            ++slot;
        }
    }
    for (std::size_t index = 0; index < grid.corners_.size(); ++index) {
        if (corner_cells[index] != 3U || corner_edges[index] != 3U) {
            throw std::logic_error("C-grid corner is not a triangle");
        }
        auto& corner = grid.corners_[index];
        corner.area_m2 = corner.kite_area_m2[0] + corner.kite_area_m2[1] + corner.kite_area_m2[2];
        // Barycentric weights of the corner in the gnomonic projection of
        // the three cell centres onto the corner's tangent plane.
        const Vec3d& v = corners_unit[index];
        std::array<Vec3d, 3> p{};
        for (std::size_t k = 0; k < 3U; ++k) {
            const Vec3d& c = mesh.cell(corner.cell[k]).center_unit;
            p[k] = c / dot(c, v) - v;
        }
        // Signed areas of the sub-triangles opposite each vertex, normal v.
        const double total = dot(v, cross(p[1] - p[0], p[2] - p[0]));
        for (std::size_t k = 0; k < 3U; ++k) {
            const Vec3d& b = p[(k + 1U) % 3U];
            const Vec3d& c = p[(k + 2U) % 3U];
            corner.interpolation_weight[k] = dot(v, cross(b, c)) / total;
        }
    }

    // TRiSK weights (Thuburn et al., 2009). Going counter-clockwise around
    // cell i from edge e, the weight of edge e' is (½ − Σ R_iv / A_i) over the
    // corners passed, times the outward signs of e and e' at i.
    std::vector<std::vector<std::pair<EdgeId, double>>> per_edge(mesh.edge_count());
    std::size_t term_count = 0;
    for (const auto& cell : mesh.cells()) {
        const auto cell_edges = mesh.cell_edges(cell.id);
        const auto corners = mesh.cell_corners(cell.id);
        const std::size_t count = corners.size();
        std::vector<double> kite_fraction(count);
        double kite_total = 0.0;
        for (std::size_t local = 0; local < count; ++local) {
            const auto& corner = grid.corners_[corners[local]];
            for (std::size_t k = 0; k < 3U; ++k) {
                if (corner.cell[k] == cell.id) {
                    kite_fraction[local] = corner.kite_area_m2[k];
                }
            }
            kite_total += kite_fraction[local];
        }
        for (auto& fraction : kite_fraction) {
            fraction /= kite_total;
        }
        for (std::size_t from = 0; from < count; ++from) {
            const EdgeId e = cell_edges[from].edge;
            const double sign_e = outward_sign(mesh.edge(e), cell.id);
            double passed = 0.0;
            for (std::size_t step = 1; step < count; ++step) {
                const std::size_t to = (from + step) % count;
                // Corner `to` lies between edge to − 1 and edge to.
                passed += kite_fraction[to];
                const EdgeId e_prime = cell_edges[to].edge;
                const double sign_e_prime = outward_sign(mesh.edge(e_prime), cell.id);
                per_edge[e.to_index()].emplace_back(e_prime,
                                                    (0.5 - passed) * sign_e * sign_e_prime);
                ++term_count;
            }
        }
    }
    grid.weight_edges_.reserve(term_count);
    grid.weights_.reserve(term_count);
    for (std::size_t index = 0; index < per_edge.size(); ++index) {
        auto& edge = grid.edges_[index];
        edge.weight_offset = static_cast<std::uint32_t>(grid.weights_.size());
        edge.weight_count = static_cast<std::uint32_t>(per_edge[index].size());
        for (const auto& [e_prime, weight] : per_edge[index]) {
            grid.weight_edges_.push_back(e_prime);
            grid.weights_.push_back(weight);
        }
    }

    grid.edge_blocks_ = make_blocks(grid.edges_.size());
    grid.corner_blocks_ = make_blocks(grid.corners_.size());
    return grid;
}

std::span<const EdgeId> CGridGeometry::tangential_weight_edges(EdgeId id) const {
    const auto& edge = edges_.at(id.to_index());
    return {weight_edges_.data() + edge.weight_offset, edge.weight_count};
}

std::span<const double> CGridGeometry::tangential_weights(EdgeId id) const {
    const auto& edge = edges_.at(id.to_index());
    return {weights_.data() + edge.weight_offset, edge.weight_count};
}

std::size_t CGridGeometry::allocated_bytes() const noexcept {
    return edges_.capacity() * sizeof(CGridEdge) + corners_.capacity() * sizeof(CGridCorner) +
           weight_edges_.capacity() * sizeof(EdgeId) + weights_.capacity() * sizeof(double) +
           (edge_blocks_.capacity() + corner_blocks_.capacity()) * sizeof(CellBlock);
}

void tangential_velocity(const PlanetMesh& mesh, const CGridGeometry& grid,
                         const EdgeField<double>& normal_velocity,
                         EdgeField<double>& tangential, std::size_t worker_count) {
    require_size(normal_velocity, mesh.edge_count(), "edge field size does not match the mesh");
    require_size(tangential, mesh.edge_count(), "edge field size does not match the mesh");
    for_each_deterministic_block(
        grid.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                const EdgeId edge_id{static_cast<EdgeId::value_type>(index)};
                const auto edges = grid.tangential_weight_edges(edge_id);
                const auto weights = grid.tangential_weights(edge_id);
                double sum = 0.0;
                for (std::size_t term = 0; term < edges.size(); ++term) {
                    sum += weights[term] * mesh.edge(edges[term]).length_m *
                           normal_velocity[edges[term]];
                }
                tangential[edge_id] = sum / mesh.edge(edge_id).centroid_distance_m;
            }
        });
}

void relative_vorticity(const PlanetMesh& mesh, const CGridGeometry& grid,
                        const EdgeField<double>& normal_velocity, Field2D<double>& vorticity,
                        std::size_t worker_count) {
    require_size(normal_velocity, mesh.edge_count(), "edge field size does not match the mesh");
    require_size(vorticity, grid.corner_count(), "corner field size does not match the grid");
    for_each_deterministic_block(
        grid.corner_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                const auto& corner = grid.corners()[index];
                double circulation = 0.0;
                for (std::size_t k = 0; k < 3U; ++k) {
                    circulation += static_cast<double>(corner.edge_sign[k]) *
                                   mesh.edge(corner.edge[k]).centroid_distance_m *
                                   normal_velocity[corner.edge[k]];
                }
                vorticity[index] = circulation / corner.area_m2;
            }
        });
}

void kinetic_energy(const PlanetMesh& mesh, const EdgeField<double>& normal_velocity,
                    Field2D<double>& kinetic, std::size_t worker_count) {
    require_size(normal_velocity, mesh.edge_count(), "edge field size does not match the mesh");
    require_size(kinetic, mesh.cell_count(), "cell field size does not match the mesh");
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                const auto& cell = mesh.cells()[index];
                double sum = 0.0;
                for (const auto& cell_edge : mesh.cell_edges(cell.id)) {
                    const auto& edge = mesh.edge(cell_edge.edge);
                    const double u = normal_velocity[cell_edge.edge];
                    sum += 0.25 * edge.length_m * edge.centroid_distance_m * u * u;
                }
                kinetic[cell.id] = sum / cell.area_m2;
            }
        });
}

void reconstruct_cell_vector(const PlanetMesh& mesh, const CGridGeometry& grid,
                             const EdgeField<double>& normal_velocity, Field2D<double>& east,
                             Field2D<double>& north, std::size_t worker_count) {
    require_size(normal_velocity, mesh.edge_count(), "edge field size does not match the mesh");
    require_size(east, mesh.cell_count(), "cell field size does not match the mesh");
    require_size(north, mesh.cell_count(), "cell field size does not match the mesh");
    const double radius = mesh.radius_m();
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                const auto& cell = mesh.cells()[index];
                Vec3d sum;
                for (const auto& cell_edge : mesh.cell_edges(cell.id)) {
                    const auto& edge = mesh.edge(cell_edge.edge);
                    const Vec3d offset =
                        (grid.edge(cell_edge.edge).midpoint_unit - cell.center_unit) * radius;
                    sum = sum + offset * (edge.length_m * outward_sign(edge, cell.id) *
                                          normal_velocity[cell_edge.edge]);
                }
                east[cell.id] = dot(sum, cell.east_unit) / cell.area_m2;
                north[cell.id] = dot(sum, cell.north_unit) / cell.area_m2;
            }
        });
}

void normal_gradient(const PlanetMesh& mesh, const CGridGeometry& grid,
                     const Field2D<double>& scalar, EdgeField<double>& gradient,
                     std::size_t worker_count) {
    require_size(scalar, mesh.cell_count(), "cell field size does not match the mesh");
    require_size(gradient, mesh.edge_count(), "edge field size does not match the mesh");
    for_each_deterministic_block(
        grid.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                const EdgeId edge_id{static_cast<EdgeId::value_type>(index)};
                const auto& edge = mesh.edge(edge_id);
                gradient[edge_id] =
                    (scalar[edge.second_cell] - scalar[edge.first_cell]) / edge.centroid_distance_m;
            }
        });
}

void tangential_gradient(const PlanetMesh& mesh, const CGridGeometry& grid,
                         const Field2D<double>& corner_scalar, EdgeField<double>& gradient,
                         std::size_t worker_count) {
    require_size(corner_scalar, grid.corner_count(), "corner field size does not match the grid");
    require_size(gradient, mesh.edge_count(), "edge field size does not match the mesh");
    for_each_deterministic_block(
        grid.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                const EdgeId edge_id{static_cast<EdgeId::value_type>(index)};
                const auto& edge = grid.edges()[index];
                gradient[edge_id] = (corner_scalar[edge.vertex[1]] - corner_scalar[edge.vertex[0]]) /
                                    mesh.edge(edge_id).length_m;
            }
        });
}

void cell_to_corner(const CGridGeometry& grid, const Field2D<double>& scalar,
                    Field2D<double>& corner_scalar, std::size_t worker_count) {
    require_size(corner_scalar, grid.corner_count(), "corner field size does not match the grid");
    for_each_deterministic_block(
        grid.corner_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                const auto& corner = grid.corners()[index];
                double sum = 0.0;
                for (std::size_t k = 0; k < 3U; ++k) {
                    sum += corner.kite_area_m2[k] * scalar[corner.cell[k]];
                }
                corner_scalar[index] = sum / corner.area_m2;
            }
        });
}

void interpolate_to_corner(const CGridGeometry& grid, const Field2D<double>& scalar,
                           Field2D<double>& corner_scalar, std::size_t worker_count) {
    require_size(corner_scalar, grid.corner_count(), "corner field size does not match the grid");
    for_each_deterministic_block(
        grid.corner_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                const auto& corner = grid.corners()[index];
                double sum = 0.0;
                for (std::size_t k = 0; k < 3U; ++k) {
                    sum += corner.interpolation_weight[k] * scalar[corner.cell[k]];
                }
                corner_scalar[index] = sum;
            }
        });
}

}  // namespace planetsim
