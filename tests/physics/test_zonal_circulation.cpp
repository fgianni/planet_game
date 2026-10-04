#include "sim/planet/dynamics/zonal_circulation.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

// The climate mode's zonal-mean circulation (ADR-0011 §14, task M6-04
// step B): rest, the exact Jacobian, Newton's convergence, Held–Suarez
// against the reference core's zonal means, V8 and V9.
namespace {

using planetsim::ZonalCirculation;
using planetsim::ZonalCirculationParameters;
using planetsim::ZonalCirculationSolution;

[[nodiscard]] planetsim::HeldSuarezForcing held_suarez() {
    planetsim::HeldSuarezForcing forcing;
    forcing.enabled = true;
    return forcing;
}

// The northern Hadley edge: the first boundary where the lowest interior
// interface's streamfunction changes sign from its value next to the
// equator (90° if it never does).
[[nodiscard]] double hadley_edge_deg(const ZonalCirculationSolution& solution) {
    const std::size_t equator = solution.bands / 2U - 1U;
    const double first = solution.at_boundary(solution.streamfunction_kg_s, 1U, equator + 1U);
    for (std::size_t i = equator + 1U; i + 1U < solution.bands; ++i) {
        if ((solution.at_boundary(solution.streamfunction_kg_s, 1U, i) > 0.0) != (first > 0.0)) {
            return solution.boundary_latitude_deg[i];
        }
    }
    return 90.0;
}

[[nodiscard]] double max_abs(const std::vector<double>& values) {
    double result = 0.0;
    for (const double value : values) {
        result = std::max(result, std::abs(value));
    }
    return result;
}

}  // namespace

