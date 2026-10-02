#include "sim/planet/dynamics/primitive_equations.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"

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

// (1/A_i) Σ_e n_ei l_e v_e for an edge array v.
[[nodiscard]] double cell_divergence(const PlanetMesh& mesh, std::size_t cell,
                                     std::span<const double> edge_values) {
    const auto& geometry = mesh.cells()[cell];
    double sum = 0.0;
    for (const auto& cell_edge : mesh.cell_edges(geometry.id)) {
        const auto& edge = mesh.edge(cell_edge.edge);
        const double sign = edge.first_cell == geometry.id ? 1.0 : -1.0;
        sum += sign * edge.length_m * edge_values[cell_edge.edge.to_index()];
    }
    return sum / geometry.area_m2;
}

// TRiSK kinetic energy at a cell from an edge array.
[[nodiscard]] double cell_kinetic(const PlanetMesh& mesh, std::size_t cell,
                                  std::span<const double> u) {
    const auto& geometry = mesh.cells()[cell];
    double sum = 0.0;
    for (const auto& cell_edge : mesh.cell_edges(geometry.id)) {
        const auto& edge = mesh.edge(cell_edge.edge);
        const double value = u[cell_edge.edge.to_index()];
        sum += 0.25 * edge.length_m * edge.centroid_distance_m * value * value;
    }
    return sum / geometry.area_m2;
}

// Circulation over area at a corner from an edge array.
[[nodiscard]] double corner_vorticity(const PlanetMesh& mesh, const CGridCorner& corner,
                                      std::span<const double> u) {
    double circulation = 0.0;
    for (std::size_t k = 0; k < 3U; ++k) {
        circulation += static_cast<double>(corner.edge_sign[k]) *
                       mesh.edge(corner.edge[k]).centroid_distance_m *
                       u[corner.edge[k].to_index()];
    }
    return circulation / corner.area_m2;
}

// ln(y/x) / (y − x): the reciprocal of the logarithmic mean of x, y > 0,
// with its series where they are close.
[[nodiscard]] double inverse_log_mean(double x, double y) noexcept {
    const double r = y / x - 1.0;
    if (std::abs(r) < 1.0e-4) {
        return (1.0 - r / 2.0 + r * r / 3.0) / x;
    }
    return std::log1p(r) / (y - x);
}

[[nodiscard]] PrimitiveEquationState sized_like(const PrimitiveEquationState& state) {
    const auto& theta = state.mass_theta;
    const auto& u = state.normal_velocity_m_s;
    return {Field2D<double>(state.surface_pressure_Pa.size()),
            Field3D<double>(theta.layer_count(), theta.cell_count()),
            Field3D<double>(u.layer_count(), u.cell_count())};
}

}  // namespace

PrimitiveEquationModel::PrimitiveEquationModel(const PlanetMesh& mesh, const CGridGeometry& grid,
                                               PrimitiveEquationParameters parameters,
                                               Field2D<double> surface_height_m)
    : mesh_(&mesh), grid_(&grid), parameters_(parameters),
      surface_geopotential_(std::move(surface_height_m)), f_corner_(grid.corner_count()),
      latitude_rad_(mesh.cell_count()) {
    if (parameters_.layer_count == 0U) {
        throw std::invalid_argument("the primitive equations need at least one layer");
    }
    if (surface_geopotential_.size() != mesh.cell_count()) {
        throw std::invalid_argument("surface height size does not match the mesh");
    }
    if (grid.edge_count() != mesh.edge_count() || grid.corner_count() != mesh.corner_count()) {
        throw std::invalid_argument("C-grid does not belong to the mesh");
    }
    if (!(parameters_.gravity_m_s2 > 0.0) || !(parameters_.gas_constant_J_kg_K > 0.0) ||
        !(parameters_.heat_capacity_J_kg_K > 0.0) || !(parameters_.reference_pressure_Pa > 0.0) ||
        !(parameters_.hyperviscosity_m4_s >= 0.0)) {
        throw std::invalid_argument("invalid primitive-equation parameters");
    }
    for (auto& value : surface_geopotential_.values()) {
        value *= parameters_.gravity_m_s2;
    }
    const Vec3d axis = normalized(parameters_.rotation_axis);
    const auto corners = mesh.corners_unit();
    for (std::size_t index = 0; index < grid.corner_count(); ++index) {
        f_corner_[index] = 2.0 * parameters_.rotation_rate_rad_s * dot(axis, corners[index]);
    }
    for (const auto& cell : mesh.cells()) {
        latitude_rad_[cell.id] = std::asin(std::clamp(dot(axis, cell.center_unit), -1.0, 1.0));
    }
}

