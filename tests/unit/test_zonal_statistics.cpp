#include "sim/planet/coordinates/local_tangent_basis.hpp"
#include "sim/planet/dynamics/zonal_statistics.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "tests/test_support.hpp"

#include <cmath>
#include <numbers>

// The zonal-mean statistics of the winds (ADR-0011 V5, V6).
int main() {
    planetsim::test::Context test;
    constexpr double pi = std::numbers::pi_v<double>;
    constexpr double radius = 6'371'000.0;
    constexpr double gravity = 9.80616;
    constexpr std::size_t layers = 3;
    constexpr std::size_t bands = 18;
    const auto mesh = planetsim::make_icosphere(4U, radius);
    const std::size_t cells = mesh.cell_count();

    planetsim::Field3D<double> east(layers, cells);
    planetsim::Field3D<double> north(layers, cells);
    planetsim::Field3D<double> temperature(layers, cells);
    planetsim::Field2D<double> surface_pressure(cells, 100'000.0);

    // A steady zonal state: u = 20 cos φ, a uniform northward v = 1 m/s,
    // T = 250 + 10 k − 30 sin² φ. No eddies, and the streamfunction is the
    // mass flux of the layers below.
    planetsim::ZonalStatistics steady(mesh, layers, bands, gravity);
    for (std::size_t k = 0; k < layers; ++k) {
        for (const auto& cell : mesh.cells()) {
            const double latitude = planetsim::latitude_rad(cell.center_unit);
            const std::size_t i = cell.id.to_index();
            east.layer(k)[i] = 20.0 * std::cos(latitude);
            north.layer(k)[i] = 1.0;
            const double sine = std::sin(latitude);
            temperature.layer(k)[i] = 250.0 + 10.0 * static_cast<double>(k) - 30.0 * sine * sine;
        }
    }
    for (int sample = 0; sample < 3; ++sample) {
        steady.add(east, north, temperature, surface_pressure);
    }
    const auto zonal = steady.profile();
    PLANETSIM_EXPECT(test, zonal.samples == 3U);
    PLANETSIM_EXPECT(test, zonal.bands == bands && zonal.layers == layers);
    const double layer_mass = 100'000.0 / (gravity * static_cast<double>(layers));
    for (std::size_t b = 0; b < bands; ++b) {
        const double latitude = zonal.latitude_deg[b] * pi / 180.0;
        PLANETSIM_EXPECT_NEAR(test, zonal.latitude_deg[b], -85.0 + 10.0 * static_cast<double>(b),
                              1e-12);
        PLANETSIM_EXPECT_NEAR(test, zonal.surface_pressure_Pa[b], 100'000.0, 1e-9);
        // Band means of a smooth profile: within the band's spread.
        PLANETSIM_EXPECT_NEAR(test, zonal.at(zonal.eastward_m_s, 1, b), 20.0 * std::cos(latitude),
                              0.6);
        PLANETSIM_EXPECT_NEAR(test, zonal.at(zonal.temperature_K, 2, b),
                              270.0 - 30.0 * std::sin(latitude) * std::sin(latitude), 1.5);
        for (std::size_t k = 0; k < layers; ++k) {
            PLANETSIM_EXPECT_NEAR(test, zonal.at(zonal.eddy_kinetic_m2_s2, k, b), 0.0, 1e-9);
            PLANETSIM_EXPECT_NEAR(test, zonal.at(zonal.eddy_heat_flux_K_m_s, k, b), 0.0, 1e-9);
            PLANETSIM_EXPECT_NEAR(test, zonal.at(zonal.eddy_momentum_flux_m2_s2, k, b), 0.0,
                                  1e-9);
        }
        const double ring = 2.0 * pi * radius * std::cos(latitude) * layer_mass;
        PLANETSIM_EXPECT_NEAR(test, zonal.streamfunction_kg_s[b], 0.0, 0.0);
        for (std::size_t m = 1; m <= layers; ++m) {
            PLANETSIM_EXPECT_NEAR(test, zonal.streamfunction_kg_s[m * bands + b] /
                                            (static_cast<double>(m) * ring),
                                  1.0, 1e-12);
        }
    }

    // Transient eddies: u' = v' = ±A cos 2λ alternately, so [u'v'] and the
    // eddy kinetic energy ½[u'² + v'²] are both A² [cos² 2λ] ≈ A²/2, and with
    // T' = ±B cos 2λ in phase, [v'T'] ≈ A B / 2.
    constexpr double a = 10.0;
    constexpr double b_amplitude = 4.0;
    planetsim::ZonalStatistics eddies(mesh, layers, bands, gravity);
    for (int sample = 0; sample < 4; ++sample) {
        const double sign = sample % 2 == 0 ? 1.0 : -1.0;
        for (std::size_t k = 0; k < layers; ++k) {
            for (const auto& cell : mesh.cells()) {
                const double wave = std::cos(2.0 * planetsim::longitude_rad(cell.center_unit));
                const std::size_t i = cell.id.to_index();
                east.layer(k)[i] = sign * a * wave;
                north.layer(k)[i] = sign * a * wave;
                temperature.layer(k)[i] = 260.0 + sign * b_amplitude * wave;
            }
        }
        eddies.add(east, north, temperature, surface_pressure);
    }
    const auto waves = eddies.profile();
    for (std::size_t b = 2; b + 2 < bands; ++b) {   // enough cells per band
        PLANETSIM_EXPECT_NEAR(test, waves.at(waves.eastward_m_s, 0, b), 0.0, 1e-12);
        PLANETSIM_EXPECT_NEAR(test, waves.at(waves.eddy_momentum_flux_m2_s2, 0, b) / (a * a), 0.5,
                              0.05);
        PLANETSIM_EXPECT_NEAR(test, waves.at(waves.eddy_kinetic_m2_s2, 1, b) / (a * a), 0.5, 0.05);
        PLANETSIM_EXPECT_NEAR(test, waves.at(waves.eddy_heat_flux_K_m_s, 2, b) / (a * b_amplitude),
                              0.5, 0.05);
    }

    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::ZonalStatistics(mesh, 0U, bands, gravity));
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            eddies.add(planetsim::Field3D<double>(2U, cells), north, temperature,
                                       surface_pressure));
    return test.result();
}
