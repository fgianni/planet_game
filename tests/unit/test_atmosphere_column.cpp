#include "sim/planet/atmosphere/atmosphere_column.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

using planetsim::AtmosphereParameters;
using planetsim::ColumnRadiation;
using planetsim::SurfaceExchange;

constexpr double sigma = planetsim::column_stefan_boltzmann_W_m2_K4;
// A step long enough that storage is negligible: one solve is the steady
// state.
constexpr double steady_dt_s = 1e20;

[[nodiscard]] double gravity() {
    return planetsim::surface_gravity_m_s2(planetsim::PlanetParameters::earth_development());
}

// A black surface of no heat capacity absorbing `sunlight`: it re-emits all
// it receives, U = S + D, and exchanges no sensible heat.
struct RadiativeSurface {
    double sunlight_W_m2 = 0.0;
    SurfaceExchange operator()(double downward_W_m2, double /*air_K*/) const {
        SurfaceExchange exchange;
        exchange.upward_W_m2 = sunlight_W_m2 + downward_W_m2;
        exchange.d_upward_d_downward = 1.0;
        return exchange;
    }
};

// ADR-0010 V2: one layer of emissivity g over a black surface, no exchange
// and no convection, reaches the closed form of ADR-0007's grey layer,
// σ T_s⁴ (1 − g/2) = S.
void check_single_layer(planetsim::test::Context& test) {
    double worst = 0.0;
    for (const double g : {0.2, 0.5, 0.78, 0.95}) {
        AtmosphereParameters parameters;
        parameters.layer_count = 1U;
        parameters.linear_optical_depth_fraction = 1.0;
        parameters.longwave_optical_depth = -std::log1p(-g);   // ε₁ = g at p_s = p₀
        parameters.convection = false;
        const auto column =
            planetsim::column_radiation(parameters, parameters.reference_pressure_Pa, gravity());
        PLANETSIM_EXPECT_NEAR(test, column.emissivity[0], g, 1e-15);
        const double sunlight = 240.0;
        std::vector<double> before{250.0};
        std::vector<double> after{250.0};
        const auto result = planetsim::solve_atmosphere_column(
            column, before, steady_dt_s, 0.0, false, RadiativeSurface{sunlight}, after);
        PLANETSIM_EXPECT(test, result.converged);
        const double surface_K4 = result.upward_surface_W_m2 / sigma;
        worst = std::max(worst, std::abs(surface_K4 * sigma * (1.0 - 0.5 * g) - sunlight) /
                                    sunlight);
        worst = std::max(worst, std::abs(result.outgoing_W_m2 - sunlight) / sunlight);
        // The layer sits at the skin temperature T_s / 2^¼.
        worst = std::max(worst, std::abs(std::pow(after[0], 4) - 0.5 * surface_K4) /
                                    surface_K4);
    }
    std::cout << "single_layer worst_relative=" << worst << '\n';
    PLANETSIM_EXPECT(test, worst <= 1e-10);
}

// ADR-0010 V3: N black layers over a black surface: the layer j-th from the
// top at T⁴ = j T_e⁴ and the surface at (N + 1) T_e⁴, T_e⁴ = S/σ.
void check_black_ladder(planetsim::test::Context& test) {
    double worst = 0.0;
    for (std::uint32_t layers = 1U; layers <= 5U; ++layers) {
        AtmosphereParameters parameters;
        parameters.layer_count = layers;
        parameters.linear_optical_depth_fraction = 1.0;
        parameters.longwave_optical_depth = 1e5;
        parameters.convection = false;
        const auto column =
            planetsim::column_radiation(parameters, parameters.reference_pressure_Pa, gravity());
        const double sunlight = 240.0;
        std::vector<double> before(layers, 250.0);
        std::vector<double> after(layers, 250.0);
        const auto result = planetsim::solve_atmosphere_column(
            column, before, steady_dt_s, 0.0, false, RadiativeSurface{sunlight}, after);
        PLANETSIM_EXPECT(test, result.converged);
        const double te4 = sunlight / sigma;
        for (std::size_t layer = 0; layer < layers; ++layer) {
            const double expected = static_cast<double>(layers - layer) * te4;
            worst = std::max(worst, std::abs(std::pow(after[layer], 4) - expected) / expected);
        }
        const double surface = static_cast<double>(layers + 1U) * sunlight;
        worst = std::max(worst, std::abs(result.upward_surface_W_m2 - surface) / surface);
        worst = std::max(worst, std::abs(result.outgoing_W_m2 - sunlight) / sunlight);
    }
    std::cout << "black_ladder worst_relative=" << worst << '\n';
    PLANETSIM_EXPECT(test, worst <= 1e-9);
}