void PrimitiveEquationModel::set_surface_drag(std::vector<double> drag_coefficient) {
    if (!drag_coefficient.empty() && drag_coefficient.size() != mesh_->edge_count()) {
        throw std::invalid_argument("drag coefficients must be one per edge");
    }
    for (const double value : drag_coefficient) {
        if (!(value >= 0.0) || !std::isfinite(value)) {
            throw std::invalid_argument("drag coefficients must be finite and non-negative");
        }
    }
    drag_coefficient_ = std::move(drag_coefficient);
}

void PrimitiveEquationModel::column_structure(const PrimitiveEquationState& state,
                                              std::vector<double>& theta,
                                              std::vector<double>& exner_mean,
                                              std::vector<double>& exner_interface,
                                              std::vector<double>& geopotential_mean,
                                              std::size_t worker_count) const {
    const auto& mesh = *mesh_;
    const std::size_t n = parameters_.layer_count;
    const std::size_t cells = mesh.cell_count();
    const double g = parameters_.gravity_m_s2;
    const double cp = parameters_.heat_capacity_J_kg_K;
    const double kappa = parameters_.gas_constant_J_kg_K / cp;
    const double p0 = parameters_.reference_pressure_Pa;
    const double n_d = static_cast<double>(n);
    theta.resize(n * cells);
    exner_mean.resize(n * cells);
    exner_interface.resize((n + 1U) * cells);
    geopotential_mean.resize(n * cells);
    const auto ps = state.surface_pressure_Pa.values();
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t i = block.begin; i < block.end; ++i) {
                const double mu = ps[i] / (g * n_d);
                for (std::size_t m = 0; m <= n; ++m) {
                    const double p = ps[i] * (1.0 - static_cast<double>(m) / n_d);
                    exner_interface[m * cells + i] = m == n ? 0.0 : std::pow(p / p0, kappa);
                }
                double phi = surface_geopotential_[i];
                for (std::size_t k = 0; k < n; ++k) {
                    const double p_bottom = ps[i] * (1.0 - static_cast<double>(k) / n_d);
                    const double p_top = ps[i] * (1.0 - static_cast<double>(k + 1U) / n_d);
                    const double pi_bottom = exner_interface[k * cells + i];
                    const double pi_top = exner_interface[(k + 1U) * cells + i];
                    const double pi_mean = (p_bottom * pi_bottom - p_top * pi_top) /
                                           ((1.0 + kappa) * (p_bottom - p_top));
                    const double th = state.mass_theta.layer(k)[i] / mu;
                    theta[k * cells + i] = th;
                    exner_mean[k * cells + i] = pi_mean;
                    geopotential_mean[k * cells + i] = phi + cp * th * (pi_bottom - pi_mean);
                    phi += cp * th * (pi_bottom - pi_top);
                }
            }
        });
}

