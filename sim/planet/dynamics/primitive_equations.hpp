#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/core/math/vec3d.hpp"
#include "sim/planet/dynamics/shallow_water.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
#include "sim/planet/operators/c_grid.hpp"

#include <cstddef>
#include <vector>

namespace planetsim {

// The hydrostatic primitive equations on N equal-mass σ layers and the
// C-grid (ADR-0011 §4.3, task M6-03). Prognostic: surface pressure p_s,
// Θ_k = μ_k θ_k (μ_k = p_s / (g N), θ potential temperature) and the edge
// velocities u_k; layer 0 is the bottom one. The vertical discretisation is
// the energy-conserving form of task M6-03 §1: layer-mean Exner π̄_k,
// geopotential at the mean-Exner level Φ̄_k, Exner-weighted interface θ̃,
// so that with TRiSK in the horizontal the total energy
//   E = Σ A [Σ_k (μ_k K_k + c_p Θ_k π̄_k) + p_s Φ_s / g]
// is conserved by the semi-discrete equations. Dissipated kinetic energy
// returns as heat.

struct HeldSuarezForcing {
    bool enabled = false;
    double relaxation_free_s = 40.0 * 86'400.0;     // 1 / k_a
    double relaxation_surface_s = 4.0 * 86'400.0;   // 1 / k_s
    double friction_s = 86'400.0;                   // 1 / k_f
    double boundary_layer_sigma = 0.7;              // σ_b
    double equator_pole_K = 60.0;                   // ΔT_y
    double static_stability_K = 10.0;               // Δθ_z
};

struct PrimitiveEquationParameters {
    std::size_t layer_count = 3;
    double gravity_m_s2 = 9.80616;
    double rotation_rate_rad_s = 7.292e-5;
    Vec3d rotation_axis{0.0, 0.0, 1.0};
    double gas_constant_J_kg_K = 287.04;
    double heat_capacity_J_kg_K = 1004.64;
    double reference_pressure_Pa = 100'000.0;   // p₀ of θ
    double hyperviscosity_m4_s = 0.0;
    HeldSuarezForcing held_suarez;
};

struct PrimitiveEquationState {
    Field2D<double> surface_pressure_Pa;   // p_s, at cells
    Field3D<double> mass_theta;            // Θ_k = μ_k θ_k, layer × cell (kg K / m²)
    Field3D<double> normal_velocity_m_s;   // u_k, layer × edge
    // Q_k = μ_k q_k, the layers' vapour (kg/m², layer × cell; ADR-0021
    // §4.5): a passive tracer in flux form, carried upwind by the same edge
    // mass fluxes F_k and vertical fluxes W as Θ, so the column's water is
    // conserved. Empty: no tracer.
    Field3D<double> mass_humidity;
};

struct PrimitiveEquationDiagnostics {
    double mass_kg = 0.0;            // Σ A p_s / g
    double kinetic_energy_J = 0.0;   // Σ A Σ_k μ_k K_k
    double enthalpy_J = 0.0;         // Σ A Σ_k c_p Θ_k π̄_k
    double surface_potential_J = 0.0;   // Σ A p_s Φ_s / g
    double max_wind_m_s = 0.0;

    [[nodiscard]] double energy_J() const noexcept {
        return kinetic_energy_J + enthalpy_J + surface_potential_J;
    }
};

class PrimitiveEquationModel {
  public:
    // `surface_height_m` is z_s at the cells (Φ_s = g z_s). The mesh and
    // grid must outlive the model.
    PrimitiveEquationModel(const PlanetMesh& mesh, const CGridGeometry& grid,
                           PrimitiveEquationParameters parameters,
                           Field2D<double> surface_height_m);

    [[nodiscard]] const PrimitiveEquationParameters& parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] std::size_t layer_count() const noexcept { return parameters_.layer_count; }

    // Bulk surface drag on the bottom layer, τ = ρ C_D |V| V with the
    // bottom layer's wind and density ρ = p_s / (R T_0), per edge drag
    // coefficient C_D ≥ 0 (empty: no drag). The acceleration
    // −g τ / (p_s / N) = −C_D |V| u g N / (R T_0) is independent of p_s.
    void set_surface_drag(std::vector<double> drag_coefficient);

    // A state at rest with the given p_s and layer temperatures T_k
    // (layer × cell).
    [[nodiscard]] PrimitiveEquationState state_at_rest(const Field2D<double>& surface_pressure_Pa,
                                                       const Field3D<double>& temperature_K) const;

    // T_k = θ_k π̄_k, the layers' mass-mean temperatures.
    void temperatures(const PrimitiveEquationState& state, Field3D<double>& temperature_K,
                      std::size_t worker_count = 1U) const;

    void tendency(const PrimitiveEquationState& state, PrimitiveEquationState& rate,
                  std::size_t worker_count = 1U) const;

    void step(PrimitiveEquationState& state, double dt_s, std::size_t worker_count = 1U) const;

    // substep_count(span, rule) RK3 steps; throws std::runtime_error if the
    // wind exceeds rule.max_wind_m_s. Returns the number of steps.
    std::size_t advance(PrimitiveEquationState& state, double span_s, const SubstepRule& rule,
                        std::size_t worker_count = 1U) const;

    [[nodiscard]] PrimitiveEquationDiagnostics diagnose(const PrimitiveEquationState& state,
                                                        std::size_t worker_count = 1U) const;

    // dE/dt from the tendencies, Σ δE/δx · ∂x/∂t, and the sum of the
    // magnitudes of its terms: the semi-discrete energy identity.
    struct EnergyRate {
        double rate_W = 0.0;
        double gross_W = 0.0;
    };
    [[nodiscard]] EnergyRate energy_rate(const PrimitiveEquationState& state,
                                         const PrimitiveEquationState& rate) const;

  private:
    void column_structure(const PrimitiveEquationState& state, std::vector<double>& theta,
                          std::vector<double>& exner_mean, std::vector<double>& exner_interface,
                          std::vector<double>& geopotential_mean, std::size_t worker_count) const;

    const PlanetMesh* mesh_;
    const CGridGeometry* grid_;
    PrimitiveEquationParameters parameters_;
    Field2D<double> surface_geopotential_;   // Φ_s = g z_s
    Field2D<double> f_corner_;
    Field2D<double> latitude_rad_;
    std::vector<double> drag_coefficient_;   // per edge; empty: no drag
};

}  // namespace planetsim
