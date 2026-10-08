#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/surface/heat_transport.hpp"

#include <cstddef>
#include <vector>

namespace planetsim {

// The balanced surface pressure written into the slow state, with the
// energy of the air it moves (ADR-0011 §17.3). The change of each coarse
// group's mass, δM_g = Σ A (p_s' − p_s)/g over its cells, is produced by a
// divergent mass flux −∇χ between the groups (the transport's graph, ADR-0009
// §12): one Poisson solve, L χ = −δM with the two-point Laplacian
// (multigrid-preconditioned CG). Each of the N equal-mass σ layers moves
// 1/N of it and carries its group's mass-weighted dry static energy
// s_l = c_p T_l + Φ_l upwind. Each group's layer energy, Σ A m_l (c_p T_l +
// Φ_s), is then the old one plus what it received, and its cells share it
// at one specific-energy shift per layer: T_l' = T_l + δ_{g,l} / c_p with the
// new masses. The atmosphere's mass is the new p_s's; its energy
// Σ (c_p T m + Φ_s M) closes to rounding and the fluxes sum to zero. Moving
// air between columns does work: a column that gains mass warms by
// compression, one that loses it cools. Air moved within a group is not
// resolved (its cells keep their differences). Bit-identical for any worker
// count.
struct PressureRedistributionResult {
    int cg_iterations = 0;
    double relative_residual = 0.0;   // ‖L χ + A δM‖ / ‖A δM‖
    double max_change_Pa = 0.0;       // max |p_s' − p_s|
    double max_temperature_change_K = 0.0;
    // Σ A c_p Σ_l T_l m_l and Σ A Φ_s M: their changes, and the change of
    // their sum (zero to rounding).
    double enthalpy_change_J = 0.0;
    double potential_change_J = 0.0;
    double energy_change_J = 0.0;
    double energy_J = 0.0;            // Σ A (c_p Σ T m + Φ_s M) after, for scale
};

class PressureRedistribution {
  public:
    // `graph` and `group_of_cell`: the coarse groups (agglomerated_transport_graph);
    // `surface_height_m`: the cells' true heights (the column physics' Φ_s).
    PressureRedistribution(const PlanetMesh& mesh, const TransportGraph& graph,
                           const std::vector<std::size_t>& group_of_cell,
                           const Field2D<double>& surface_height_m, double gravity_m_s2,
                           double gas_constant_J_kg_K, double heat_capacity_J_kg_K);
    PressureRedistribution(const PressureRedistribution&) = delete;
    PressureRedistribution& operator=(const PressureRedistribution&) = delete;

    // Replaces slow.atmosphere_surface_pressure_Pa with `surface_pressure_Pa`
    // and sets the layer temperatures as above.
    PressureRedistributionResult apply(SlowState& slow, const Field2D<double>& surface_pressure_Pa,
                                       std::size_t worker_count = 1U) const;

  private:
    const PlanetMesh* mesh_;
    const TransportGraph* graph_;
    std::vector<std::size_t> member_offset_;   // each group's cells, in cell order
    std::vector<std::size_t> members_;
    GraphMultigrid multigrid_;
    std::vector<double> diagonal_;
    std::vector<double> off_;
    Field2D<double> surface_geopotential_m2_s2_;
    double gravity_m_s2_;
    double gas_constant_J_kg_K_;
    double heat_capacity_J_kg_K_;
};

}  // namespace planetsim
