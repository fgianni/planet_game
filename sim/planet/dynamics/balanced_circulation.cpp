#include "sim/planet/dynamics/balanced_circulation.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/coordinates/local_tangent_basis.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>

namespace planetsim {
namespace {

constexpr double pi = std::numbers::pi_v<double>;

[[nodiscard]] EdgeId edge_id(std::size_t index) noexcept {
    return EdgeId{static_cast<EdgeId::value_type>(index)};
}

// The zonal means' band count on the coarse mesh: the largest of 36, 18,
// 12, 9 and 6 equal-width bands whose every band holds three cell centres.
[[nodiscard]] std::size_t band_count_for(const std::vector<double>& latitude_rad) {
    for (const std::size_t bands : {36U, 18U, 12U, 9U, 6U}) {
        std::vector<std::size_t> count(bands, 0U);
        const double width = pi / static_cast<double>(bands);
        for (const double latitude : latitude_rad) {
            const auto band = static_cast<std::size_t>(std::clamp(
                std::floor((latitude + 0.5 * pi) / width), 0.0, static_cast<double>(bands - 1U)));
            ++count[band];
        }
        if (*std::min_element(count.begin(), count.end()) >= 3U) {
            return bands;
        }
    }
    return 1U;
}

[[nodiscard]] std::size_t band_of(double latitude_rad, std::size_t bands) {
    const double width = pi / static_cast<double>(bands);
    return static_cast<std::size_t>(std::clamp(std::floor((latitude_rad + 0.5 * pi) / width), 0.0,
                                               static_cast<double>(bands - 1U)));
}

// Linear interpolation in latitude of values given at increasing
// latitudes, held constant beyond the ends.
[[nodiscard]] double interpolate_latitude(const std::vector<double>& latitude_deg,
                                          std::span<const double> values, double at_deg) {
    if (at_deg <= latitude_deg.front()) {
        return values.front();
    }
    if (at_deg >= latitude_deg.back()) {
        return values.back();
    }
    const auto upper = static_cast<std::size_t>(
        std::upper_bound(latitude_deg.begin(), latitude_deg.end(), at_deg) - latitude_deg.begin());
    const std::size_t lower = upper - 1U;
    const double fraction =
        (at_deg - latitude_deg[lower]) / (latitude_deg[upper] - latitude_deg[lower]);
    return values[lower] + fraction * (values[upper] - values[lower]);
}

// ILU(0) of a matrix on a transport graph's pattern (the diagonal and the
// CSR off-diagonals), in the graph's node order. The balance's operator is
// non-symmetric (its Coriolis part outweighs the friction away from the
// equator), which the multigrid on its symmetric part does not see; this
// factor of the full operator, applied after the V-cycle, does.
// Sequential, so deterministic.
class IncompleteLU {
  public:
    IncompleteLU(const TransportGraph& graph, const std::vector<double>& diagonal,
                 const std::vector<double>& off, double diagonal_scale) {
        const std::size_t nodes = graph.size();
        start_.assign(nodes + 1U, 0U);
        for (std::size_t i = 0; i < nodes; ++i) {
            start_[i + 1U] = start_[i] + 1U + (graph.offset[i + 1U] - graph.offset[i]);
        }
        column_.resize(start_[nodes]);
        value_.resize(start_[nodes]);
        diagonal_.resize(nodes);
        std::vector<std::pair<std::size_t, double>> row;
        for (std::size_t i = 0; i < nodes; ++i) {
            row.clear();
            row.emplace_back(i, diagonal[i] * diagonal_scale);
            for (std::size_t k = graph.offset[i]; k < graph.offset[i + 1U]; ++k) {
                row.emplace_back(graph.neighbour[k], off[k]);
            }
            std::sort(row.begin(), row.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            for (std::size_t q = 0; q < row.size(); ++q) {
                column_[start_[i] + q] = row[q].first;
                value_[start_[i] + q] = row[q].second;
                if (row[q].first == i) {
                    diagonal_[i] = start_[i] + q;
                }
            }
        }
        // IKJ elimination restricted to the pattern.
        std::vector<std::size_t> position(nodes, nodes == 0U ? 0U : start_[nodes]);
        const std::size_t absent = start_[nodes];
        for (std::size_t i = 0; i < nodes; ++i) {
            for (std::size_t q = start_[i]; q < start_[i + 1U]; ++q) {
                position[column_[q]] = q;
            }
            for (std::size_t q = start_[i]; q < diagonal_[i]; ++q) {
                const std::size_t j = column_[q];
                const double pivot = value_[diagonal_[j]];
                if (pivot == 0.0) {
                    throw std::runtime_error("balanced surface pressure: zero ILU pivot");
                }
                value_[q] /= pivot;
                for (std::size_t t = diagonal_[j] + 1U; t < start_[j + 1U]; ++t) {
                    const std::size_t at = position[column_[t]];
                    if (at != absent) {
                        value_[at] -= value_[q] * value_[t];
                    }
                }
            }
            for (std::size_t q = start_[i]; q < start_[i + 1U]; ++q) {
                position[column_[q]] = absent;
            }
        }
    }

    // x ← (LU)⁻¹ x.
    void solve(std::vector<double>& x) const {
        const std::size_t nodes = diagonal_.size();
        for (std::size_t i = 0; i < nodes; ++i) {
            double sum = x[i];
            for (std::size_t q = start_[i]; q < diagonal_[i]; ++q) {
                sum -= value_[q] * x[column_[q]];
            }
            x[i] = sum;
        }
        for (std::size_t i = nodes; i-- > 0U;) {
            double sum = x[i];
            for (std::size_t q = diagonal_[i] + 1U; q < start_[i + 1U]; ++q) {
                sum -= value_[q] * x[column_[q]];
            }
            x[i] = sum / value_[diagonal_[i]];
        }
    }

  private:
    std::vector<std::size_t> start_;
    std::vector<std::size_t> column_;
    std::vector<double> value_;
    std::vector<std::size_t> diagonal_;
};

}  // namespace

BalancedCirculation::BalancedCirculation(const PlanetMesh& mesh,
                                         const ZonalCirculationParameters& zonal,
                                         BalancedCirculationParameters parameters)
    : mesh_(&mesh), coarse_(&agglomerated_mesh(mesh)),
      graph_(&agglomerated_transport_graph(mesh, group_of_cell_)),
      grid_(CGridGeometry::build(*coarse_)), zonal_(zonal), parameters_(parameters) {
    if (!(parameters_.free_damping_s > 0.0) || !(parameters_.drag_speed_m_s >= 0.0) ||
        !(parameters_.relative_tolerance > 0.0) || parameters_.max_iterations == 0U ||
        !(parameters_.drag.ocean >= 0.0) || !(parameters_.drag.land >= 0.0) ||
        !(parameters_.equatorial_damping_s >= 0.0) || !(parameters_.equatorial_width_deg > 0.0)) {
        throw std::invalid_argument("invalid balanced circulation parameters");
    }
    if (zonal_.layer_count == 0U) {
        throw std::invalid_argument("the balanced circulation needs layers");
    }
    latitude_rad_.resize(coarse_->cell_count());
    for (const auto& cell : coarse_->cells()) {
        latitude_rad_[cell.id.to_index()] = latitude_rad(cell.center_unit);
    }
}

BalancedCirculationResult BalancedCirculation::solve(const SlowState& slow,
                                                     const Field2D<double>& dynamics_height_m,
                                                     const SurfaceFractions& fractions,
                                                     const ZonalCirculationSolution& zonal,
                                                     std::size_t worker_count) const {
    const PlanetMesh& mesh = *mesh_;
    const PlanetMesh& coarse = *coarse_;
    const TransportGraph& graph = *graph_;
    const auto& group_of = *group_of_cell_;
    const std::size_t n = zonal_.layer_count;
    const std::size_t cells = mesh.cell_count();
    const std::size_t groups = coarse.cell_count();
    const std::size_t edges = coarse.edge_count();
    if (slow.atmosphere_layer_count() != n || dynamics_height_m.size() != cells ||
        fractions.land_fraction.size() != cells || zonal.layers != n ||
        zonal.bands != zonal_.bands) {
        throw std::invalid_argument("balanced circulation inputs do not match");
    }
    const double g = zonal_.gravity_m_s2;
    const double gas = zonal_.gas_constant_J_kg_K;
    const double cp = zonal_.heat_capacity_J_kg_K;
    const double kappa = gas / cp;
    const double p0 = zonal_.reference_pressure_Pa;
    const double n_d = static_cast<double>(n);
    const auto& blocks = graph.blocks;

    // 1. The groups' means: p_s, height and land fraction by area, T by mass.
    std::vector<double> ps(groups, 0.0);
    std::vector<double> height(groups, 0.0);
    std::vector<double> land(groups, 0.0);
    std::vector<double> temperature(n * groups, 0.0);
    std::vector<double> mass(groups, 0.0);
    for (std::size_t i = 0; i < cells; ++i) {
        const std::size_t c = group_of[i];
        const double a = mesh.cells()[i].area_m2;
        const double p = slow.atmosphere_surface_pressure_Pa[i];
        ps[c] += a * p;
        mass[c] += a * p;
        height[c] += a * dynamics_height_m[i];
        land[c] += a * std::clamp(static_cast<double>(fractions.land_fraction[i]), 0.0, 1.0);
        for (std::size_t k = 0; k < n; ++k) {
            temperature[k * groups + c] += a * p * slow.atmosphere_temperature_K.layer(k)[i];
        }
    }
    for (std::size_t c = 0; c < groups; ++c) {
        const double area = graph.area_m2[c];
        ps[c] /= area;
        height[c] /= area;
        land[c] /= area;
        for (std::size_t k = 0; k < n; ++k) {
            temperature[k * groups + c] /= mass[c];
        }
    }

    // 2. Layer geopotentials (task M6-03's vertical structure) and their
    // departures from the zonal means on the coarse mesh's bands.
    std::vector<double> geopotential(n * groups);
    for_each_deterministic_block(
        std::span<const CellBlock>(blocks), worker_count,
        [&](std::size_t, const CellBlock& block) {
            for (std::size_t c = block.begin; c < block.end; ++c) {
                double phi = g * height[c];
                double pi_bottom = std::pow(ps[c] / p0, kappa);
                for (std::size_t k = 0; k < n; ++k) {
                    const double p_bottom = ps[c] * (1.0 - static_cast<double>(k) / n_d);
                    const double p_top = ps[c] * (1.0 - static_cast<double>(k + 1U) / n_d);
                    const double pi_top = k + 1U == n ? 0.0 : std::pow(p_top / p0, kappa);
                    const double pi_mean = (p_bottom * pi_bottom - p_top * pi_top) /
                                           ((1.0 + kappa) * (p_bottom - p_top));
                    const double theta = temperature[k * groups + c] / pi_mean;
                    geopotential[k * groups + c] = phi + cp * theta * (pi_bottom - pi_mean);
                    phi += cp * theta * (pi_bottom - pi_top);
                    pi_bottom = pi_top;
                }
            }
        });
    const std::size_t bands = band_count_for(latitude_rad_);
    std::vector<std::size_t> band(groups);
    std::vector<double> band_area(bands, 0.0);
    for (std::size_t c = 0; c < groups; ++c) {
        band[c] = band_of(latitude_rad_[c], bands);
        band_area[band[c]] += graph.area_m2[c];
    }
    // The zonal mean at a cell: the band means interpolated linearly in
    // latitude between the band centres, so that departures have no steps
    // at the bands' edges.
    std::vector<double> band_centre_deg(bands);
    for (std::size_t j = 0; j < bands; ++j) {
        band_centre_deg[j] = -90.0 + 180.0 / static_cast<double>(bands) * (static_cast<double>(j) + 0.5);
    }
    std::vector<double> departure(n * groups);
    for (std::size_t k = 0; k < n; ++k) {
        std::vector<double> mean(bands, 0.0);
        for (std::size_t c = 0; c < groups; ++c) {
            mean[band[c]] += graph.area_m2[c] * geopotential[k * groups + c];
        }
        for (std::size_t j = 0; j < bands; ++j) {
            mean[j] /= band_area[j];
        }
        for (std::size_t c = 0; c < groups; ++c) {
            departure[k * groups + c] =
                geopotential[k * groups + c] -
                interpolate_latitude(band_centre_deg, mean, latitude_rad_[c] * 180.0 / pi);
        }
    }
    std::vector<double> corner_departure(n * grid_.corner_count());
    for (std::size_t v = 0; v < grid_.corner_count(); ++v) {
        const auto& corner = grid_.corners()[v];
        for (std::size_t k = 0; k < n; ++k) {
            double sum = 0.0;
            for (std::size_t m = 0; m < 3U; ++m) {
                sum += corner.interpolation_weight[m] *
                       departure[k * groups + corner.cell[m].to_index()];
            }
            corner_departure[k * grid_.corner_count() + v] = sum;
        }
    }

    // 3. Per edge and layer: the balance's coefficients. u_n = (r G_n +
    // f G_t) / (r² + f²); its s part is −R T̂ (r ∂_n s + f ∂_t s) / (r² + f²).
    const double omega = zonal_.rotation_rate_rad_s;
    std::vector<double> edge_mass(edges);
    std::vector<double> coefficient_n(edges, 0.0);   // Σ_k μ̂ R T̂ r / (r² + f²)
    std::vector<double> coefficient_t(edges, 0.0);   // Σ_k μ̂ R T̂ f / (r² + f²)
    std::vector<double> known_flux(edges, 0.0);      // Σ_k μ̂ (r G_n^Φ + f G_t^Φ) / (r² + f²)
    std::vector<double> layer_rate(n * edges);       // r_k per edge
    std::vector<double> edge_f(edges);
    for_each_deterministic_block(
        grid_.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t e = block.begin; e < block.end; ++e) {
                const auto& edge = coarse.edge(edge_id(e));
                const auto& c_edge = grid_.edges()[e];
                const std::size_t a = edge.first_cell.to_index();
                const std::size_t b = edge.second_cell.to_index();
                const double mu = 0.5 * (ps[a] + ps[b]) / (g * n_d);
                const double f = 2.0 * omega * c_edge.midpoint_unit.z;
                edge_mass[e] = mu;
                edge_f[e] = f;
                double free_rate = 1.0 / parameters_.free_damping_s;
                if (parameters_.equatorial_damping_s > 0.0) {
                    const double latitude_deg =
                        std::asin(std::clamp(c_edge.midpoint_unit.z, -1.0, 1.0)) * 180.0 / pi;
                    const double x = latitude_deg / parameters_.equatorial_width_deg;
                    free_rate += std::exp(-x * x) / parameters_.equatorial_damping_s;
                }
                const double drag_coefficient =
                    0.5 * (land[a] + land[b]) * parameters_.drag.land +
                    (1.0 - 0.5 * (land[a] + land[b])) * parameters_.drag.ocean;
                for (std::size_t k = 0; k < n; ++k) {
                    const double t_edge =
                        0.5 * (temperature[k * groups + a] + temperature[k * groups + b]);
                    double r = free_rate;
                    if (k == 0U) {
                        r += drag_coefficient * parameters_.drag_speed_m_s * g * n_d /
                             (gas * t_edge);
                    }
                    layer_rate[k * edges + e] = r;
                    const double denominator = r * r + f * f;
                    coefficient_n[e] += mu * gas * t_edge * r / denominator;
                    coefficient_t[e] += mu * gas * t_edge * f / denominator;
                    const double gn = -(departure[k * groups + b] - departure[k * groups + a]) /
                                      edge.centroid_distance_m;
                    const std::size_t corners = grid_.corner_count();
                    const double gt =
                        -(corner_departure[k * corners + c_edge.vertex[1]] -
                          corner_departure[k * corners + c_edge.vertex[0]]) /
                        edge.length_m;
                    known_flux[e] += mu * (r * gn + f * gt) / denominator;
                }
            }
        });

