#include "sim/planet/dynamics/shallow_water.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/operators/finite_volume.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace planetsim {
namespace {

[[nodiscard]] EdgeId edge_id(std::size_t index) noexcept {
    return EdgeId{static_cast<EdgeId::value_type>(index)};
}

// y = a + scale · b, for both components.
void axpy(const PlanetMesh& mesh, const CGridGeometry& grid, const ShallowWaterState& a,
          double scale, const ShallowWaterState& b, ShallowWaterState& y,
          std::size_t worker_count) {
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                y.thickness_m[index] = a.thickness_m[index] + scale * b.thickness_m[index];
            }
        });
    const auto a_u = a.normal_velocity_m_s.values();
    const auto b_u = b.normal_velocity_m_s.values();
    const auto y_u = y.normal_velocity_m_s.values();
    for_each_deterministic_block(
        grid.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                y_u[index] = a_u[index] + scale * b_u[index];
            }
        });
}

[[nodiscard]] ShallowWaterState sized_like(const ShallowWaterState& state) {
    return {Field2D<double>(state.thickness_m.size()),
            EdgeField<double>(state.normal_velocity_m_s.size())};
}

}  // namespace

std::size_t substep_count(const PlanetMesh& mesh, double span_s, const SubstepRule& rule) {
    if (!(span_s > 0.0) || !std::isfinite(span_s)) {
        throw std::invalid_argument("a dynamics span must be positive and finite");
    }
    if (!(rule.wave_speed_m_s > 0.0) || !(rule.max_wind_m_s >= 0.0) || !(rule.courant > 0.0)) {
        throw std::invalid_argument("the sub-step rule needs positive bounds");
    }
    double d_min = mesh.edges().front().centroid_distance_m;
    for (const auto& edge : mesh.edges()) {
        d_min = std::min(d_min, edge.centroid_distance_m);
    }
    const double dt_max = rule.courant * d_min / (rule.wave_speed_m_s + rule.max_wind_m_s);
    return std::max<std::size_t>(1U, static_cast<std::size_t>(std::ceil(span_s / dt_max)));
}

double hyperviscosity_for_damping_time(const PlanetMesh& mesh, double damping_time_s) {
    if (!(damping_time_s > 0.0)) {
        throw std::invalid_argument("the hyperviscous damping time must be positive");
    }
    double sum = 0.0;
    for (const auto& edge : mesh.edges()) {
        sum += edge.centroid_distance_m;
    }
    const double d_mean = sum / static_cast<double>(mesh.edge_count());
    const double d2 = d_mean * d_mean;
    // The largest |λ| d̄² of L is 27–29 on these meshes; 30 bounds it.
    constexpr double grid_scale_eigenvalue = 30.0;
    return d2 * d2 / (grid_scale_eigenvalue * grid_scale_eigenvalue * damping_time_s);
}

ShallowWaterModel::ShallowWaterModel(const PlanetMesh& mesh, const CGridGeometry& grid,
                                     ShallowWaterParameters parameters,
                                     Field2D<double> bottom_height_m)
    : mesh_(&mesh), grid_(&grid), parameters_(parameters),
      bottom_height_m_(std::move(bottom_height_m)), f_corner_(grid.corner_count()) {
    if (bottom_height_m_.size() != mesh.cell_count()) {
        throw std::invalid_argument("bottom height size does not match the mesh");
    }
    if (grid.edge_count() != mesh.edge_count() || grid.corner_count() != mesh.corner_count()) {
        throw std::invalid_argument("C-grid does not belong to the mesh");
    }
    if (!(parameters_.gravity_m_s2 > 0.0) || !(parameters_.hyperviscosity_m4_s >= 0.0)) {
        throw std::invalid_argument("shallow water needs positive gravity, ν₄ ≥ 0");
    }
    const Vec3d axis = normalized(parameters_.rotation_axis);
    const auto corners = mesh.corners_unit();
    for (std::size_t index = 0; index < grid.corner_count(); ++index) {
        f_corner_[index] = 2.0 * parameters_.rotation_rate_rad_s * dot(axis, corners[index]);
    }
}

