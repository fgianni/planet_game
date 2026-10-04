#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/planet/coordinates/local_tangent_basis.hpp"
#include "sim/planet/dynamics/balanced_circulation.hpp"
#include "sim/planet/dynamics/orography.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

// The climate mode's azonal circulation and balanced surface pressure
// (ADR-0011 §4.4, §4.6; task M6-04 step C).
namespace {

[[nodiscard]] planetsim::EdgeId edge_id(std::size_t index) {
    return planetsim::EdgeId{static_cast<planetsim::EdgeId::value_type>(index)};
}

// A zonal circulation at rest on `bands` bands, or with a prescribed
// overturning v̄_k(φ) at the boundaries.
[[nodiscard]] planetsim::ZonalCirculationSolution zonal_state(std::size_t bands, std::size_t layers,
                                                              double surface_pressure_Pa,
                                                              const std::vector<double>& amplitude) {
    planetsim::ZonalCirculationSolution zonal;
    zonal.bands = bands;
    zonal.layers = layers;
    const double width = 180.0 / static_cast<double>(bands);
    for (std::size_t j = 0; j < bands; ++j) {
        zonal.latitude_deg.push_back(-90.0 + width * (static_cast<double>(j) + 0.5));
    }
    for (std::size_t i = 0; i + 1U < bands; ++i) {
        zonal.boundary_latitude_deg.push_back(-90.0 + width * static_cast<double>(i + 1U));
    }
    zonal.surface_pressure_Pa.assign(bands, surface_pressure_Pa);
    zonal.northward_m_s.assign(layers * (bands - 1U), 0.0);
    for (std::size_t k = 0; k < layers; ++k) {
        for (std::size_t i = 0; i + 1U < bands; ++i) {
            const double latitude = zonal.boundary_latitude_deg[i] * std::numbers::pi / 180.0;
            zonal.northward_m_s[k * (bands - 1U) + i] = amplitude[k] * std::sin(2.0 * latitude);
        }
    }
    return zonal;
}

}  // namespace

