#include "sim/planet/operators/finite_volume.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"

#include <stdexcept>

namespace planetsim {
namespace {

template <typename FieldType>
void require_size(const FieldType& field, std::size_t expected, const char* message) {
    if (field.size() != expected) {
        throw std::invalid_argument(message);
    }
}

template <typename T> void require_cell_field(const PlanetMesh& mesh, const Field2D<T>& field) {
    require_size(field, mesh.cell_count(), "cell field size does not match the mesh");
}

}  // namespace

double edge_orientation(const PlanetMesh& mesh, CellId cell, EdgeId edge) {
    const auto& geometry = mesh.edge(edge);
    if (geometry.first_cell == cell) {
        return 1.0;
    }
    if (geometry.second_cell == cell) {
        return -1.0;
    }
    throw std::invalid_argument("cell is not incident to edge");
}

template <typename T>
void divergence(const PlanetMesh& mesh, const EdgeField<T>& edge_normal_flux,
                Field2D<T>& result, std::size_t worker_count) {
    require_size(edge_normal_flux, mesh.edge_count(), "edge field size does not match the mesh");
    require_cell_field(mesh, result);

    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t cell_index = block.begin; cell_index < block.end; ++cell_index) {
                const auto& cell = mesh.cells()[cell_index];
                double net_outflow = 0.0;
                for (const auto& cell_edge : mesh.cell_edges(cell.id)) {
                    net_outflow += edge_orientation(mesh, cell.id, cell_edge.edge) *
                                   static_cast<double>(edge_normal_flux[cell_edge.edge]) *
                                   mesh.edge(cell_edge.edge).length_m;
                }
                result[cell.id] = static_cast<T>(net_outflow / cell.area_m2);
            }
        });
}

template <typename T>
void gradient(const PlanetMesh& mesh, const Field2D<T>& scalar, Field2D<T>& east,
              Field2D<T>& north, std::size_t worker_count) {
    require_cell_field(mesh, scalar);
    require_cell_field(mesh, east);
    require_cell_field(mesh, north);

    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t cell_index = block.begin; cell_index < block.end; ++cell_index) {
                const auto& cell = mesh.cells()[cell_index];
                const double own = static_cast<double>(scalar[cell.id]);
                double m_ee = 0.0;
                double m_en = 0.0;
                double m_nn = 0.0;
                double rhs_east = 0.0;
                double rhs_north = 0.0;
                for (const auto& cell_edge : mesh.cell_edges(cell.id)) {
                    const double n_east = static_cast<double>(cell_edge.outward_normal_east);
                    const double n_north = static_cast<double>(cell_edge.outward_normal_north);
                    const double slope = (static_cast<double>(scalar[cell_edge.neighbor]) - own) /
                                         mesh.edge(cell_edge.edge).centroid_distance_m;
                    m_ee += n_east * n_east;
                    m_en += n_east * n_north;
                    m_nn += n_north * n_north;
                    rhs_east += slope * n_east;
                    rhs_north += slope * n_north;
                }
                const double determinant = m_ee * m_nn - m_en * m_en;
                east[cell.id] = static_cast<T>((m_nn * rhs_east - m_en * rhs_north) / determinant);
                north[cell.id] =
                    static_cast<T>((m_ee * rhs_north - m_en * rhs_east) / determinant);
            }
        });
}

template <typename T>
void laplacian(const PlanetMesh& mesh, const Field2D<T>& scalar, Field2D<T>& result,
               std::size_t worker_count) {
    require_cell_field(mesh, scalar);
    require_cell_field(mesh, result);

    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t cell_index = block.begin; cell_index < block.end; ++cell_index) {
                const auto& cell = mesh.cells()[cell_index];
                const double own = static_cast<double>(scalar[cell.id]);
                double sum = 0.0;
                for (const auto& cell_edge : mesh.cell_edges(cell.id)) {
                    const auto& edge = mesh.edge(cell_edge.edge);
                    sum += (static_cast<double>(scalar[cell_edge.neighbor]) - own) *
                           edge.length_m / edge.centroid_distance_m;
                }
                result[cell.id] = static_cast<T>(sum / cell.area_m2);
            }
        });
}

template void divergence(const PlanetMesh&, const EdgeField<float>&, Field2D<float>&,
                         std::size_t);
template void divergence(const PlanetMesh&, const EdgeField<double>&, Field2D<double>&,
                         std::size_t);
template void gradient(const PlanetMesh&, const Field2D<float>&, Field2D<float>&,
                       Field2D<float>&, std::size_t);
template void gradient(const PlanetMesh&, const Field2D<double>&, Field2D<double>&,
                       Field2D<double>&, std::size_t);
template void laplacian(const PlanetMesh&, const Field2D<float>&, Field2D<float>&, std::size_t);
template void laplacian(const PlanetMesh&, const Field2D<double>&, Field2D<double>&,
                        std::size_t);

}  // namespace planetsim