    // 4. The matrix K s = rhs, row a = −Σ_e out_ae l_e (s part of the flux):
    // −div(c_n ∂_n s) − div(c_t ∂_t s), area-integrated, in the graph's CSR
    // pattern; and the preconditioner's symmetric part.
    std::vector<double> diagonal(groups, 0.0);
    std::vector<double> off(graph.neighbour.size(), 0.0);
    std::vector<double> rhs(groups, 0.0);
    std::vector<double> sym_diagonal(groups, 0.0);
    std::vector<double> sym_off(graph.neighbour.size(), 0.0);
    for_each_deterministic_block(
        std::span<const CellBlock>(blocks), worker_count,
        [&](std::size_t, const CellBlock& block) {
            for (std::size_t a = block.begin; a < block.end; ++a) {
                const CellId id{static_cast<CellId::value_type>(a)};
                const auto column_of = [&](std::size_t cell) -> double& {
                    if (cell == a) {
                        return diagonal[a];
                    }
                    for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
                        if (graph.neighbour[k] == cell) {
                            return off[k];
                        }
                    }
                    throw std::logic_error("a corner cell outside the cell's ring");
                };
                for (const auto& cell_edge : coarse.cell_edges(id)) {
                    const std::size_t e = cell_edge.edge.to_index();
                    const auto& edge = coarse.edge(cell_edge.edge);
                    const auto& c_edge = grid_.edges()[e];
                    const double out = edge.first_cell.to_index() == a ? 1.0 : -1.0;
                    const std::size_t other = cell_edge.neighbor.to_index();
                    const double normal = edge.length_m * coefficient_n[e] /
                                          edge.centroid_distance_m;
                    diagonal[a] += normal;
                    column_of(other) -= normal;
                    sym_diagonal[a] += normal;
                    for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
                        if (graph.neighbour[k] == other) {
                            sym_off[k] -= normal;
                        }
                    }
                    for (std::size_t side = 0; side < 2U; ++side) {
                        const auto& corner = grid_.corners()[c_edge.vertex[side]];
                        const double sign = side == 1U ? -out : out;
                        for (std::size_t m = 0; m < 3U; ++m) {
                            column_of(corner.cell[m].to_index()) +=
                                sign * coefficient_t[e] * corner.interpolation_weight[m];
                        }
                    }
                    rhs[a] -= out * edge.length_m * known_flux[e];
                }
                // The preconditioner regularises the constant null mode.
                sym_diagonal[a] *= 1.0 + 1.0e-8;
            }
        });

    // 5. BiCGSTAB with the multigrid V-cycle as right preconditioner.
    const auto dot = [&](const std::vector<double>& x, const std::vector<double>& y) {
        return reduce_deterministic_blocks<double>(
            std::span<const CellBlock>(blocks), worker_count, 0.0,
            [&](std::size_t, const CellBlock& block) {
                double sum = 0.0;
                for (std::size_t c = block.begin; c < block.end; ++c) {
                    sum += x[c] * y[c];
                }
                return sum;
            },
            [](double p, double q) { return p + q; });
    };
    const auto apply = [&](const std::vector<double>& x, std::vector<double>& y) {
        for_each_deterministic_block(
            std::span<const CellBlock>(blocks), worker_count,
            [&](std::size_t, const CellBlock& block) {
                for (std::size_t c = block.begin; c < block.end; ++c) {
                    double sum = diagonal[c] * x[c];
                    for (std::size_t k = graph.offset[c]; k < graph.offset[c + 1U]; ++k) {
                        sum += off[k] * x[graph.neighbour[k]];
                    }
                    y[c] = sum;
                }
            });
    };
    {
        // The rows sum to zero for any s (flux form): remove the right-hand
        // side's rounding component along that direction.
        double sum = 0.0;
        for (const double value : rhs) {
            sum += value;
        }
        for (double& value : rhs) {
            value -= sum / static_cast<double>(groups);
        }
    }
    GraphMultigrid multigrid(graph);
    multigrid.set_matrix(sym_diagonal, sym_off);
    const IncompleteLU incomplete(graph, diagonal, off, 1.0 + 1.0e-8);
    std::vector<double> correction(groups, 0.0);
    // The preconditioner: the V-cycle, then ILU(0) of the full operator on
    // what the V-cycle leaves.
    const auto precondition = [&](const std::vector<double>& in, std::vector<double>& out) {
        multigrid.precondition(in, out, worker_count);
        apply(out, correction);
        for (std::size_t c = 0; c < groups; ++c) {
            correction[c] = in[c] - correction[c];
        }
        incomplete.solve(correction);
        for (std::size_t c = 0; c < groups; ++c) {
            out[c] += correction[c];
        }
    };
    std::vector<double> s(groups, 0.0);
    std::vector<double> residual = rhs;
    const std::vector<double> shadow = residual;
    std::vector<double> direction(groups, 0.0);
    std::vector<double> product_p(groups, 0.0);
    std::vector<double> p_hat;
    std::vector<double> s_hat;
    std::vector<double> half(groups, 0.0);
    std::vector<double> product_s(groups, 0.0);
    const double rhs_norm = std::sqrt(dot(rhs, rhs));
    double rho = 1.0;
    double alpha = 1.0;
    double omega_k = 1.0;
    BalancedCirculationResult result;
    result.layers = n;
    result.bands = bands;
    double residual_norm = rhs_norm;
    std::size_t iteration = 0;
    while (rhs_norm > 0.0 && residual_norm > parameters_.relative_tolerance * rhs_norm) {
        if (iteration == parameters_.max_iterations) {
            throw std::runtime_error(
                "balanced surface pressure: BiCGSTAB did not converge in " +
                std::to_string(iteration) + " iterations (relative residual " +
                std::to_string(residual_norm / rhs_norm) + ")");
        }
        const double rho_next = dot(shadow, residual);
        if (rho_next == 0.0 || omega_k == 0.0) {
            throw std::runtime_error("balanced surface pressure: BiCGSTAB broke down");
        }
        const double beta = rho_next / rho * (alpha / omega_k);
        rho = rho_next;
        for (std::size_t c = 0; c < groups; ++c) {
            direction[c] = residual[c] + beta * (direction[c] - omega_k * product_p[c]);
        }
        precondition(direction, p_hat);
        apply(p_hat, product_p);
        alpha = rho / dot(shadow, product_p);
        for (std::size_t c = 0; c < groups; ++c) {
            half[c] = residual[c] - alpha * product_p[c];
        }
        ++iteration;
        if (std::sqrt(dot(half, half)) <= parameters_.relative_tolerance * rhs_norm) {
            for (std::size_t c = 0; c < groups; ++c) {
                s[c] += alpha * p_hat[c];
            }
            residual = half;
            residual_norm = std::sqrt(dot(residual, residual));
            break;
        }
        precondition(half, s_hat);
        apply(s_hat, product_s);
        omega_k = dot(product_s, half) / dot(product_s, product_s);
        for (std::size_t c = 0; c < groups; ++c) {
            s[c] += alpha * p_hat[c] + omega_k * s_hat[c];
            residual[c] = half[c] - omega_k * product_s[c];
        }
        residual_norm = std::sqrt(dot(residual, residual));
    }
    result.iterations = iteration;
    {
        // The true residual, not the recurrence's.
        apply(s, product_p);
        for (std::size_t c = 0; c < groups; ++c) {
            product_p[c] -= rhs[c];
        }
        result.relative_residual = rhs_norm > 0.0 ? std::sqrt(dot(product_p, product_p)) / rhs_norm
                                                  : 0.0;
    }
    {
        // The null mode: s has zero area mean.
        double weighted = 0.0;
        double area = 0.0;
        for (std::size_t c = 0; c < groups; ++c) {
            weighted += graph.area_m2[c] * s[c];
            area += graph.area_m2[c];
        }
        for (double& value : s) {
            value -= weighted / area;
        }
        std::vector<double> band_mean(bands, 0.0);
        for (std::size_t c = 0; c < groups; ++c) {
            band_mean[band[c]] += graph.area_m2[c] * s[c];
        }
        for (std::size_t j = 0; j < bands; ++j) {
            result.max_band_mean_departure =
                std::max(result.max_band_mean_departure, std::abs(band_mean[j] / band_area[j]));
        }
    }
    result.log_departure = s;

    // 6. The layer mass fluxes: the overturning mapped to the edges plus the
    // azonal part.
    const std::size_t corners = grid_.corner_count();
    std::vector<double> corner_s(corners);
    for (std::size_t index = 0; index < corners; ++index) {
        const auto& corner = grid_.corners()[index];
        double sum = 0.0;
        for (std::size_t m = 0; m < 3U; ++m) {
            sum += corner.interpolation_weight[m] * s[corner.cell[m].to_index()];
        }
        corner_s[index] = sum;
    }
    // v̄_k at the zonal model's boundaries, with zero at the poles.
    std::vector<double> boundary_deg{-90.0};
    boundary_deg.insert(boundary_deg.end(), zonal.boundary_latitude_deg.begin(),
                        zonal.boundary_latitude_deg.end());
    boundary_deg.push_back(90.0);
    const std::size_t interior = zonal.bands - 1U;
    std::vector<double> northward((interior + 2U) * n, 0.0);   // layer-major
    for (std::size_t k = 0; k < n; ++k) {
        for (std::size_t i = 0; i < interior; ++i) {
            northward[k * (interior + 2U) + i + 1U] = zonal.at_boundary(zonal.northward_m_s, k, i);
        }
    }
    result.mass_flux_kg_m_s.assign(n * edges, 0.0);
    result.azonal_mass_flux_kg_m_s.assign(n * edges, 0.0);
    for_each_deterministic_block(
        grid_.edge_blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t e = block.begin; e < block.end; ++e) {
                const auto& edge = coarse.edge(edge_id(e));
                const auto& c_edge = grid_.edges()[e];
                const std::size_t a = edge.first_cell.to_index();
                const std::size_t b = edge.second_cell.to_index();
                const double mu = edge_mass[e];
                const double f = edge_f[e];
                const double sine = std::clamp(c_edge.midpoint_unit.z, -1.0, 1.0);
                const double cosine = std::sqrt(std::max(0.0, 1.0 - sine * sine));
                const double latitude_deg = std::asin(sine) * 180.0 / pi;
                // n_e · ê_north = n_z / cos φ (n_e is tangent to the sphere).
                const double northward_share =
                    cosine > 1.0e-12 ? c_edge.normal_unit.z / cosine : 0.0;
                const double ds_n = (s[b] - s[a]) / edge.centroid_distance_m;
                const double ds_t =
                    (corner_s[c_edge.vertex[1]] - corner_s[c_edge.vertex[0]]) / edge.length_m;
                for (std::size_t k = 0; k < n; ++k) {
                    const double t_edge =
                        0.5 * (temperature[k * groups + a] + temperature[k * groups + b]);
                    const double r = layer_rate[k * edges + e];
                    const double gn = -(departure[k * groups + b] - departure[k * groups + a]) /
                                          edge.centroid_distance_m -
                                      gas * t_edge * ds_n;
                    const double gt = -(corner_departure[k * corners + c_edge.vertex[1]] -
                                        corner_departure[k * corners + c_edge.vertex[0]]) /
                                          edge.length_m -
                                      gas * t_edge * ds_t;
                    const double azonal = mu * (r * gn + f * gt) / (r * r + f * f);
                    const double v_bar = interpolate_latitude(
                        boundary_deg,
                        std::span<const double>(northward).subspan(k * (interior + 2U),
                                                                   interior + 2U),
                        latitude_deg);
                    result.azonal_mass_flux_kg_m_s[k * edges + e] = azonal;
                    result.mass_flux_kg_m_s[k * edges + e] =
                        azonal + mu * v_bar * northward_share;
                }
            }
        });

    // 7. Continuity, cell by cell: W_{k+1} = W_k − div F_k.
    result.vertical_mass_flux_kg_m2_s.assign((n + 1U) * groups, 0.0);
    const auto [largest_layer, largest_column] = reduce_deterministic_blocks<std::pair<double, double>>(
        std::span<const CellBlock>(blocks), worker_count, std::pair<double, double>{0.0, 0.0},
        [&](std::size_t, const CellBlock& block) {
            std::pair<double, double> extremes{0.0, 0.0};
            for (std::size_t c = block.begin; c < block.end; ++c) {
                const CellId id{static_cast<CellId::value_type>(c)};
                double w = 0.0;
                for (std::size_t k = 0; k < n; ++k) {
                    double net = 0.0;
                    for (const auto& cell_edge : coarse.cell_edges(id)) {
                        const auto& edge = coarse.edge(cell_edge.edge);
                        const double out = edge.first_cell.to_index() == c ? 1.0 : -1.0;
                        net += out * edge.length_m *
                               result.mass_flux_kg_m_s[k * edges + cell_edge.edge.to_index()];
                    }
                    const double divergence = net / graph.area_m2[c];
                    extremes.first = std::max(extremes.first, std::abs(divergence));
                    w -= divergence;
                    result.vertical_mass_flux_kg_m2_s[(k + 1U) * groups + c] = w;
                }
                extremes.second = std::max(extremes.second, std::abs(w));
            }
            return extremes;
        },
        [](std::pair<double, double> x, std::pair<double, double> y) {
            return std::pair<double, double>{std::max(x.first, y.first),
                                             std::max(x.second, y.second)};
        });
    result.relative_column_divergence =
        largest_layer > 0.0 ? largest_column / largest_layer : 0.0;

    // 8. The balanced surface pressure: the zonal profile times exp(s), the
    // groups' mass held.
    result.surface_pressure_Pa.resize(groups);
    double mass_before = 0.0;
    double mass_after = 0.0;
    for (std::size_t c = 0; c < groups; ++c) {
        const double zonal_ps =
            interpolate_latitude(zonal.latitude_deg, zonal.surface_pressure_Pa,
                                 latitude_rad_[c] * 180.0 / pi);
        result.surface_pressure_Pa[c] = zonal_ps * std::exp(s[c]);
        mass_before += graph.area_m2[c] * ps[c];
        mass_after += graph.area_m2[c] * result.surface_pressure_Pa[c];
    }
    for (double& value : result.surface_pressure_Pa) {
        value *= mass_before / mass_after;
    }
    return result;
}

}  // namespace planetsim
