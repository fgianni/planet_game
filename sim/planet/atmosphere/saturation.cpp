#include "sim/planet/atmosphere/saturation.hpp"

#include <cmath>

namespace planetsim {

double saturation_vapour_pressure_Pa(double temperature_K) noexcept {
    if (temperature_K >= freezing_point_K) {
        const double celsius = temperature_K - freezing_point_K;
        return 611.2 * std::exp(17.67 * celsius / (temperature_K - 29.65));
    }
    return std::exp(9.550426 - 5723.265 / temperature_K + 3.53068 * std::log(temperature_K) -
                    0.00728332 * temperature_K);
}

double saturation_vapour_pressure_slope_Pa_K(double temperature_K) noexcept {
    const double e = saturation_vapour_pressure_Pa(temperature_K);
    if (temperature_K >= freezing_point_K) {
        const double d = temperature_K - 29.65;
        return e * 17.67 * (freezing_point_K - 29.65) / (d * d);
    }
    return e * (5723.265 / (temperature_K * temperature_K) + 3.53068 / temperature_K - 0.00728332);
}

double saturation_specific_humidity(double temperature_K, double pressure_Pa) noexcept {
    const double e = saturation_vapour_pressure_Pa(temperature_K);
    const double denominator = pressure_Pa - (1.0 - vapour_molar_ratio) * e;
    if (!(denominator > vapour_molar_ratio * e)) {
        return 1.0;
    }
    return vapour_molar_ratio * e / denominator;
}

double saturation_specific_humidity_slope(double temperature_K, double pressure_Pa) noexcept {
    const double e = saturation_vapour_pressure_Pa(temperature_K);
    const double denominator = pressure_Pa - (1.0 - vapour_molar_ratio) * e;
    if (!(denominator > vapour_molar_ratio * e)) {
        return 0.0;
    }
    // d/de [ε e / (p − (1 − ε) e)] = ε p / (p − (1 − ε) e)².
    return vapour_molar_ratio * pressure_Pa / (denominator * denominator) *
           saturation_vapour_pressure_slope_Pa_K(temperature_K);
}

double latent_heat_J_kg(double temperature_K) noexcept {
    return temperature_K > freezing_point_K ? latent_heat_vaporisation_J_kg
                                            : latent_heat_sublimation_J_kg;
}

}  // namespace planetsim
