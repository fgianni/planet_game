#include "sim/planet/orbit/solar_diagnostics.hpp"

#include "sim/core/math/vec3d.hpp"
#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/planet_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace planetsim {
namespace {

struct SolarPartial {
    std::size_t illuminated_cell_count = 0;
    std::size_t night_cell_count = 0;
    std::size_t night_side_nonzero_count = 0;
    std::size_t out_of_range_count = 0;
    std::size_t non_finite_count = 0;
    double min_insolation_W_m2 = std::numeric_limits<double>::infinity();
    double max_insolation_W_m2 = -std::numeric_limits<double>::infinity();
    double total_area_m2 = 0.0;
    double total_incoming_power_W = 0.0;
};

void compensated_add(double value, double& sum, double& compensation) noexcept {
    const double corrected = value - compensation;
    const double next = sum + corrected;
    compensation = (next - sum) - corrected;
    sum = next;
}

}  // namespace

SolarDiagnostics analyze_solar_forcing(const PlanetState& state, std::size_t worker_count) {
    SolarDiagnostics result;
    result.cell_count = state.mesh().cell_count();

    const auto& mesh = state.mesh();
    const auto& forcing = state.forcing();
    std::vector<SolarPartial> partials(mesh.blocks().size());
    for_each_deterministic_block(
        mesh.blocks(), worker_count, [&](std::size_t block_index, const CellBlock& block) {
            auto& partial = partials[block_index];
            double area_compensation = 0.0;
            double power_compensation = 0.0;
            for (std::size_t cell_index = block.begin; cell_index < block.end; ++cell_index) {
                const auto& cell = mesh.cells()[cell_index];
                const double insolation =
                    static_cast<double>(forcing.top_of_atmosphere_insolation_W_m2[cell.id]);
                const double incidence =
                    dot(cell.center_unit, forcing.orbit.sun_direction_body_unit);
                compensated_add(cell.area_m2, partial.total_area_m2, area_compensation);

                if (!std::isfinite(insolation)) {
                    ++partial.non_finite_count;
                    continue;
                }
                partial.min_insolation_W_m2 = std::min(partial.min_insolation_W_m2, insolation);
                partial.max_insolation_W_m2 = std::max(partial.max_insolation_W_m2, insolation);
                compensated_add(insolation * cell.area_m2, partial.total_incoming_power_W,
                                power_compensation);

                if (incidence > 0.0) {
                    ++partial.illuminated_cell_count;
                } else {
                    ++partial.night_cell_count;
                    if (insolation != 0.0) {
                        ++partial.night_side_nonzero_count;
                    }
                }
                if (insolation < 0.0 || insolation > forcing.incident_solar_flux_W_m2) {
                    ++partial.out_of_range_count;
                }
            }
        });

    result.min_insolation_W_m2 = std::numeric_limits<double>::infinity();
    result.max_insolation_W_m2 = -std::numeric_limits<double>::infinity();
    double total_area_m2 = 0.0;
    double area_compensation = 0.0;
    double power_compensation = 0.0;
    for (const auto& partial : partials) {
        result.illuminated_cell_count += partial.illuminated_cell_count;
        result.night_cell_count += partial.night_cell_count;
        result.night_side_nonzero_count += partial.night_side_nonzero_count;
        result.out_of_range_count += partial.out_of_range_count;
        result.non_finite_count += partial.non_finite_count;
        result.min_insolation_W_m2 =
            std::min(result.min_insolation_W_m2, partial.min_insolation_W_m2);
        result.max_insolation_W_m2 =
            std::max(result.max_insolation_W_m2, partial.max_insolation_W_m2);
        compensated_add(partial.total_area_m2, total_area_m2, area_compensation);
        compensated_add(partial.total_incoming_power_W, result.total_incoming_power_W,
                        power_compensation);
    }

    if (result.cell_count == 0) {
        result.min_insolation_W_m2 = 0.0;
        result.max_insolation_W_m2 = 0.0;
        return result;
    }

    result.expected_incoming_power_W = std::numbers::pi_v<double> * mesh.radius_m() *
                                       mesh.radius_m() * forcing.incident_solar_flux_W_m2;
    result.global_mean_insolation_W_m2 = result.total_incoming_power_W / total_area_m2;
    result.expected_global_mean_insolation_W_m2 = 0.25 * forcing.incident_solar_flux_W_m2;
    if (result.expected_global_mean_insolation_W_m2 > 0.0) {
        result.relative_global_mean_error = std::abs(result.global_mean_insolation_W_m2 -
                                                     result.expected_global_mean_insolation_W_m2) /
                                            result.expected_global_mean_insolation_W_m2;
    }
    return result;
}

}  // namespace planetsim