int main() {
    planetsim::test::Context test;
    planetsim::Scenario scenario;
    scenario.subdivision = 4;
    scenario.spin_up_years = 1;
    planetsim::PlanetRun run(scenario, 4U);
    const auto& mesh = run.state().mesh();
    const auto& surface = run.surface_parameters();
    const auto zonal_parameters =
        planetsim::zonal_circulation_parameters(run.parameters(), surface.atmosphere);
    const std::size_t n = zonal_parameters.layer_count;
    const std::size_t cells = mesh.cell_count();
    planetsim::Field2D<double> height;
    planetsim::compute_surface_height(mesh, run.state().slow(), run.fractions(), height);
    const auto dynamics_height = planetsim::limit_dynamics_orography_steps(mesh, height).height_m;
    const planetsim::BalancedCirculation balanced(mesh, zonal_parameters);
    const auto& coarse = balanced.coarse_mesh();
    const std::size_t groups = coarse.cell_count();
    const std::size_t edges = coarse.edge_count();
    PLANETSIM_EXPECT(test, coarse.subdivision() + 1U == mesh.subdivision());
    const double g = zonal_parameters.gravity_m_s2;
    const double gas = zonal_parameters.gas_constant_J_kg_K;

    // 1. An isothermal atmosphere at rest over the terrain, in hydrostatic
    // balance: the balance recovers s = −Φ_s' / (R T) up to a constant, and
    // nothing moves.
    {
        constexpr double temperature = 250.0;
        planetsim::SlowState slow = run.state().slow();
        for (std::size_t i = 0; i < cells; ++i) {
            slow.atmosphere_surface_pressure_Pa[i] =
                1.0e5 * std::exp(-g * dynamics_height[i] / (gas * temperature));
            for (std::size_t k = 0; k < n; ++k) {
                slow.atmosphere_temperature_K.layer(k)[i] = temperature;
            }
        }
        const auto rest = zonal_state(zonal_parameters.bands, n, 1.0e5, std::vector<double>(n, 0.0));
        const auto result = balanced.solve(slow, dynamics_height, run.fractions(), rest, 4U);
        double largest = 0.0;
        for (const double flux : result.mass_flux_kg_m_s) {
            largest = std::max(largest, std::abs(flux));
        }
        // 1e-6 m/s in a layer of about 3,400 kg/m².
        PLANETSIM_EXPECT_NEAR(test, largest, 0.0, 3.4e-3);
        std::vector<double> coarse_height(groups, 0.0);
        for (std::size_t i = 0; i < cells; ++i) {
            coarse_height[balanced.group_of_cell()[i]] +=
                mesh.cells()[i].area_m2 * dynamics_height[i];
        }
        // s + g z / (R T) is a function of latitude alone (the zonal mean
        // of the terrain): equal for cells on the same latitude.
        std::vector<double> offset(groups);
        for (std::size_t c = 0; c < groups; ++c) {
            const double z = coarse_height[c] / balanced.graph().area_m2[c];
            offset[c] = result.log_departure[c] + g * z / (gas * temperature);
        }
        double largest_difference = 0.0;
        std::size_t pairs = 0;
        for (std::size_t c = 0; c < groups; ++c) {
            for (std::size_t d = c + 1U; d < groups; ++d) {
                if (std::abs(coarse.cells()[c].center_unit.z - coarse.cells()[d].center_unit.z) <
                    1e-12) {
                    largest_difference =
                        std::max(largest_difference, std::abs(offset[c] - offset[d]));
                    ++pairs;
                }
            }
        }
        PLANETSIM_EXPECT(test, pairs > 100U);
        PLANETSIM_EXPECT_NEAR(test, largest_difference, 0.0, 1e-8);
    }

    // 2. The month's state: converged, columns closed, the atmosphere's mass
    // held, and the same for any worker count.
    const auto begin = run.tick();
    run.run_until(planetsim::climate_substep_containing(begin, run.parameters()).end_tick);
    planetsim::AtmosphereHeating heating;
    planetsim::compute_atmosphere_heating(run.state(), run.parameters(), surface, run.fractions(),
                                          run.state().forcing().substep_mean_insolation_W_m2,
                                          heating, 4U);
    const auto forcing = planetsim::zonal_forcing_from_state(
        mesh, run.state().slow(), heating, dynamics_height, run.fractions(), zonal_parameters.bands);
    const auto zonal = planetsim::ZonalCirculation(zonal_parameters).solve(forcing);
    const auto result = balanced.solve(run.state().slow(), dynamics_height, run.fractions(), zonal, 4U);
    const auto serial = balanced.solve(run.state().slow(), dynamics_height, run.fractions(), zonal, 1U);
    PLANETSIM_EXPECT(test, result.mass_flux_kg_m_s == serial.mass_flux_kg_m_s);
    PLANETSIM_EXPECT(test, result.surface_pressure_Pa == serial.surface_pressure_Pa);
    PLANETSIM_EXPECT(test, result.relative_residual < 1e-9);
    PLANETSIM_EXPECT(test, result.relative_column_divergence < 1e-8);
    {
        double mass = 0.0;
        for (std::size_t i = 0; i < cells; ++i) {
            mass += mesh.cells()[i].area_m2 * run.state().slow().atmosphere_surface_pressure_Pa[i];
        }
        double balanced_mass = 0.0;
        for (std::size_t c = 0; c < groups; ++c) {
            balanced_mass += balanced.graph().area_m2[c] * result.surface_pressure_Pa[c];
        }
        PLANETSIM_EXPECT_NEAR(test, balanced_mass / mass, 1.0, 1e-13);
        // Every layer's mass closes: W_{k+1} − W_k = −div F_k by construction,
        // so W_N is the column divergence the solver leaves.
        double largest_top = 0.0;
        double largest_interior = 0.0;
        for (std::size_t c = 0; c < groups; ++c) {
            largest_top = std::max(largest_top, std::abs(result.vertical_mass_flux_kg_m2_s[n * groups + c]));
            for (std::size_t m = 1; m < n; ++m) {
                largest_interior = std::max(
                    largest_interior, std::abs(result.vertical_mass_flux_kg_m2_s[m * groups + c]));
            }
        }
        PLANETSIM_EXPECT(test, largest_top < 1e-8 * largest_interior);
    }
    // Azonal winds of the right size outside the deep tropics: 3–30 m/s rms
    // in the top layer between 30° and 60°.
    {
        double sum = 0.0;
        double count = 0.0;
        for (std::size_t e = 0; e < edges; ++e) {
            const double latitude = std::abs(
                std::asin(balanced.coarse_grid().edges()[e].midpoint_unit.z) * 180.0 / std::numbers::pi);
            if (latitude < 30.0 || latitude > 60.0) {
                continue;
            }
            const auto& edge = coarse.edge(edge_id(e));
            const double mu = 0.5 *
                              (result.surface_pressure_Pa[edge.first_cell.to_index()] +
                               result.surface_pressure_Pa[edge.second_cell.to_index()]) /
                              (g * static_cast<double>(n));
            const double u = result.azonal_mass_flux_kg_m_s[(n - 1U) * edges + e] / mu;
            sum += u * u;
            count += 1.0;
        }
        const double rms = std::sqrt(sum / count);
        PLANETSIM_EXPECT(test, rms > 3.0 && rms < 30.0);
    }

    // 3. The overturning mapped to the edges: zero column sum on every edge,
    // and the zonal circulation's mass transport across a latitude circle.
    {
        planetsim::SlowState slow = run.state().slow();
        for (std::size_t i = 0; i < cells; ++i) {
            slow.atmosphere_surface_pressure_Pa[i] = 1.0e5;
            for (std::size_t k = 0; k < n; ++k) {
                slow.atmosphere_temperature_K.layer(k)[i] = 250.0;
            }
        }
        const planetsim::Field2D<double> flat(cells, 0.0);
        const std::vector<double> amplitude{-2.0, 1.0, 1.0};
        const auto overturning = zonal_state(zonal_parameters.bands, n, 1.0e5, amplitude);
        const auto mapped = balanced.solve(slow, flat, run.fractions(), overturning, 4U);
        double largest_sum = 0.0;
        double largest = 0.0;
        for (std::size_t e = 0; e < edges; ++e) {
            double sum = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                // The overturning alone: the azonal part here is rounding.
                const double mapped_overturning = mapped.mass_flux_kg_m_s[k * edges + e] -
                                           mapped.azonal_mass_flux_kg_m_s[k * edges + e];
                sum += mapped_overturning;
                largest = std::max(largest, std::abs(mapped_overturning));
            }
            largest_sum = std::max(largest_sum, std::abs(sum));
        }
        PLANETSIM_EXPECT(test, largest_sum < 1e-12 * largest);

        // Net flux of layer 0 out of the cells south of 30° N against
        // 2π a cos φ μ v̄(30° N).
        const double latitude = 30.0 * std::numbers::pi / 180.0;
        double outflow = 0.0;
        for (std::size_t e = 0; e < edges; ++e) {
            const auto& edge = coarse.edge(edge_id(e));
            const bool south_a = planetsim::latitude_rad(coarse.cells()[edge.first_cell.to_index()].center_unit) < latitude;
            const bool south_b = planetsim::latitude_rad(coarse.cells()[edge.second_cell.to_index()].center_unit) < latitude;
            if (south_a != south_b) {
                outflow += (south_a ? 1.0 : -1.0) * edge.length_m * mapped.mass_flux_kg_m_s[e];
            }
        }
        const double mu = 1.0e5 / (g * static_cast<double>(n));
        const double expected = 2.0 * std::numbers::pi * coarse.radius_m() * std::cos(latitude) * mu *
                                amplitude[0] * std::sin(2.0 * latitude);
        PLANETSIM_EXPECT_NEAR(test, outflow / expected, 1.0, 0.1);
    }
    return test.result();
}