PrimitiveEquationState PrimitiveEquationModel::state_at_rest(
    const Field2D<double>& surface_pressure_Pa, const Field3D<double>& temperature_K) const {
    const auto& mesh = *mesh_;
    const std::size_t n = parameters_.layer_count;
    if (surface_pressure_Pa.size() != mesh.cell_count() || temperature_K.layer_count() != n ||
        temperature_K.cell_count() != mesh.cell_count()) {
        throw std::invalid_argument("rest state fields do not match the model");
    }
    PrimitiveEquationState state{surface_pressure_Pa, Field3D<double>(n, mesh.cell_count()),
                                 Field3D<double>(n, mesh.edge_count(), 0.0)};
    // Θ from T needs π̄, which depends only on p_s: compute it with Θ = 1.
    for (std::size_t k = 0; k < n; ++k) {
        for (std::size_t i = 0; i < mesh.cell_count(); ++i) {
            state.mass_theta.layer(k)[i] = 1.0;
        }
    }
    std::vector<double> theta;
    std::vector<double> exner_mean;
    std::vector<double> exner_interface;
    std::vector<double> geopotential;
    column_structure(state, theta, exner_mean, exner_interface, geopotential, 1U);
    const double mu_factor = 1.0 / (parameters_.gravity_m_s2 * static_cast<double>(n));
    for (std::size_t k = 0; k < n; ++k) {
        for (std::size_t i = 0; i < mesh.cell_count(); ++i) {
            const double mu = surface_pressure_Pa[i] * mu_factor;
            state.mass_theta.layer(k)[i] =
                mu * temperature_K.layer(k)[i] / exner_mean[k * mesh.cell_count() + i];
        }
    }
    return state;
}

void PrimitiveEquationModel::temperatures(const PrimitiveEquationState& state,
                                          Field3D<double>& temperature_K,
                                          std::size_t worker_count) const {
    std::vector<double> theta;
    std::vector<double> exner_mean;
    std::vector<double> exner_interface;
    std::vector<double> geopotential;
    column_structure(state, theta, exner_mean, exner_interface, geopotential, worker_count);
    const std::size_t cells = mesh_->cell_count();
    for (std::size_t k = 0; k < parameters_.layer_count; ++k) {
        auto layer = temperature_K.layer(k);
        for (std::size_t i = 0; i < cells; ++i) {
            layer[i] = theta[k * cells + i] * exner_mean[k * cells + i];
        }
    }
}

