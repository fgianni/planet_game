#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/solar_diagnostics.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>

int main() {
    planetsim::test::Context test;
    auto parameters = planetsim::PlanetParameters::earth_development();
    parameters.eccentricity = 0.0;
    auto mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(parameters.mesh_subdivision, parameters.radius_m));
    planetsim::PlanetState state(mesh);

    constexpr std::size_t samples_per_orbit = 48;
    constexpr double maximum_quadrature_relative_error = 5.0e-4;
    double annual_mean_insolation_W_m2 = 0.0;
    double maximum_instantaneous_relative_error = 0.0;

    for (std::size_t sample = 0; sample < samples_per_orbit; ++sample) {
        const double time_s = (static_cast<double>(sample) + 0.5) /
                              static_cast<double>(samples_per_orbit) * parameters.orbital_period_s;
        planetsim::update_solar_forcing(state, parameters, time_s);
        const auto diagnostics = planetsim::analyze_solar_forcing(state);
        PLANETSIM_EXPECT(test, diagnostics.forcing_valid());
        PLANETSIM_EXPECT(test, diagnostics.relative_global_mean_error <=
                                   maximum_quadrature_relative_error);
        annual_mean_insolation_W_m2 += diagnostics.global_mean_insolation_W_m2;
        maximum_instantaneous_relative_error =
            std::max(maximum_instantaneous_relative_error, diagnostics.relative_global_mean_error);
    }

    annual_mean_insolation_W_m2 /= static_cast<double>(samples_per_orbit);
    const double reference_solar_flux_W_m2 =
        parameters.star_luminosity_W /
        (4.0 * std::numbers::pi_v<double> * parameters.semi_major_axis_m *
         parameters.semi_major_axis_m);
    const double expected_mean_insolation_W_m2 = 0.25 * reference_solar_flux_W_m2;
    const double annual_relative_error =
        std::abs(annual_mean_insolation_W_m2 - expected_mean_insolation_W_m2) /
        expected_mean_insolation_W_m2;
    PLANETSIM_EXPECT(test, annual_relative_error <= maximum_quadrature_relative_error);

    std::cout << std::setprecision(17)
              << "annual_mean_insolation_W_m2: " << annual_mean_insolation_W_m2 << '\n'
              << "expected_mean_insolation_W_m2: " << expected_mean_insolation_W_m2 << '\n'
              << "annual_relative_error: " << annual_relative_error << '\n'
              << "maximum_instantaneous_relative_error: " << maximum_instantaneous_relative_error
              << '\n';

    return test.result();
}
