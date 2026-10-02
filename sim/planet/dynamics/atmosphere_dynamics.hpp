#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/core/scheduler/scheduler.hpp"
#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/dynamics/primitive_equations.hpp"
#include "sim/planet/dynamics/shallow_water.hpp"
#include "sim/planet/operators/c_grid.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <cstddef>

namespace planetsim {

// The winds in reference mode (ADR-0011 §4.3, task M6-03 step B): the
// primitive-equation core on the planet's atmosphere. Each reference step
// reads p_s and the layer temperatures from the slow state and the edge
// winds from the fast state, advances the step, and writes them back. The
// column physics runs before it in the same step (process split).
struct AtmosphereDynamicsParameters {
    // ∇⁴ hyperviscosity: the grid-scale mode decays in this time (M6-02).
    double hyperviscosity_damping_s = 8.0 * 3600.0;
    // Bulk drag coefficients of the bottom layer's wind: neutral 10 m
    // values for the open ocean and for land of about 0.1 m roughness.
    double ocean_drag_coefficient = 1.5e-3;
    double land_drag_coefficient = 4.0e-3;
    double orography_max_step_m = 800.0;   // ADR-0011 §13
    SubstepRule rule;
};

struct AtmosphereDynamicsDiagnostics {
    std::size_t steps = 0;            // RK3 steps in the last reference step
    double kinetic_energy_J = 0.0;
    double max_wind_m_s = 0.0;
    double energy_change_J = 0.0;     // dynamics' change of total energy in the step
};

class AtmosphereDynamics {
  public:
    // Needs initialised surface temperatures and atmosphere (for the true
    // surface height and the layer count).
    AtmosphereDynamics(const PlanetMesh& mesh, const SlowState& slow,
                       const PlanetParameters& planet, const AtmosphereParameters& atmosphere,
                       const SurfaceFractions& fractions,
                       AtmosphereDynamicsParameters parameters = {},
                       std::size_t worker_count = 1U);
    AtmosphereDynamics(const AtmosphereDynamics&) = delete;
    AtmosphereDynamics& operator=(const AtmosphereDynamics&) = delete;
    AtmosphereDynamics(AtmosphereDynamics&&) = delete;
    AtmosphereDynamics& operator=(AtmosphereDynamics&&) = delete;

    // Advances the atmosphere by dt. Opens the fast state with the winds at
    // rest if it is closed (until M6-04 supplies the balanced start).
    void step(PlanetState& state, double dt_s);

    [[nodiscard]] const AtmosphereDynamicsDiagnostics& last() const noexcept { return last_; }
    [[nodiscard]] const PrimitiveEquationModel& model() const noexcept { return model_; }
    [[nodiscard]] const CGridGeometry& grid() const noexcept { return grid_; }
    [[nodiscard]] std::size_t orography_passes() const noexcept { return orography_passes_; }
    [[nodiscard]] PrimitiveEquationState model_state(const PlanetState& state) const;

  private:
    [[nodiscard]] static PrimitiveEquationParameters model_parameters(
        const PlanetMesh& mesh, const PlanetParameters& planet,
        const AtmosphereParameters& atmosphere, const AtmosphereDynamicsParameters& parameters);

    const PlanetMesh* mesh_;
    AtmosphereDynamicsParameters parameters_;
    std::size_t worker_count_;
    CGridGeometry grid_;
    std::size_t orography_passes_ = 0;
    PrimitiveEquationModel model_;
    AtmosphereDynamicsDiagnostics last_;
};

// Reference mode runs `dynamics` on every step after the processes already
// registered; climate steps release the fast state (ADR-0001 §4.1).
void register_atmosphere_dynamics(Scheduler& scheduler, PlanetState& state,
                                  AtmosphereDynamics& dynamics);

}  // namespace planetsim