void PrimitiveEquationModel::tendency(const PrimitiveEquationState& state,
                                      PrimitiveEquationState& rate,
                                      std::size_t worker_count) const {
    const auto& mesh = *mesh_;
    const auto& grid = *grid_;
    const std::size_t n = parameters_.layer_count;
    const std::size_t cells = mesh.cell_count();
    const std::size_t edges = mesh.edge_count();
    const std::size_t corners = grid.corner_count();
    const double g = parameters_.gravity_m_s2;
    const double cp = parameters_.heat_capacity_J_kg_K;
    const double n_d = static_cast<double>(n);
    const auto ps = state.surface_pressure_Pa.values();

    std::vector<double> theta;
    std::vector<double> exner_mean;
    std::vector<double> exner_interface;
    std::vector<double> geopotential_mean;
    column_structure(state, theta, exner_mean, exner_interface, geopotential_mean, worker_count);

    // Edge mass fluxes F_k = μ̂ u_k and their cell divergences.
    std::vector<double> flux(n * edges);
    std::vector<double> mass_edge(edges);
    for_each_deterministic_block(
        grid.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t e = block.begin; e < block.end; ++e) {
                const auto& edge = mesh.edge(edge_id(e));
                mass_edge[e] = 0.5 * (ps[edge.first_cell.to_index()] +
                                      ps[edge.second_cell.to_index()]) /
                               (g * n_d);
                for (std::size_t k = 0; k < n; ++k) {
                    flux[k * edges + e] = mass_edge[e] * state.normal_velocity_m_s.layer(k)[e];
                }
            }
        });
    std::vector<double> flux_divergence(n * cells);
    std::vector<double> vertical_flux((n + 1U) * cells, 0.0);   // W_m, upward
    std::vector<double> interface_theta((n + 1U) * cells, 0.0); // θ̃_m
    auto dps = rate.surface_pressure_Pa.values();
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t i = block.begin; i < block.end; ++i) {
                double total = 0.0;
                for (std::size_t k = 0; k < n; ++k) {
                    const double div = cell_divergence(
                        mesh, i, std::span<const double>(flux).subspan(k * edges, edges));
                    flux_divergence[k * cells + i] = div;
                    total += div;
                }
                dps[i] = -g * total;
                // W_{k+1} = W_k − div F_k − ∂μ_k/∂t; W_N vanishes up to rounding.
                for (std::size_t k = 0; k + 1U < n; ++k) {
                    vertical_flux[(k + 1U) * cells + i] = vertical_flux[k * cells + i] -
                                                          flux_divergence[k * cells + i] -
                                                          dps[i] / (g * n_d);
                }
                for (std::size_t m = 1; m < n; ++m) {
                    const double below = exner_mean[(m - 1U) * cells + i];
                    const double above = exner_mean[m * cells + i];
                    const double at = exner_interface[m * cells + i];
                    interface_theta[m * cells + i] =
                        (theta[(m - 1U) * cells + i] * (below - at) +
                         theta[m * cells + i] * (at - above)) /
                        (below - above);
                }
            }
        });

    // Θ: horizontal flux θ̂ F, vertical flux W θ̃. The edge value
    // θ̂ = T̂ · ln(π̄_b/π̄_a) / (π̄_b − π̄_a), with T̂ the mean of the cells'
    // T = θ π̄, balances an isothermal atmosphere at rest over any terrain
    // exactly; the energy identity holds for any symmetric θ̂ that the Θ
    // flux and the pressure-gradient force share.
    std::vector<double> theta_edge(n * edges);
    std::vector<double> theta_flux(n * edges);
    for_each_deterministic_block(
        grid.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t e = block.begin; e < block.end; ++e) {
                const auto& edge = mesh.edge(edge_id(e));
                const std::size_t a = edge.first_cell.to_index();
                const std::size_t b = edge.second_cell.to_index();
                for (std::size_t k = 0; k < n; ++k) {
                    const double pi_a = exner_mean[k * cells + a];
                    const double pi_b = exner_mean[k * cells + b];
                    const double t_mean =
                        0.5 * (theta[k * cells + a] * pi_a + theta[k * cells + b] * pi_b);
                    theta_edge[k * edges + e] = t_mean * inverse_log_mean(pi_a, pi_b);
                    theta_flux[k * edges + e] = theta_edge[k * edges + e] * flux[k * edges + e];
                }
            }
        });
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t i = block.begin; i < block.end; ++i) {
                for (std::size_t k = 0; k < n; ++k) {
                    const double horizontal = cell_divergence(
                        mesh, i, std::span<const double>(theta_flux).subspan(k * edges, edges));
                    const double up = vertical_flux[(k + 1U) * cells + i] *
                                      interface_theta[(k + 1U) * cells + i];
                    const double down =
                        vertical_flux[k * cells + i] * interface_theta[k * cells + i];
                    rate.mass_theta.layer(k)[i] = -horizontal - (up - down);
                }
            }
        });

    // Corner potential vorticity and cell Bernoulli function, per layer.
    std::vector<double> corner_mass(corners);
    for_each_deterministic_block(
        grid.corner_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t v = block.begin; v < block.end; ++v) {
                const auto& corner = grid.corners()[v];
                double sum = 0.0;
                for (std::size_t c = 0; c < 3U; ++c) {
                    sum += corner.kite_area_m2[c] * ps[corner.cell[c].to_index()];
                }
                corner_mass[v] = sum / (corner.area_m2 * g * n_d);
            }
        });
    std::vector<double> pv(n * corners);
    std::vector<double> bernoulli(n * cells);
    for (std::size_t k = 0; k < n; ++k) {
        const auto u = state.normal_velocity_m_s.layer(k);
        for_each_deterministic_block(
            grid.corner_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                for (std::size_t v = block.begin; v < block.end; ++v) {
                    pv[k * corners + v] =
                        (corner_vorticity(mesh, grid.corners()[v], u) + f_corner_[v]) /
                        corner_mass[v];
                }
            });
        for_each_deterministic_block(
            mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                for (std::size_t i = block.begin; i < block.end; ++i) {
                    bernoulli[k * cells + i] =
                        cell_kinetic(mesh, i, u) + geopotential_mean[k * cells + i];
                }
            });
    }

    // Momentum.
    for_each_deterministic_block(
        grid.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t e = block.begin; e < block.end; ++e) {
                const EdgeId id = edge_id(e);
                const auto& edge = mesh.edge(id);
                const auto& c_edge = grid.edges()[e];
                const std::size_t a = edge.first_cell.to_index();
                const std::size_t b = edge.second_cell.to_index();
                const auto terms = grid.tangential_weight_edges(id);
                const auto weights = grid.tangential_weights(id);
                for (std::size_t k = 0; k < n; ++k) {
                    const double* q = pv.data() + k * corners;
                    const double q_e = 0.5 * (q[c_edge.vertex[0]] + q[c_edge.vertex[1]]);
                    double pv_flux = 0.0;
                    for (std::size_t t = 0; t < terms.size(); ++t) {
                        const std::size_t other = terms[t].to_index();
                        const auto& o = grid.edges()[other];
                        const double q_o = 0.5 * (q[o.vertex[0]] + q[o.vertex[1]]);
                        pv_flux += weights[t] * mesh.edge(terms[t]).length_m *
                                   flux[k * edges + other] * 0.5 * (q_e + q_o);
                    }
                    double du = (pv_flux - (bernoulli[k * cells + b] - bernoulli[k * cells + a]) -
                                 theta_edge[k * edges + e] * cp *
                                     (exner_mean[k * cells + b] - exner_mean[k * cells + a])) /
                                edge.centroid_distance_m;
                    // Vertical advection, centred, with Ŵ the edge mean of W.
                    const double u_k = state.normal_velocity_m_s.layer(k)[e];
                    double vertical = 0.0;
                    if (k + 1U < n) {
                        const double w_up = 0.5 * (vertical_flux[(k + 1U) * cells + a] +
                                                   vertical_flux[(k + 1U) * cells + b]);
                        vertical += w_up * (state.normal_velocity_m_s.layer(k + 1U)[e] - u_k);
                    }
                    if (k > 0U) {
                        const double w_down = 0.5 * (vertical_flux[k * cells + a] +
                                                     vertical_flux[k * cells + b]);
                        vertical += w_down * (u_k - state.normal_velocity_m_s.layer(k - 1U)[e]);
                    }
                    du -= vertical / (2.0 * mass_edge[e]);
                    rate.normal_velocity_m_s.layer(k)[e] = du;
                }
            }
        });

    // Dissipation: hyperviscosity and Held–Suarez friction; the kinetic
    // energy they remove returns as heat to the edge's two cells.
    const auto& hs = parameters_.held_suarez;
    const bool viscous = parameters_.hyperviscosity_m4_s > 0.0;
    const bool drag = !drag_coefficient_.empty();
    if (viscous || hs.enabled || drag) {
        std::vector<double> dissipation(n * edges, 0.0);
        if (viscous) {
            std::vector<double> once(edges);
            std::vector<double> twice(edges);
            std::vector<double> div(cells);
            std::vector<double> zeta(corners);
            const auto laplacian = [&](std::span<const double> in, std::vector<double>& out) {
                for_each_deterministic_block(
                    mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                        for (std::size_t i = block.begin; i < block.end; ++i) {
                            div[i] = cell_divergence(mesh, i, in);
                        }
                    });
                for_each_deterministic_block(
                    grid.corner_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                        for (std::size_t v = block.begin; v < block.end; ++v) {
                            zeta[v] = corner_vorticity(mesh, grid.corners()[v], in);
                        }
                    });
                for_each_deterministic_block(
                    grid.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                        for (std::size_t e = block.begin; e < block.end; ++e) {
                            const auto& edge = mesh.edge(edge_id(e));
                            const auto& c_edge = grid.edges()[e];
                            out[e] = (div[edge.second_cell.to_index()] -
                                      div[edge.first_cell.to_index()]) /
                                         edge.centroid_distance_m -
                                     (zeta[c_edge.vertex[1]] - zeta[c_edge.vertex[0]]) /
                                         edge.length_m;
                        }
                    });
            };
            for (std::size_t k = 0; k < n; ++k) {
                laplacian(state.normal_velocity_m_s.layer(k), once);
                laplacian(once, twice);
                for (std::size_t e = 0; e < edges; ++e) {
                    dissipation[k * edges + e] -= parameters_.hyperviscosity_m4_s * twice[e];
                }
            }
        }
        if (hs.enabled) {
            for (std::size_t k = 0; k < n; ++k) {
                const double sigma = 1.0 - (static_cast<double>(k) + 0.5) / n_d;
                const double factor = std::max(0.0, (sigma - hs.boundary_layer_sigma) /
                                                        (1.0 - hs.boundary_layer_sigma));
                if (factor > 0.0) {
                    const auto u = state.normal_velocity_m_s.layer(k);
                    for (std::size_t e = 0; e < edges; ++e) {
                        dissipation[k * edges + e] -= factor / hs.friction_s * u[e];
                    }
                }
            }
        }
        if (drag) {
            // Bottom layer: |V| from u and TRiSK's tangential component, T_0
            // the mean of the edge's cells' bottom-layer temperatures.
            const auto u = state.normal_velocity_m_s.layer(0);
            const double factor =
                parameters_.gravity_m_s2 * n_d / parameters_.gas_constant_J_kg_K;
            for_each_deterministic_block(
                grid.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                    for (std::size_t e = block.begin; e < block.end; ++e) {
                        const EdgeId id = edge_id(e);
                        const auto& edge = mesh.edge(id);
                        const auto terms = grid.tangential_weight_edges(id);
                        const auto weights = grid.tangential_weights(id);
                        double tangential = 0.0;
                        for (std::size_t t = 0; t < terms.size(); ++t) {
                            tangential += weights[t] * mesh.edge(terms[t]).length_m *
                                          u[terms[t].to_index()];
                        }
                        tangential /= edge.centroid_distance_m;
                        const std::size_t a = edge.first_cell.to_index();
                        const std::size_t b = edge.second_cell.to_index();
                        const double t0 = 0.5 * (theta[a] * exner_mean[a] + theta[b] * exner_mean[b]);
                        const double speed = std::sqrt(u[e] * u[e] + tangential * tangential);
                        dissipation[e] -= drag_coefficient_[e] * speed * u[e] * factor / t0;
                    }
                });
        }
        for (std::size_t k = 0; k < n; ++k) {
            auto du = rate.normal_velocity_m_s.layer(k);
            for (std::size_t e = 0; e < edges; ++e) {
                du[e] += dissipation[k * edges + e];
            }
        }
        // Heat: −(1/A_i) Σ_e ½ l d F du_diss, in the same layer.
        for_each_deterministic_block(
            mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                for (std::size_t i = block.begin; i < block.end; ++i) {
                    const auto& cell = mesh.cells()[i];
                    for (std::size_t k = 0; k < n; ++k) {
                        double work = 0.0;
                        for (const auto& cell_edge : mesh.cell_edges(cell.id)) {
                            const std::size_t e = cell_edge.edge.to_index();
                            const auto& edge = mesh.edge(cell_edge.edge);
                            work += 0.5 * edge.length_m * edge.centroid_distance_m *
                                    flux[k * edges + e] * dissipation[k * edges + e];
                        }
                        const double heat_W_m2 = -work / cell.area_m2;
                        rate.mass_theta.layer(k)[i] += heat_W_m2 / (cp * exner_mean[k * cells + i]);
                    }
                }
            });
    }
    if (hs.enabled) {
        const double kappa = parameters_.gas_constant_J_kg_K / cp;
        const double p0 = parameters_.reference_pressure_Pa;
        for_each_deterministic_block(
            mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
                for (std::size_t i = block.begin; i < block.end; ++i) {
                    const double lat = latitude_rad_[i];
                    const double s2 = std::sin(lat) * std::sin(lat);
                    const double c2 = 1.0 - s2;
                    const double mu = ps[i] / (g * n_d);
                    for (std::size_t k = 0; k < n; ++k) {
                        const double pi_mean = exner_mean[k * cells + i];
                        const double p = p0 * std::pow(pi_mean, 1.0 / kappa);
                        const double sigma = p / ps[i];
                        const double t_eq = std::max(
                            200.0, (315.0 - hs.equator_pole_K * s2 -
                                    hs.static_stability_K * std::log(p / p0) * c2) *
                                       pi_mean);
                        const double factor = std::max(0.0, (sigma - hs.boundary_layer_sigma) /
                                                                (1.0 - hs.boundary_layer_sigma));
                        const double k_t = 1.0 / hs.relaxation_free_s +
                                           (1.0 / hs.relaxation_surface_s -
                                            1.0 / hs.relaxation_free_s) *
                                               factor * c2 * c2;
                        const double t = theta[k * cells + i] * pi_mean;
                        rate.mass_theta.layer(k)[i] += mu * (-k_t * (t - t_eq)) / pi_mean;
                    }
                }
            });
    }
}

