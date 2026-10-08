#include "sim/planet/dynamics/pressure_redistribution.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/atmosphere/atmosphere.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
#include <vector>

namespace planetsim {
namespace {

constexpr std::size_t max_layers = 64U;

}  // namespace

PressureRedistribution::PressureRedistribution(const PlanetMesh& mesh, const TransportGraph& graph,
                                               const std::vector<std::size_t>& group_of_cell,
                                               const Field2D<double>& surface_height_m,
                                               double gravity_m_s2, double gas_constant_J_kg_K,
                                               double heat_capacity_J_kg_K)
    : mesh_(&mesh), graph_(&graph), multigrid_(graph),
      surface_geopotential_m2_s2_(mesh.cell_count(), 0.0), gravity_m_s2_(gravity_m_s2),
      gas_constant_J_kg_K_(gas_constant_J_kg_K), heat_capacity_J_kg_K_(heat_capacity_J_kg_K) {
    if (surface_height_m.size() != mesh.cell_count() ||
        group_of_cell.size() != mesh.cell_count() || !(gravity_m_s2 > 0.0)) {
        throw std::invalid_argument("invalid pressure redistribution inputs");
    }
    for (std::size_t i = 0; i < mesh.cell_count(); ++i) {
        surface_geopotential_m2_s2_[i] = gravity_m_s2 * surface_height_m[i];
    }
    const std::size_t groups = graph.size();
    member_offset_.assign(groups + 1U, 0U);
    for (const std::size_t g : group_of_cell) {
        ++member_offset_[g + 1U];
    }
    for (std::size_t g = 0; g < groups; ++g) {
        member_offset_[g + 1U] += member_offset_[g];
    }
    members_.resize(mesh.cell_count());
    std::vector<std::size_t> next(member_offset_.begin(), member_offset_.end() - 1);
    for (std::size_t i = 0; i < mesh.cell_count(); ++i) {
        members_[next[group_of_cell[i]]++] = i;
    }
    // The two-point Laplacian on the groups, its constant mode regularised
    // for the preconditioner (the right-hand side has zero sum).
    diagonal_.resize(groups);
    off_.resize(graph.neighbour.size());
    for (std::size_t a = 0; a < groups; ++a) {
        diagonal_[a] = graph.weight_sum[a] * (1.0 + 1.0e-10);
        for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
            off_[k] = -graph.weight[k];
        }
    }
    multigrid_.set_matrix(diagonal_, off_);
}