void ShallowWaterModel::vector_laplacian(const EdgeField<double>& velocity,
                                         EdgeField<double>& result,
                                         std::size_t worker_count) const {
    const auto& mesh = *mesh_;
    const auto& grid = *grid_;
    Field2D<double> divergence_field(mesh.cell_count());
    Field2D<double> vorticity(grid.corner_count());
    divergence(mesh, velocity, divergence_field, worker_count);
    relative_vorticity(mesh, grid, velocity, vorticity, worker_count);
    for_each_deterministic_block(
        grid.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                const auto& edge = mesh.edge(edge_id(index));
                const auto& c_edge = grid.edges()[index];
                result[edge_id(index)] =
                    (divergence_field[edge.second_cell] - divergence_field[edge.first_cell]) /
                        edge.centroid_distance_m -
                    (vorticity[c_edge.vertex[1]] - vorticity[c_edge.vertex[0]]) / edge.length_m;
            }
        });
}

void ShallowWaterModel::tendency(const ShallowWaterState& state, ShallowWaterState& rate,
                                 std::size_t worker_count) const {
    const auto& mesh = *mesh_;
    const auto& grid = *grid_;
    const auto& h = state.thickness_m;
    const auto& u = state.normal_velocity_m_s;
    const double g = parameters_.gravity_m_s2;

    // Mass flux and the thickness tendency.
    EdgeField<double> flux(mesh.edge_count());
    for_each_deterministic_block(
        grid.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                const auto& edge = mesh.edge(edge_id(index));
                flux[edge_id(index)] =
                    0.5 * (h[edge.first_cell] + h[edge.second_cell]) * u[edge_id(index)];
            }
        });
    divergence(mesh, flux, rate.thickness_m, worker_count);

    // Potential vorticity at the corners.
    Field2D<double> corner_thickness(grid.corner_count());
    Field2D<double> pv(grid.corner_count());
    cell_to_corner(grid, h, corner_thickness, worker_count);
    relative_vorticity(mesh, grid, u, pv, worker_count);
    for_each_deterministic_block(
        grid.corner_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                pv[index] = (pv[index] + f_corner_[index]) / corner_thickness[index];
            }
        });

    // Bernoulli function K + g (h + b) at the cells.
    Field2D<double> bernoulli(mesh.cell_count());
    kinetic_energy(mesh, u, bernoulli, worker_count);
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                bernoulli[index] += g * (h[index] + bottom_height_m_[index]);
            }
        });
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                rate.thickness_m[index] = -rate.thickness_m[index];
            }
        });

    // Momentum: PV flux minus the Bernoulli gradient.
    auto& du = rate.normal_velocity_m_s;
    for_each_deterministic_block(
        grid.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                const EdgeId e = edge_id(index);
                const auto& edge = mesh.edge(e);
                const auto& c_edge = grid.edges()[index];
                const double q_e = 0.5 * (pv[c_edge.vertex[0]] + pv[c_edge.vertex[1]]);
                const auto terms = grid.tangential_weight_edges(e);
                const auto weights = grid.tangential_weights(e);
                double pv_flux = 0.0;
                for (std::size_t term = 0; term < terms.size(); ++term) {
                    const auto& other = grid.edge(terms[term]);
                    const double q_other = 0.5 * (pv[other.vertex[0]] + pv[other.vertex[1]]);
                    pv_flux += weights[term] * mesh.edge(terms[term]).length_m *
                               flux[terms[term]] * 0.5 * (q_e + q_other);
                }
                du[e] = (pv_flux - (bernoulli[edge.second_cell] - bernoulli[edge.first_cell])) /
                        edge.centroid_distance_m;
            }
        });

    if (parameters_.hyperviscosity_m4_s > 0.0) {
        EdgeField<double> once(mesh.edge_count());
        EdgeField<double> twice(mesh.edge_count());
        vector_laplacian(u, once, worker_count);
        vector_laplacian(once, twice, worker_count);
        const double nu = parameters_.hyperviscosity_m4_s;
        for_each_deterministic_block(
            grid.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                for (std::size_t index = block.begin; index < block.end; ++index) {
                    du[edge_id(index)] -= nu * twice[edge_id(index)];
                }
            });
    }
}