void PrimitiveEquationModel::step(PrimitiveEquationState& state, double dt_s,
                                  std::size_t worker_count) const {
    if (!(dt_s > 0.0) || !std::isfinite(dt_s)) {
        throw std::invalid_argument("a dynamics step must be positive and finite");
    }
    PrimitiveEquationState rate = sized_like(state);
    PrimitiveEquationState stage = sized_like(state);
    const auto combine = [&](double scale, PrimitiveEquationState& out) {
        const auto a_ps = state.surface_pressure_Pa.values();
        const auto r_ps = rate.surface_pressure_Pa.values();
        auto o_ps = out.surface_pressure_Pa.values();
        for (std::size_t i = 0; i < a_ps.size(); ++i) {
            o_ps[i] = a_ps[i] + scale * r_ps[i];
        }
        for (std::size_t k = 0; k < parameters_.layer_count; ++k) {
            const auto a_t = state.mass_theta.layer(k);
            const auto r_t = rate.mass_theta.layer(k);
            auto o_t = out.mass_theta.layer(k);
            for (std::size_t i = 0; i < a_t.size(); ++i) {
                o_t[i] = a_t[i] + scale * r_t[i];
            }
            const auto a_u = state.normal_velocity_m_s.layer(k);
            const auto r_u = rate.normal_velocity_m_s.layer(k);
            auto o_u = out.normal_velocity_m_s.layer(k);
            for (std::size_t e = 0; e < a_u.size(); ++e) {
                o_u[e] = a_u[e] + scale * r_u[e];
            }
        }
    };
    tendency(state, rate, worker_count);
    combine(dt_s / 3.0, stage);
    tendency(stage, rate, worker_count);
    combine(dt_s / 2.0, stage);
    tendency(stage, rate, worker_count);
    combine(dt_s, state);
}