PressureRedistributionResult PressureRedistribution::apply(
    SlowState& slow, const Field2D<double>& surface_pressure_Pa, std::size_t worker_count) const {
    const PlanetMesh& mesh = *mesh_;
    const TransportGraph& graph = *graph_;
    const std::size_t cells = mesh.cell_count();
    const std::size_t groups = graph.size();
    const std::size_t layers = slow.atmosphere_layer_count();
    if (surface_pressure_Pa.size() != cells || layers == 0U || layers > max_layers) {
        throw std::invalid_argument("pressure redistribution: the state does not match");
    }
    const double g = gravity_m_s2_;
    const double cp = heat_capacity_J_kg_K_;
    const double n_d = static_cast<double>(layers);
    const auto blocks = std::span<const CellBlock>(graph.blocks);
    const auto dot = [&](const std::vector<double>& u, const std::vector<double>& v) {
        return reduce_deterministic_blocks<double>(
            blocks, worker_count, 0.0,
            [&](std::size_t, const CellBlock& block) {
                double sum = 0.0;
                for (std::size_t i = block.begin; i < block.end; ++i) {
                    sum += u[i] * v[i];
                }
                return sum;
            },
            [](double p, double q) { return p + q; });
    };
    const auto apply_laplacian = [&](const std::vector<double>& x, std::vector<double>& y) {
        for_each_deterministic_block(blocks, worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t a = block.begin; a < block.end; ++a) {
                double sum = graph.weight_sum[a] * x[a];
                for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
                    sum -= graph.weight[k] * x[graph.neighbour[k]];
                }
                y[a] = sum;
            }
        });
    };

    PressureRedistributionResult result;
    // 1. Per group: the layers' mass-weighted dry static energy, the layer
    // energy Σ A m (c_p T + Φ_s) (J), and L χ = −δM (kg).
    std::vector<double> energy(layers * groups, 0.0);
    std::vector<double> group_energy(layers * groups, 0.0);
    std::vector<double> rhs(groups, 0.0);
    for_each_deterministic_block(blocks, worker_count, [&](std::size_t, const CellBlock& block) {
        std::array<double, max_layers> t{};
        std::array<double, max_layers> s{};
        for (std::size_t c = block.begin; c < block.end; ++c) {
            double old_mass = 0.0;
            double change = 0.0;
            for (std::size_t q = member_offset_[c]; q < member_offset_[c + 1U]; ++q) {
                const std::size_t i = members_[q];
                const double area = mesh.cells()[i].area_m2;
                const double ps = slow.atmosphere_surface_pressure_Pa[i];
                const double m = area * ps / (g * n_d);
                old_mass += m;
                change += area * (surface_pressure_Pa[i] - ps) / g;
                for (std::size_t l = 0; l < layers; ++l) {
                    t[l] = slow.atmosphere_temperature_K.layer(l)[i];
                }
                layer_dry_static_energy(surface_geopotential_m2_s2_[i],
                                        std::span<const double>(t.data(), layers),
                                        std::span<double>(s.data(), layers), gas_constant_J_kg_K_,
                                        cp);
                for (std::size_t l = 0; l < layers; ++l) {
                    energy[l * groups + c] += m * s[l];
                    group_energy[l * groups + c] +=
                        m * (cp * t[l] + surface_geopotential_m2_s2_[i]);
                }
            }
            for (std::size_t l = 0; l < layers; ++l) {
                energy[l * groups + c] /= old_mass;
            }
            rhs[c] = -change;
        }
    });
    for (std::size_t i = 0; i < cells; ++i) {
        result.max_change_Pa =
            std::max(result.max_change_Pa,
                     std::abs(surface_pressure_Pa[i] - slow.atmosphere_surface_pressure_Pa[i]));
    }
    double mean = 0.0;
    for (const double value : rhs) {
        mean += value;
    }
    mean /= static_cast<double>(groups);
    for (double& value : rhs) {
        value -= mean;   // rounding of a mass-holding p_s'
    }
    std::vector<double> chi(groups, 0.0);
    {
        // Preconditioned conjugate gradients. Energy closes whatever the
        // residual; it only sets how exactly each group's temperature follows
        // its own mass change (an error of about the tolerance times ΔT).
        std::vector<double> r = rhs;
        std::vector<double> z(groups, 0.0);
        std::vector<double> p(groups, 0.0);
        std::vector<double> q(groups, 0.0);
        const double b_norm = std::sqrt(dot(rhs, rhs));
        if (b_norm > 0.0) {
            multigrid_.precondition(r, z, worker_count);
            p = z;
            double rz = dot(r, z);
            constexpr int max_iterations = 500;
            constexpr double tolerance = 1.0e-6;
            for (int iteration = 0; iteration < max_iterations; ++iteration) {
                apply_laplacian(p, q);
                const double alpha = rz / dot(p, q);
                for (std::size_t i = 0; i < groups; ++i) {
                    chi[i] += alpha * p[i];
                    r[i] -= alpha * q[i];
                }
                ++result.cg_iterations;
                if (std::sqrt(dot(r, r)) <= tolerance * b_norm) {
                    break;
                }
                multigrid_.precondition(r, z, worker_count);
                const double rz_next = dot(r, z);
                const double beta = rz_next / rz;
                rz = rz_next;
                for (std::size_t i = 0; i < groups; ++i) {
                    p[i] = z[i] + beta * p[i];
                }
            }
            result.relative_residual = std::sqrt(dot(r, r)) / b_norm;
        }
    }

    // 2. Each group's layer energy after the move, shared among its cells at
    // one specific-energy shift per layer with the new masses.
    Field3D<double> temperature(layers, cells, 0.0);
    struct Sums {
        double enthalpy_before = 0.0;
        double enthalpy_after = 0.0;
        double potential_before = 0.0;
        double potential_after = 0.0;
        double max_change_K = 0.0;
    };
    const Sums sums = reduce_deterministic_blocks<Sums>(
        blocks, worker_count, Sums{},
        [&](std::size_t, const CellBlock& block) {
            Sums partial;
            for (std::size_t a = block.begin; a < block.end; ++a) {
                for (std::size_t l = 0; l < layers; ++l) {
                    double target = group_energy[l * groups + a];   // J
                    for (std::size_t k = graph.offset[a]; k < graph.offset[a + 1U]; ++k) {
                        const std::size_t b = graph.neighbour[k];
                        const double moved = graph.weight[k] * (chi[a] - chi[b]) / n_d;   // kg
                        target -= moved > 0.0 ? moved * energy[l * groups + a]
                                              : moved * energy[l * groups + b];
                    }
                    // Σ m' (c_p T + Φ_s) at the old temperatures, and Σ m'.
                    double kept = 0.0;
                    double new_mass = 0.0;
                    for (std::size_t q = member_offset_[a]; q < member_offset_[a + 1U]; ++q) {
                        const std::size_t i = members_[q];
                        const double m =
                            mesh.cells()[i].area_m2 * surface_pressure_Pa[i] / (g * n_d);
                        kept += m * (cp * slow.atmosphere_temperature_K.layer(l)[i] +
                                     surface_geopotential_m2_s2_[i]);
                        new_mass += m;
                    }
                    const double shift = (target - kept) / new_mass;   // J/kg
                    for (std::size_t q = member_offset_[a]; q < member_offset_[a + 1U]; ++q) {
                        const std::size_t i = members_[q];
                        const double area = mesh.cells()[i].area_m2;
                        const double phi_s = surface_geopotential_m2_s2_[i];
                        const double t_old = slow.atmosphere_temperature_K.layer(l)[i];
                        const double t_new = t_old + shift / cp;
                        const double old_m =
                            area * slow.atmosphere_surface_pressure_Pa[i] / (g * n_d);
                        const double new_m = area * surface_pressure_Pa[i] / (g * n_d);
                        temperature.layer(l)[i] = t_new;
                        partial.enthalpy_before += cp * t_old * old_m;
                        partial.enthalpy_after += cp * t_new * new_m;
                        partial.potential_before += phi_s * old_m;
                        partial.potential_after += phi_s * new_m;
                        partial.max_change_K =
                            std::max(partial.max_change_K, std::abs(t_new - t_old));
                    }
                }
            }
            return partial;
        },
        [](Sums x, const Sums& y) {
            x.enthalpy_before += y.enthalpy_before;
            x.enthalpy_after += y.enthalpy_after;
            x.potential_before += y.potential_before;
            x.potential_after += y.potential_after;
            x.max_change_K = std::max(x.max_change_K, y.max_change_K);
            return x;
        });
    result.enthalpy_change_J = sums.enthalpy_after - sums.enthalpy_before;
    result.potential_change_J = sums.potential_after - sums.potential_before;
    result.energy_change_J = result.enthalpy_change_J + result.potential_change_J;
    result.energy_J = sums.enthalpy_after + sums.potential_after;
    result.max_temperature_change_K = sums.max_change_K;
    slow.atmosphere_surface_pressure_Pa = surface_pressure_Pa;
    slow.atmosphere_temperature_K = std::move(temperature);
    return result;
}

}  // namespace planetsim
