#include "sim/planet/atmosphere/saturation.hpp"
#include "tests/test_support.hpp"

#include <cmath>
#include <iostream>

// Saturation (ADR-0021 §4.2, V1): e_s over water and ice against reference
// values, continuity at 0 °C, the slopes against finite differences, and
// q_sat's definition and bound.
int main() {
    planetsim::test::Context test;
    // Hyland and Wexler (1983) over water, Murphy and Koop (2005) over ice.
    struct Reference {
        double celsius;
        double pascal;
    };
    for (const auto& [celsius, pascal] : {Reference{0.0, 611.21}, Reference{10.0, 1228.1},
                                          Reference{20.0, 2339.3}, Reference{30.0, 4246.0},
                                          Reference{40.0, 7384.9}, Reference{-10.0, 259.9},
                                          Reference{-20.0, 103.3}, Reference{-40.0, 12.84},
                                          Reference{-60.0, 1.080}}) {
        const double e = planetsim::saturation_vapour_pressure_Pa(planetsim::freezing_point_K + celsius);
        std::cout << "e_s(" << celsius << " C) = " << e << " Pa (reference " << pascal << ")\n";
        PLANETSIM_EXPECT(test, std::abs(e / pascal - 1.0) <= 2.0e-3);
    }
    // The two branches meet at 0 °C.
    const double below = planetsim::saturation_vapour_pressure_Pa(planetsim::freezing_point_K - 1e-9);
    const double above = planetsim::saturation_vapour_pressure_Pa(planetsim::freezing_point_K);
    PLANETSIM_EXPECT(test, std::abs(below / above - 1.0) <= 1.0e-4);

    for (const double temperature : {200.0, 240.0, 265.0, 280.0, 300.0, 315.0}) {
        const double h = 1e-4;
        const double e_slope = planetsim::saturation_vapour_pressure_slope_Pa_K(temperature);
        const double e_finite = (planetsim::saturation_vapour_pressure_Pa(temperature + h) -
                                 planetsim::saturation_vapour_pressure_Pa(temperature - h)) /
                                (2.0 * h);
        PLANETSIM_EXPECT(test, std::abs(e_slope / e_finite - 1.0) <= 1e-6);
        const double p = 80'000.0;
        const double q_slope = planetsim::saturation_specific_humidity_slope(temperature, p);
        const double q_finite = (planetsim::saturation_specific_humidity(temperature + h, p) -
                                 planetsim::saturation_specific_humidity(temperature - h, p)) /
                                (2.0 * h);
        PLANETSIM_EXPECT(test, std::abs(q_slope / q_finite - 1.0) <= 1e-6);
        // q_sat = ε e / (p − (1 − ε) e).
        const double e = planetsim::saturation_vapour_pressure_Pa(temperature);
        PLANETSIM_EXPECT_NEAR(test, planetsim::saturation_specific_humidity(temperature, p),
                              planetsim::vapour_molar_ratio * e /
                                  (p - (1.0 - planetsim::vapour_molar_ratio) * e),
                              1e-15);
    }
    // 20 °C at 1000 hPa: about 14.7 g/kg.
    PLANETSIM_EXPECT_NEAR(test, planetsim::saturation_specific_humidity(293.15, 100'000.0), 0.01469,
                          0.0002);
    // Where e_s reaches p the bound holds.
    PLANETSIM_EXPECT(test, planetsim::saturation_specific_humidity(373.15, 100.0) == 1.0);
    PLANETSIM_EXPECT(test, planetsim::latent_heat_J_kg(280.0) == planetsim::latent_heat_vaporisation_J_kg);
    PLANETSIM_EXPECT(test, planetsim::latent_heat_J_kg(260.0) == planetsim::latent_heat_sublimation_J_kg);
    return test.result();
}