std::size_t PrimitiveEquationModel::advance(PrimitiveEquationState& state, double span_s,
                                            const SubstepRule& rule,
                                            std::size_t worker_count) const {
    const std::size_t steps = substep_count(*mesh_, span_s, rule);
    const double dt = span_s / static_cast<double>(steps);
    for (std::size_t index = 0; index < steps; ++index) {
        step(state, dt, worker_count);
        double max_wind = 0.0;
        for (std::size_t k = 0; k < parameters_.layer_count; ++k) {
            for (const double value : state.normal_velocity_m_s.layer(k)) {
                max_wind = std::max(max_wind, std::abs(value));
            }
        }
        if (!(max_wind <= rule.max_wind_m_s)) {
            throw std::runtime_error("wind of " + std::to_string(max_wind) +
                                     " m/s exceeds the sub-step rule's bound of " +
                                     std::to_string(rule.max_wind_m_s) + " m/s");
        }
    }
    return steps;
}

PrimitiveEquationDiagnostics PrimitiveEquationModel::diagnose(
    const PrimitiveEquationState& state, std::size_t worker_count) const {
    const auto& mesh = *mesh_;
    const std::size_t n = parameters_.layer_count;
    const std::size_t cells = mesh.cell_count();
    const double g = parameters_.gravity_m_s2;
    const double cp = parameters_.heat_capacity_J_kg_K;
    std::vector<double> theta;
    std::vector<double> exner_mean;
    std::vector<double> exner_interface;
    std::vector<double> geopotential;
    column_structure(state, theta, exner_mean, exner_interface, geopotential, worker_count);
    const auto ps = state.surface_pressure_Pa.values();
    struct Sums {
        double mass = 0.0;
        double kinetic = 0.0;
        double enthalpy = 0.0;
        double surface = 0.0;
    };
    const auto sums = reduce_deterministic_blocks(
        mesh.blocks(), worker_count, Sums{},
        [&](std::size_t, const CellBlock& block) {
            Sums part;
            for (std::size_t i = block.begin; i < block.end; ++i) {
                const double area = mesh.cells()[i].area_m2;
                const double mu = ps[i] / (g * static_cast<double>(n));
                part.mass += area * ps[i] / g;
                part.surface += area * ps[i] * surface_geopotential_[i] / g;
                for (std::size_t k = 0; k < n; ++k) {
                    part.kinetic +=
                        area * mu * cell_kinetic(mesh, i, state.normal_velocity_m_s.layer(k));
                    part.enthalpy += area * cp * state.mass_theta.layer(k)[i] *
                                     exner_mean[k * cells + i];
                }
            }
            return part;
        },
        [](Sums total, const Sums& part) {
            total.mass += part.mass;
            total.kinetic += part.kinetic;
            total.enthalpy += part.enthalpy;
            total.surface += part.surface;
            return total;
        });
    double max_wind = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        for (const double value : state.normal_velocity_m_s.layer(k)) {
            max_wind = std::max(max_wind, std::abs(value));
        }
    }
    return {sums.mass, sums.kinetic, sums.enthalpy, sums.surface, max_wind};
}

