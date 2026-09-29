#include "sim/planet/orbit/substep_forcing.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/orbit/orbit_state.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace planetsim {
namespace {

// Gauss-Legendre nodes on [-1, 1] and their weights, for sixteen points.
// Eight nodes left 0.048 W/m² of quadrature error at high latitudes in the
// sub-steps where a cell crosses between polar night and day (the integrand
// has a kink in time there); sixteen bring it to 0.012 W/m² (ADR-0006 §9).
constexpr std::array<double, substep_forcing_quadrature_nodes> gauss_legendre_nodes{
    -0.98940093499164994, -0.9445750230732326,   -0.86563120238783176, -0.755404408355003,
    -0.61787624440264377, -0.45801677765722737,  -0.28160355077925892, -0.095012509837637454,
    0.095012509837637454, 0.28160355077925892,   0.45801677765722737,  0.61787624440264377,
    0.755404408355003,    0.86563120238783176,   0.9445750230732326,   0.98940093499164994,
};
constexpr std::array<double, substep_forcing_quadrature_nodes> gauss_legendre_weights{
    0.027152459411754037, 0.062253523938647706, 0.095158511682492591, 0.12462897125553403,
    0.14959598881657676,  0.16915651939500262,  0.18260341504492361,  0.18945061045506859,
    0.18945061045506859,  0.18260341504492361,  0.16915651939500262,  0.14959598881657676,
    0.12462897125553403,  0.095158511682492591, 0.062253523938647706, 0.027152459411754037,
};

}  // namespace

double daily_mean_insolation_W_m2(double sin_latitude, double cos_latitude,
                                  double sin_declination, double cos_declination,
                                  double incident_flux_W_m2) noexcept {
    const double a = sin_latitude * sin_declination;
    const double b = cos_latitude * cos_declination;
    double hour_angle_rad = 0.0;
    if (b <= 0.0) {
        // At a pole (or with the sun at a pole) the sun is up all day or down
        // all day, depending on the sign of a.
        hour_angle_rad = a > 0.0 ? std::numbers::pi_v<double> : 0.0;
    } else {
        const double cosine = -a / b;
        if (cosine <= -1.0) {
            hour_angle_rad = std::numbers::pi_v<double>;   // polar day
        } else if (cosine < 1.0) {
            hour_angle_rad = std::acos(cosine);
        }                                                  // else polar night: 0
    }
    const double value = incident_flux_W_m2 / std::numbers::pi_v<double> *
                         (hour_angle_rad * a + b * std::sin(hour_angle_rad));
    return std::max(0.0, value);
}

double daily_mean_insolation_W_m2(double latitude_rad, double declination_rad,
                                  double incident_flux_W_m2) noexcept {
    return daily_mean_insolation_W_m2(std::sin(latitude_rad), std::cos(latitude_rad),
                                      std::sin(declination_rad), std::cos(declination_rad),
                                      incident_flux_W_m2);
}

double synodic_day_s(const PlanetParameters& parameters) {
    const double rotation_rate = 1.0 / parameters.sidereal_rotation_period_s;
    const double orbital_rate = 1.0 / parameters.orbital_period_s;
    if (!(rotation_rate > orbital_rate)) {
        throw std::domain_error(
            "the analytic diurnal mean needs a prograde rotation faster than the orbit");
    }
    return 1.0 / (rotation_rate - orbital_rate);
}

std::array<SubstepForcingNode, substep_forcing_quadrature_nodes>
substep_forcing_nodes(const PlanetParameters& parameters, const ClimateSubstep& substep) {
    const double nominal_substep_s =
        parameters.orbital_period_s / static_cast<double>(climate_substeps_per_year);
    if (!(synodic_day_s(parameters) < 0.1 * nominal_substep_s)) {
        throw std::domain_error(
            "the synodic day must be shorter than a tenth of a climate sub-step (ADR-0006 §6)");
    }
    if (substep.end_tick <= substep.begin_tick) {
        throw std::invalid_argument("climate sub-step must span at least one tick");
    }
    const double begin_s = simulation_time_s(substep.begin_tick);
    const double span_s = simulation_time_s(substep.end_tick) - begin_s;

    std::array<SubstepForcingNode, substep_forcing_quadrature_nodes> nodes{};
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        auto& node = nodes[index];
        node.time_s = begin_s + 0.5 * span_s * (1.0 + gauss_legendre_nodes[index]);
        node.weight = 0.5 * gauss_legendre_weights[index];
        const OrbitState orbit = evaluate_orbit(parameters, node.time_s);
        node.sin_declination = std::sin(orbit.solar_declination_rad);
        node.cos_declination = std::cos(orbit.solar_declination_rad);
        node.incident_flux_W_m2 = orbit.incident_solar_flux_W_m2;
    }
    return nodes;
}

void update_substep_mean_insolation(PlanetState& state, const PlanetParameters& parameters,
                                    const ClimateSubstep& substep, std::size_t worker_count) {
    const auto nodes = substep_forcing_nodes(parameters, substep);
    const PlanetMesh& mesh = state.mesh();
    auto& field = state.forcing().substep_mean_insolation_W_m2;
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
            for (std::size_t index = block.begin; index < block.end; ++index) {
                const auto& cell = mesh.cells()[index];
                const Vec3d& up = cell.center_unit;
                const double sin_latitude = up.z;
                const double cos_latitude = std::sqrt(up.x * up.x + up.y * up.y);
                double mean = 0.0;
                for (const auto& node : nodes) {
                    mean += node.weight *
                            daily_mean_insolation_W_m2(sin_latitude, cos_latitude,
                                                       node.sin_declination,
                                                       node.cos_declination,
                                                       node.incident_flux_W_m2);
                }
                field[cell.id] = static_cast<float>(mean);
            }
        });
}

}  // namespace planetsim