int main() {
    planetsim::test::Context test;

    // 1. An isothermal atmosphere at rest over terrain, in hydrostatic
    // balance, with no heating and no friction: the initial state is the
    // exact steady state, at any rotation.
    for (const double rotation : {0.0, 7.292e-5}) {
        ZonalCirculationParameters parameters;
        parameters.rotation_rate_rad_s = rotation;
        const ZonalCirculation model(parameters);
        planetsim::ZonalForcing forcing;
        const std::size_t bands = parameters.bands;
        const std::size_t cells = bands * parameters.layer_count;
        constexpr double temperature = 250.0;
        forcing.surface_height_m.resize(bands);
        forcing.surface_pressure_Pa.resize(bands);
        for (std::size_t j = 0; j < bands; ++j) {
            const double height = 1500.0 * (1.0 + std::sin(0.9 * static_cast<double>(j)));
            forcing.surface_height_m[j] = height;
            forcing.surface_pressure_Pa[j] =
                1.0e5 * std::exp(-parameters.gravity_m_s2 * height /
                                 (parameters.gas_constant_J_kg_K * temperature));
        }
        forcing.drag_coefficient.assign(bands, 0.0);
        forcing.temperature_K.assign(cells, temperature);
        forcing.heating_K_s.assign(cells, 0.0);
        forcing.heating_derivative_s.assign(cells, -1.0 / (40.0 * 86'400.0));
        forcing.rayleigh_friction_s.assign(cells, 0.0);
        const auto rest = model.initial_state(forcing);
        PLANETSIM_EXPECT_NEAR(test, model.scaled_norm(model.residual(forcing, rest)), 0.0, 1e-9);
        const auto solution = model.solve(forcing);
        PLANETSIM_EXPECT(test, solution.method == planetsim::ZonalSolutionMethod::newton);
        PLANETSIM_EXPECT_NEAR(test, max_abs(solution.eastward_m_s), 0.0, 1e-9);
        PLANETSIM_EXPECT_NEAR(test, max_abs(solution.northward_m_s), 0.0, 1e-12);
        PLANETSIM_EXPECT_NEAR(test, max_abs(solution.eddy_kinetic_m2_s2), 0.0, 1e-12);
        for (std::size_t c = 0; c < cells; ++c) {
            PLANETSIM_EXPECT_NEAR(test, solution.temperature_K[c], temperature, 1e-9);
        }
    }

    // 2. The forward-mode Jacobian against central differences on a small
    // band count, away from any special state.
    {
        ZonalCirculationParameters parameters;
        parameters.bands = 12;
        const ZonalCirculation model(parameters);
        const auto forcing = planetsim::held_suarez_zonal_forcing(parameters, held_suarez());
        auto state = model.initial_state(forcing);
        const std::size_t block = model.block_size();
        for (std::size_t c = 0; c < state.size(); ++c) {
            state[c] += 0.3 * std::sin(1.7 * static_cast<double>(c) + 0.3);
        }
        for (std::size_t j = 0; j < parameters.bands; ++j) {
            state[j * block + 2U * parameters.layer_count] =
                50.0 + 10.0 * std::sin(static_cast<double>(j));
        }
        const auto jacobian = model.jacobian(forcing, state);
        const std::size_t n = state.size();
        std::vector<double> row_scale(n, 0.0);
        for (std::size_t r = 0; r < n; ++r) {
            for (std::size_t c = 0; c < n; ++c) {
                if (jacobian.in_band(r, c)) {
                    row_scale[r] = std::max(row_scale[r], std::abs(jacobian.at(r, c)));
                }
            }
        }
        double worst = 0.0;
        double outside = 0.0;
        for (std::size_t c = 0; c < n; ++c) {
            const double h = 1e-6 * std::max(1.0, std::abs(state[c]));
            auto plus = state;
            auto minus = state;
            plus[c] += h;
            minus[c] -= h;
            const auto r_plus = model.residual(forcing, plus);
            const auto r_minus = model.residual(forcing, minus);
            for (std::size_t r = 0; r < n; ++r) {
                const double difference = (r_plus[r] - r_minus[r]) / (2.0 * h);
                if (jacobian.in_band(r, c)) {
                    worst = std::max(worst,
                                     std::abs(difference - jacobian.at(r, c)) / row_scale[r]);
                } else {
                    outside = std::max(outside, std::abs(difference));
                }
            }
        }
        PLANETSIM_EXPECT_NEAR(test, worst, 0.0, 1e-5);
        PLANETSIM_EXPECT_NEAR(test, outside, 0.0, 0.0);
    }

    // 3. Held–Suarez on three layers: the reference core's zonal means at
    // L4 (tools/zonal_mean_prototype/data/hs_L4_N3.csv) have a 25.8 m/s jet
    // at 32.5°, trades over 0–17.5°, surface westerlies over 22.5–47.5°,
    // polar easterlies from 57.5° and a 38.7 K bottom-layer contrast.
    ZonalCirculationParameters parameters;
    const ZonalCirculation model(parameters);
    const auto forcing = planetsim::held_suarez_zonal_forcing(parameters, held_suarez());
    const auto solution = model.solve(forcing);
    const std::size_t bands = solution.bands;
    const std::size_t top = solution.layers - 1U;
    PLANETSIM_EXPECT(test, solution.residual < parameters.tolerance);
    {
        // The state, the residual's zero, and a second solve repeat bit for bit.
        const auto again = model.solve(forcing);
        PLANETSIM_EXPECT(test, again.state == solution.state);
        PLANETSIM_EXPECT_NEAR(test, model.scaled_norm(model.residual(forcing, solution.state)),
                              0.0, parameters.tolerance);
    }
    std::size_t jet = bands / 2U;
    for (std::size_t j = bands / 2U; j < bands; ++j) {
        if (solution.at_band(solution.eastward_m_s, top, j) >
            solution.at_band(solution.eastward_m_s, top, jet)) {
            jet = j;
        }
    }
    PLANETSIM_EXPECT(test, solution.latitude_deg[jet] >= 22.5 && solution.latitude_deg[jet] <= 37.5);
    PLANETSIM_EXPECT(test, solution.at_band(solution.eastward_m_s, top, jet) > 18.0 &&
                               solution.at_band(solution.eastward_m_s, top, jet) < 35.0);
    const auto bottom_wind = [&](double latitude) {
        const auto band = static_cast<std::size_t>((latitude + 90.0) / 5.0);
        return solution.at_band(solution.eastward_m_s, 0U, band);
    };
    for (const double sign : {-1.0, 1.0}) {
        for (const double latitude : {2.5, 7.5}) {
            PLANETSIM_EXPECT(test, bottom_wind(sign * latitude) < 0.0);   // trades
        }
        for (const double latitude : {27.5, 32.5, 37.5, 42.5}) {
            PLANETSIM_EXPECT(test, bottom_wind(sign * latitude) > 0.0);   // westerlies
        }
        for (const double latitude : {67.5, 72.5, 77.5}) {
            PLANETSIM_EXPECT(test, bottom_wind(sign * latitude) < 0.0);   // polar easterlies
        }
    }
    const double contrast = solution.at_band(solution.temperature_K, 0U, bands / 2U) -
                            solution.at_band(solution.temperature_K, 0U, bands - 1U);
    PLANETSIM_EXPECT(test, contrast > 33.0 && contrast < 45.0);
    // The forcing is symmetric about the equator, and so is the solution.
    for (std::size_t k = 0; k < solution.layers; ++k) {
        for (std::size_t j = 0; j < bands / 2U; ++j) {
            PLANETSIM_EXPECT_NEAR(test, solution.at_band(solution.eastward_m_s, k, j),
                                  solution.at_band(solution.eastward_m_s, k, bands - 1U - j),
                                  1e-6);
        }
    }

    // 4. Newton converges quadratically near the solution.
    {
        auto state = solution.state;
        for (std::size_t c = 0; c < state.size(); ++c) {
            state[c] += 1e-3 * std::sin(0.37 * static_cast<double>(c)) *
                        std::max(1.0, std::abs(state[c])) * 1e-2;
        }
        std::vector<double> norms;
        for (int iteration = 0; iteration < 4; ++iteration) {
            auto residual = model.residual(forcing, state);
            norms.push_back(model.scaled_norm(residual));
            const planetsim::BandedLU lu(model.jacobian(forcing, state));
            lu.solve(residual);
            for (std::size_t c = 0; c < state.size(); ++c) {
                state[c] -= residual[c];
            }
        }
        norms.push_back(model.scaled_norm(model.residual(forcing, state)));
        PLANETSIM_EXPECT(test, norms[0] > 1e-3);
        PLANETSIM_EXPECT(test, norms[2] < 1e-2 * norms[1]);
        PLANETSIM_EXPECT(test, norms[4] < 1e-9);
    }

    // 5. V8 (rotation) and V9 (torque). At Ω = 0 there is no wind, no eddy
    // energy and one overturning cell per hemisphere; the Hadley cell
    // narrows as Ω grows. The flux-form transport and the eddies exert no
    // torque, so friction's surface torque sums to zero.
    std::vector<double> edges;
    for (const double factor : {0.0, 0.5, 1.0, 2.0}) {
        ZonalCirculationParameters rotating;
        rotating.rotation_rate_rad_s *= factor;
        const ZonalCirculation rotating_model(rotating);
        const auto result = factor == 1.0 ? solution
                                          : rotating_model.solve(planetsim::held_suarez_zonal_forcing(
                                                rotating, held_suarez()));
        edges.push_back(hadley_edge_deg(result));
        PLANETSIM_EXPECT_NEAR(test, result.total_torque_N_m, 0.0,
                              1e-9 * std::max(result.gross_torque_N_m, 1.0));
        if (factor == 0.0) {
            PLANETSIM_EXPECT_NEAR(test, max_abs(result.eastward_m_s), 0.0, 1e-9);
            PLANETSIM_EXPECT_NEAR(test, max_abs(result.eddy_kinetic_m2_s2), 0.0, 1e-9);
            PLANETSIM_EXPECT_NEAR(test, edges.back(), 90.0, 0.0);
        }
    }
    PLANETSIM_EXPECT(test, edges[1] >= edges[2] && edges[2] >= edges[3] && edges[3] < edges[1]);
    PLANETSIM_EXPECT(test, edges[2] >= 20.0 && edges[2] <= 35.0);

    // 6. Five layers converge too; invalid forcing is refused.
    {
        ZonalCirculationParameters five;
        five.layer_count = 5;
        const ZonalCirculation five_model(five);
        const auto result =
            five_model.solve(planetsim::held_suarez_zonal_forcing(five, held_suarez()));
        PLANETSIM_EXPECT(test, result.residual < five.tolerance);
        auto bad = forcing;
        bad.heating_derivative_s[3] = 1e-6;   // Λ must not be positive
        PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, model.solve(bad));
        ZonalCirculationParameters one_layer;
        one_layer.layer_count = 1;
        PLANETSIM_EXPECT_THROWS(test, std::invalid_argument, ZonalCirculation{one_layer});
    }
    return test.result();
}
