#include "sim/planet/dynamics/atmosphere_dynamics.hpp"

#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/planet/dynamics/orography.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace planetsim {
namespace {

[[nodiscard]] Field2D<double> dynamics_height(const PlanetMesh& mesh, const SlowState& slow,
                                              const SurfaceFractions& fractions,
                                              double max_step_m, std::size_t worker_count,
                                              std::size_t& passes) {
    Field2D<double> height;
    compute_surface_height(mesh, slow, fractions, height);
    auto limited = limit_dynamics_orography_steps(mesh, height, max_step_m,
                                                  dynamics_orography_max_passes, worker_count);
    passes = limited.passes;
    return std::move(limited.height_m);
}

}  // namespace

PrimitiveEquationParameters AtmosphereDynamics::model_parameters(
    const PlanetMesh& mesh, const PlanetParameters& planet,
    const AtmosphereParameters& atmosphere, const AtmosphereDynamicsParameters& parameters) {
    if (atmosphere.layer_count == 0U) {
        throw std::invalid_argument("winds need an atmosphere");
    }
    PrimitiveEquationParameters result;
    result.layer_count = atmosphere.layer_count;
    result.gravity_m_s2 = surface_gravity_m_s2(planet);
    result.rotation_rate_rad_s =
        2.0 * std::numbers::pi_v<double> / planet.sidereal_rotation_period_s;
    result.gas_constant_J_kg_K = dry_air_gas_constant_J_kg_K;
    result.heat_capacity_J_kg_K = dry_air_heat_capacity_J_kg_K;
    result.hyperviscosity_m4_s =
        hyperviscosity_for_damping_time(mesh, parameters.hyperviscosity_damping_s);
    return result;
}

AtmosphereDynamics::AtmosphereDynamics(const PlanetMesh& mesh, const SlowState& slow,
                                       const PlanetParameters& planet,
                                       const AtmosphereParameters& atmosphere,
                                       const SurfaceFractions& fractions,
                                       AtmosphereDynamicsParameters parameters,
                                       std::size_t worker_count)
    : mesh_(&mesh), parameters_(parameters), worker_count_(worker_count),
      grid_(CGridGeometry::build(mesh)),
      model_(mesh, grid_, model_parameters(mesh, planet, atmosphere, parameters_),
             dynamics_height(mesh, slow, fractions, parameters_.orography_max_step_m,
                             worker_count, orography_passes_)) {
    if (slow.atmosphere_layer_count() != atmosphere.layer_count) {
        throw std::invalid_argument("the state's atmosphere does not have the scenario's layers");
    }
    // Drag coefficient per edge: the two cells' land/ocean mix.
    std::vector<double> drag(mesh.edge_count());
    for (std::size_t e = 0; e < mesh.edge_count(); ++e) {
        const auto& edge = mesh.edge(EdgeId{static_cast<EdgeId::value_type>(e)});
        const double land = 0.5 * (static_cast<double>(fractions.land_fraction[edge.first_cell]) +
                                   static_cast<double>(fractions.land_fraction[edge.second_cell]));
        drag[e] = land * parameters_.land_drag_coefficient +
                  (1.0 - land) * parameters_.ocean_drag_coefficient;
    }
    model_.set_surface_drag(std::move(drag));
}

PrimitiveEquationState AtmosphereDynamics::model_state(const PlanetState& state) const {
    auto result = model_.state_at_rest(state.slow().atmosphere_surface_pressure_Pa,
                                       state.slow().atmosphere_temperature_K);
    if (state.has_fast_state()) {
        result.normal_velocity_m_s = state.fast_state()->atmosphere_edge_normal_wind_m_s;
    }
    return result;
}

void AtmosphereDynamics::step(PlanetState& state, double dt_s) {
    const std::size_t n = model_.layer_count();
    if (!state.has_fast_state()) {
        state.open_fast_state().atmosphere_edge_normal_wind_m_s =
            Field3D<double>(n, mesh_->edge_count(), 0.0);
    }
    auto& winds = state.fast_state()->atmosphere_edge_normal_wind_m_s;
    if (winds.layer_count() != n || winds.cell_count() != mesh_->edge_count()) {
        winds = Field3D<double>(n, mesh_->edge_count(), 0.0);
    }
    auto pe = model_state(state);
    const double before = model_.diagnose(pe, worker_count_).energy_J();
    last_.steps = model_.advance(pe, dt_s, parameters_.rule, worker_count_);
    const auto after = model_.diagnose(pe, worker_count_);
    last_.kinetic_energy_J = after.kinetic_energy_J;
    last_.max_wind_m_s = after.max_wind_m_s;
    last_.energy_change_J = after.energy_J() - before;

    state.slow().atmosphere_surface_pressure_Pa = pe.surface_pressure_Pa;
    model_.temperatures(pe, state.slow().atmosphere_temperature_K, worker_count_);
    winds = std::move(pe.normal_velocity_m_s);
}

void register_atmosphere_dynamics(Scheduler& scheduler, PlanetState& state,
                                  AtmosphereDynamics& dynamics) {
    scheduler.register_process({"atmosphere_dynamics_reference", SimulationMode::reference, 0},
                               [&state, &dynamics](const StepContext& context) {
                                   dynamics.step(state, simulation_time_s(context.length_ticks()));
                               });
    scheduler.register_process({"atmosphere_dynamics_release", SimulationMode::climate, 0},
                               [&state](const StepContext&) { state.release_fast_state(); });
}

}  // namespace planetsim