// ADR-0010 V4: convective adjustment conserves Σ T, leaves θ_c
// non-decreasing upward, is idempotent and leaves a stable column alone.
void check_convective_adjustment(planetsim::test::Context& test) {
    AtmosphereParameters parameters;
    parameters.layer_count = 5U;
    const auto column = planetsim::column_radiation(parameters, 95'000.0, gravity());
    std::uint64_t state = 0x1234'5678'9ABC'DEF0ULL;
    const auto next_unit = [&state]() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(state >> 11U) * 0x1.0p-53;
    };
    bool conserved = true;
    bool neutral = true;
    bool idempotent = true;
    int adjusted_count = 0;
    for (int sample = 0; sample < 200; ++sample) {
        std::vector<double> t(5U);
        for (double& value : t) {
            value = 180.0 + 140.0 * next_unit();
        }
        const double before = std::accumulate(t.begin(), t.end(), 0.0);
        const bool adjusted = planetsim::convective_adjustment(column, t);
        adjusted_count += adjusted ? 1 : 0;
        const double after = std::accumulate(t.begin(), t.end(), 0.0);
        conserved = conserved && std::abs(after - before) <= 8e-16 * before;
        for (std::size_t k = 1; k < t.size(); ++k) {
            neutral = neutral && t[k] / column.exner[k] >=
                                     t[k - 1U] / column.exner[k - 1U] * (1.0 - 1e-15);
        }
        const auto copy = t;
        const bool again = planetsim::convective_adjustment(column, t);
        idempotent = idempotent && !again && copy == t;
    }
    PLANETSIM_EXPECT(test, conserved);
    PLANETSIM_EXPECT(test, neutral);
    PLANETSIM_EXPECT(test, idempotent);
    PLANETSIM_EXPECT(test, adjusted_count > 100);
    // A stable column (θ_c increasing upward) is untouched bit for bit.
    std::vector<double> stable(5U);
    for (std::size_t k = 0; k < 5U; ++k) {
        stable[k] = (280.0 + 5.0 * static_cast<double>(k)) * column.exner[k];
    }
    const auto copy = stable;
    PLANETSIM_EXPECT(test, !planetsim::convective_adjustment(column, stable));
    PLANETSIM_EXPECT(test, copy == stable);
}

// A surface at fixed temperature with sensible exchange: emission σT_g⁴,
// absorbs all downward longwave, H = γ (T_g − A).
struct FixedSurface {
    double surface_K = 290.0;
    double exchange_W_m2_K = 10.0;
    SurfaceExchange operator()(double /*downward_W_m2*/, double air_K) const {
        SurfaceExchange exchange;
        const double t2 = surface_K * surface_K;
        exchange.upward_W_m2 = sigma * t2 * t2;
        exchange.sensible_W_m2 = exchange_W_m2_K * (surface_K - air_K);
        exchange.d_sensible_d_air = -exchange_W_m2_K;
        return exchange;
    }
};