void ShallowWaterModel::step(ShallowWaterState& state, double dt_s,
                             std::size_t worker_count) const {
    if (!(dt_s > 0.0) || !std::isfinite(dt_s)) {
        throw std::invalid_argument("a shallow-water step must be positive and finite");
    }
    ShallowWaterState rate = sized_like(state);
    ShallowWaterState stage = sized_like(state);
    tendency(state, rate, worker_count);
    axpy(*mesh_, *grid_, state, dt_s / 3.0, rate, stage, worker_count);
    tendency(stage, rate, worker_count);
    axpy(*mesh_, *grid_, state, dt_s / 2.0, rate, stage, worker_count);
    tendency(stage, rate, worker_count);
    axpy(*mesh_, *grid_, state, dt_s, rate, state, worker_count);
}

std::size_t ShallowWaterModel::advance(ShallowWaterState& state, double span_s,
                                       const SubstepRule& rule,
                                       std::size_t worker_count) const {
    const std::size_t steps = substep_count(*mesh_, span_s, rule);
    const double dt = span_s / static_cast<double>(steps);
    for (std::size_t index = 0; index < steps; ++index) {
        step(state, dt, worker_count);
        const auto values = state.normal_velocity_m_s.values();
        double max_wind = 0.0;
        for (const double value : values) {
            max_wind = std::max(max_wind, std::abs(value));
        }
        if (!(max_wind <= rule.max_wind_m_s)) {
            throw std::runtime_error("wind of " + std::to_string(max_wind) +
                                     " m/s exceeds the sub-step rule's bound of " +
                                     std::to_string(rule.max_wind_m_s) + " m/s");
        }
    }
    return steps;
}

ShallowWaterDiagnostics ShallowWaterModel::diagnose(const ShallowWaterState& state,
                                                    std::size_t worker_count) const {
    const auto& mesh = *mesh_;
    const auto& grid = *grid_;
    const auto& h = state.thickness_m;
    const auto& u = state.normal_velocity_m_s;
    const double g = parameters_.gravity_m_s2;

    Field2D<double> kinetic(mesh.cell_count());
    kinetic_energy(mesh, u, kinetic, worker_count);
    struct CellSums {
        double mass = 0.0;
        double kinetic = 0.0;
        double potential = 0.0;
    };
    const auto cells = reduce_deterministic_blocks(
        mesh.blocks(), worker_count, CellSums{},
        [&](std::size_t, const CellBlock& block) {
            CellSums sums;
            for (std::size_t index = block.begin; index < block.end; ++index) {
                const double area = mesh.cells()[index].area_m2;
                sums.mass += area * h[index];
                sums.kinetic += area * h[index] * kinetic[index];
                sums.potential += area * g * h[index] * (0.5 * h[index] + bottom_height_m_[index]);
            }
            return sums;
        },
        [](CellSums total, const CellSums& part) {
            total.mass += part.mass;
            total.kinetic += part.kinetic;
            total.potential += part.potential;
            return total;
        });

    Field2D<double> corner_thickness(grid.corner_count());
    Field2D<double> vorticity(grid.corner_count());
    cell_to_corner(grid, h, corner_thickness, worker_count);
    relative_vorticity(mesh, grid, u, vorticity, worker_count);
    const double enstrophy = reduce_deterministic_blocks(
        grid.corner_blocks(), worker_count, 0.0,
        [&](std::size_t, const CellBlock& block) {
            double sum = 0.0;
            for (std::size_t index = block.begin; index < block.end; ++index) {
                const double q = (vorticity[index] + f_corner_[index]) / corner_thickness[index];
                sum += 0.5 * grid.corners()[index].area_m2 * corner_thickness[index] * q * q;
            }
            return sum;
        },
        [](double total, double part) { return total + part; });

    const auto velocities = u.values();
    const double max_wind = reduce_deterministic_blocks(
        grid.edge_blocks(), worker_count, 0.0,
        [&](std::size_t, const CellBlock& block) {
            double maximum = 0.0;
            for (std::size_t index = block.begin; index < block.end; ++index) {
                maximum = std::max(maximum, std::abs(velocities[index]));
            }
            return maximum;
        },
        [](double total, double part) { return std::max(total, part); });

    return {cells.mass, cells.kinetic, cells.potential, enstrophy, max_wind};
}

}  // namespace planetsim
