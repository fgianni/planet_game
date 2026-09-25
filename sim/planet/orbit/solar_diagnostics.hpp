#pragma once

#include <cstddef>

namespace planetsim {

class PlanetState;

struct SolarDiagnostics {
    std::size_t cell_count = 0;
    std::size_t illuminated_cell_count = 0;
    std::size_t night_cell_count = 0;
    std::size_t night_side_nonzero_count = 0;
    std::size_t out_of_range_count = 0;
    std::size_t non_finite_count = 0;
    double min_insolation_W_m2 = 0.0;
    double max_insolation_W_m2 = 0.0;
    double total_incoming_power_W = 0.0;
    double expected_incoming_power_W = 0.0;
    double global_mean_insolation_W_m2 = 0.0;
    double expected_global_mean_insolation_W_m2 = 0.0;
    double relative_global_mean_error = 0.0;

    [[nodiscard]] bool forcing_valid() const noexcept {
        return cell_count > 0 && night_side_nonzero_count == 0 && out_of_range_count == 0 &&
               non_finite_count == 0;
    }
};

[[nodiscard]] SolarDiagnostics analyze_solar_forcing(const PlanetState& state,
                                                     std::size_t worker_count = 1U);

}  // namespace planetsim