// The θ_c slope that the transport solve uses agrees with a finite
// difference of the solved column, and the residual meets the tolerance on
// a monthly step.
void check_source_response(planetsim::test::Context& test) {
    for (std::uint32_t layers : {1U, 3U, 5U}) {
        AtmosphereParameters parameters;
        parameters.layer_count = layers;
        parameters.longwave_optical_depth = 2.0;
        const auto column = planetsim::column_radiation(parameters, 98'000.0, gravity());
        std::vector<double> before(layers);
        for (std::size_t k = 0; k < layers; ++k) {
            before[k] = 285.0 * column.exner[k];
        }
        const double dt = 2.6e6;
        const auto solve = [&](double h, std::vector<double>& out) {
            out = before;
            return planetsim::solve_atmosphere_column(column, before, dt, h, false,
                                                      FixedSurface{}, out);
        };
        std::vector<double> base;
        std::vector<double> plus;
        std::vector<double> minus;
        const auto result = solve(20.0, base);
        const auto up = solve(20.0 + 1e-3, plus);
        const auto down = solve(20.0 - 1e-3, minus);
        PLANETSIM_EXPECT(test, result.converged && result.max_residual_W_m2 <= 1e-8);
        const double finite = (up.theta_K - down.theta_K) / 2e-3;
        std::cout << "source_response layers=" << layers << " slope=" << result.theta_slope_K_m2_W
                  << " finite=" << finite << " iterations=" << result.iterations << '\n';
        PLANETSIM_EXPECT(test, std::abs(result.theta_slope_K_m2_W - finite) <=
                                   1e-6 * std::abs(finite));
        PLANETSIM_EXPECT(test, result.theta_slope_K_m2_W > 0.0);
        // Each layer's slope too (ADR-0011 §17.1), and the dry static
        // energy's through layer_dry_static_energy.
        std::vector<double> slope(result.temperature_slope_K_m2_W.begin(),
                                  result.temperature_slope_K_m2_W.begin() + layers);
        std::vector<double> energy_up(layers);
        std::vector<double> energy_down(layers);
        std::vector<double> energy_slope(layers);
        planetsim::layer_dry_static_energy(9'000.0, plus, energy_up);
        planetsim::layer_dry_static_energy(9'000.0, minus, energy_down);
        planetsim::layer_dry_static_energy(0.0, slope, energy_slope);
        for (std::size_t k = 0; k < layers; ++k) {
            const double layer_finite = (plus[k] - minus[k]) / 2e-3;
            PLANETSIM_EXPECT(test, std::abs(slope[k] - layer_finite) <=
                                       1e-6 * std::abs(layer_finite) + 1e-12);
            const double energy_finite = (energy_up[k] - energy_down[k]) / 2e-3;
            PLANETSIM_EXPECT(test, std::abs(energy_slope[k] - energy_finite) <=
                                       1e-6 * std::abs(energy_finite) + 1e-9);
        }
    }
}

// The dry static energy of the layers, s = c_p T + Φ: the vertical
// discretisation's energy identity, mean_k Φ_k = Φ_s + R mean_k T_k (so
// Σ (c_v T + Φ) m = Σ c_p T m + Φ_s M), holds to rounding for any column,
// and Φ grows upward.
void check_dry_static_energy(planetsim::test::Context& test) {
    const double cp = planetsim::dry_air_heat_capacity_J_kg_K;
    const double gas = planetsim::dry_air_gas_constant_J_kg_K;
    for (const std::size_t layers : {1U, 3U, 5U}) {
        std::vector<double> temperature(layers);
        for (std::size_t k = 0; k < layers; ++k) {
            temperature[k] = 290.0 - 17.0 * static_cast<double>(k) + 3.0 * std::sin(1.0 + static_cast<double>(k));
        }
        std::vector<double> energy(layers);
        planetsim::layer_dry_static_energy(1'000.0, temperature, energy);
        double mean_phi = 0.0;
        double mean_t = 0.0;
        double previous = 1'000.0;
        for (std::size_t k = 0; k < layers; ++k) {
            const double phi = energy[k] - cp * temperature[k];
            PLANETSIM_EXPECT(test, phi > previous);
            previous = phi;
            mean_phi += phi / static_cast<double>(layers);
            mean_t += temperature[k] / static_cast<double>(layers);
        }
        PLANETSIM_EXPECT_NEAR(test, mean_phi, 1'000.0 + gas * mean_t, 1e-9 * mean_phi);
    }
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_single_layer(test);
    check_black_ladder(test);
    check_convective_adjustment(test);
    check_source_response(test);
    check_dry_static_energy(test);
    return test.result();
}