PrimitiveEquationModel::EnergyRate PrimitiveEquationModel::energy_rate(
    const PrimitiveEquationState& state, const PrimitiveEquationState& rate) const {
    const auto& mesh = *mesh_;
    const std::size_t n = parameters_.layer_count;
    const std::size_t cells = mesh.cell_count();
    const std::size_t edges = mesh.edge_count();
    const double g = parameters_.gravity_m_s2;
    const double cp = parameters_.heat_capacity_J_kg_K;
    const double n_d = static_cast<double>(n);
    std::vector<double> theta;
    std::vector<double> exner_mean;
    std::vector<double> exner_interface;
    std::vector<double> geopotential_mean;
    column_structure(state, theta, exner_mean, exner_interface, geopotential_mean, 1U);
    const auto ps = state.surface_pressure_Pa.values();
    EnergyRate result;
    const auto add = [&](double term) {
        result.rate_W += term;
        result.gross_W += std::abs(term);
    };
    for (std::size_t i = 0; i < cells; ++i) {
        const double area = mesh.cells()[i].area_m2;
        const double dmu = rate.surface_pressure_Pa[i] / (g * n_d);
        for (std::size_t k = 0; k < n; ++k) {
            const double bernoulli = cell_kinetic(mesh, i, state.normal_velocity_m_s.layer(k)) +
                                     geopotential_mean[k * cells + i];
            add(area * bernoulli * dmu);
            add(area * cp * exner_mean[k * cells + i] * rate.mass_theta.layer(k)[i]);
        }
    }
    for (std::size_t e = 0; e < edges; ++e) {
        const auto& edge = mesh.edge(edge_id(e));
        const double mass = 0.5 * (ps[edge.first_cell.to_index()] +
                                   ps[edge.second_cell.to_index()]) /
                            (g * n_d);
        for (std::size_t k = 0; k < n; ++k) {
            add(edge.length_m * edge.centroid_distance_m * mass *
                state.normal_velocity_m_s.layer(k)[e] * rate.normal_velocity_m_s.layer(k)[e]);
        }
    }
    return result;
}

}  // namespace planetsim
