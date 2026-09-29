#pragma once

#include "sim/core/scheduler/orbital_calendar.hpp"

#include <array>
#include <cstddef>

namespace planetsim {

class PlanetState;
struct PlanetParameters;

// Climate-mode forcing of ADR-0006 §4.3: the time mean of top-of-atmosphere
// insolation over one orbital sub-step. The diurnal cycle is averaged
// analytically; the motion along the orbit by fixed Gauss-Legendre
// quadrature in time. The instantaneous M1 forcing is unchanged and remains
// the reference-mode forcing.

inline constexpr std::size_t substep_forcing_quadrature_nodes = 16;

// Daily-mean insolation at latitude φ for a fixed declination δ and incident
// flux S (the standard sunrise-hour-angle result):
//   Q = (S/π)(h0 sinφ sinδ + cosφ cosδ sin h0),  cos h0 = −tanφ tanδ,
// with h0 = π in polar day and 0 in polar night. The component form takes the
// sines and cosines directly so the poles are exact.
[[nodiscard]] double daily_mean_insolation_W_m2(double sin_latitude, double cos_latitude,
                                                double sin_declination, double cos_declination,
                                                double incident_flux_W_m2) noexcept;
[[nodiscard]] double daily_mean_insolation_W_m2(double latitude_rad, double declination_rad,
                                                double incident_flux_W_m2) noexcept;

// One quadrature node of a sub-step: its time, weight (the node weights sum
// to 1) and the orbit there.
struct SubstepForcingNode {
    double time_s = 0.0;
    double weight = 0.0;
    double sin_declination = 0.0;
    double cos_declination = 1.0;
    double incident_flux_W_m2 = 0.0;
};

// The synodic (solar) day, 1 / (1/T_sidereal − 1/P). Throws
// std::domain_error for a non-prograde or tidally locked rotation.
[[nodiscard]] double synodic_day_s(const PlanetParameters& parameters);

// The quadrature nodes over [begin_tick, end_tick). Throws std::domain_error
// when the synodic day is not shorter than a tenth of a sub-step (ADR-0006
// §6), where the analytic diurnal mean does not apply.
[[nodiscard]] std::array<SubstepForcingNode, substep_forcing_quadrature_nodes>
substep_forcing_nodes(const PlanetParameters& parameters, const ClimateSubstep& substep);

// Fills forcing().substep_mean_insolation_W_m2 for the given sub-step,
// accumulating in double and storing float. Bit-identical for any worker
// count.
void update_substep_mean_insolation(PlanetState& state, const PlanetParameters& parameters,
                                    const ClimateSubstep& substep,
                                    std::size_t worker_count = 1U);

}  // namespace planetsim
