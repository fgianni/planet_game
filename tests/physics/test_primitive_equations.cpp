#include "sim/core/random/counter_rng.hpp"
#include "sim/planet/dynamics/primitive_equations.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "tests/test_support.hpp"

#include <cmath>
#include <cstdint>

// ADR-0011 V4 and V11, and the energy identity, for the dry
// primitive-equation core (task M6-03, step A).
namespace {

using planetsim::Field2D;
using planetsim::Field3D;
using planetsim::PrimitiveEquationModel;
using planetsim::PrimitiveEquationParameters;
using planetsim::PrimitiveEquationState;

[[nodiscard]] double noise(std::uint32_t stream, std::size_t index) {
    return planetsim::keyed_random_unit_double(0x0603U, planetsim::RandomStreamId::validation,
                                               stream, static_cast<std::uint32_t>(index)) -
           0.5;
}

[[nodiscard]] PrimitiveEquationState empty_like(const PrimitiveEquationState& state) {
    return {Field2D<double>(state.surface_pressure_Pa.size()),
            Field3D<double>(state.mass_theta.layer_count(), state.mass_theta.cell_count()),
            Field3D<double>(state.normal_velocity_m_s.layer_count(),
                            state.normal_velocity_m_s.cell_count())};
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const auto mesh = planetsim::make_icosphere(3U, 6'371'000.0);
    const auto grid = planetsim::CGridGeometry::build(mesh);
    const std::size_t cells = mesh.cell_count();

    // Rough terrain up to 3 km for every check below.
    Field2D<double> height(cells);
    for (std::size_t i = 0; i < cells; ++i) {
        height[i] = 3000.0 * (noise(0, i) + 0.5);
    }

    // The energy identity: for random states the tendencies change the
    // total energy by nothing but rounding (unforced, inviscid), N = 1, 3, 5.
    for (const std::size_t n : {1U, 3U, 5U}) {
        PrimitiveEquationParameters parameters;
        parameters.layer_count = n;
        const PrimitiveEquationModel model(mesh, grid, parameters, height);
        Field2D<double> ps(cells);
        Field3D<double> t(n, cells);
        for (std::size_t i = 0; i < cells; ++i) {
            ps[i] = 100'000.0 + 4'000.0 * noise(1, i);
            for (std::size_t k = 0; k < n; ++k) {
                t.layer(k)[i] = 260.0 - 20.0 * static_cast<double>(k) + 30.0 * noise(2 + static_cast<std::uint32_t>(k), i);
            }
        }
        auto state = model.state_at_rest(ps, t);
        for (std::size_t k = 0; k < n; ++k) {
            for (std::size_t e = 0; e < mesh.edge_count(); ++e) {
                state.normal_velocity_m_s.layer(k)[e] = 40.0 * noise(10 + static_cast<std::uint32_t>(k), e);
            }
        }
        auto rate = empty_like(state);
        model.tendency(state, rate);
        const auto energy = model.energy_rate(state, rate);
        PLANETSIM_EXPECT(test, std::abs(energy.rate_W) <= 1.0e-12 * energy.gross_W);

        // Temperatures round-trip through Θ.
        Field3D<double> back(n, cells);
        model.temperatures(state, back);
        double worst = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            for (std::size_t i = 0; i < cells; ++i) {
                worst = std::max(worst, std::abs(back.layer(k)[i] - t.layer(k)[i]));
            }
        }
        PLANETSIM_EXPECT(test, worst <= 1.0e-10);

        // Mass is exact over a day of steps from this random state.
        const auto before = model.diagnose(state);
        for (int step = 0; step < 20; ++step) {
            model.step(state, 120.0);
        }
        const auto after = model.diagnose(state);
        PLANETSIM_EXPECT(test, std::abs(after.mass_kg - before.mass_kg) <= 1.0e-13 * before.mass_kg);
    }

    // V4: an isothermal atmosphere at rest over the terrain stays at rest.
    {
        PrimitiveEquationParameters parameters;
        parameters.layer_count = 3;
        const PrimitiveEquationModel model(mesh, grid, parameters, height);
        Field2D<double> ps(cells);
        Field3D<double> t(3, cells, 250.0);
        for (std::size_t i = 0; i < cells; ++i) {
            ps[i] = 100'000.0 * std::exp(-parameters.gravity_m_s2 * height[i] /
                                         (parameters.gas_constant_J_kg_K * 250.0));
        }
        auto state = model.state_at_rest(ps, t);
        planetsim::SubstepRule rule;
        model.advance(state, 10.0 * 86'400.0, rule, 4U);
        PLANETSIM_EXPECT(test, model.diagnose(state, 4U).max_wind_m_s <= 0.5);
    }

    // Held–Suarez with hyperviscosity: energy changes only by forcing, and
    // a short run from rest stays finite and spins up winds; determinism on
    // 1, 2 and 8 workers.
    {
        PrimitiveEquationParameters parameters;
        parameters.layer_count = 3;
        parameters.held_suarez.enabled = true;
        parameters.hyperviscosity_m4_s =
            planetsim::hyperviscosity_for_damping_time(mesh, 8.0 * 3600.0);
        const PrimitiveEquationModel model(mesh, grid, parameters, Field2D<double>(cells, 0.0));
        Field2D<double> ps(cells, 100'000.0);
        Field3D<double> t(3, cells);
        for (std::size_t k = 0; k < 3U; ++k) {
            for (std::size_t i = 0; i < cells; ++i) {
                t.layer(k)[i] = 300.0 + 0.2 * noise(20 + static_cast<std::uint32_t>(k), i);
            }
        }
        const auto start = model.state_at_rest(ps, t);
        const auto run = [&](std::size_t workers) {
            auto state = start;
            for (int step = 0; step < 30; ++step) {
                model.step(state, 600.0, workers);
            }
            return state;
        };
        const auto serial = run(1U);
        for (const std::size_t workers : {2U, 8U}) {
            const auto parallel = run(workers);
            bool equal = true;
            for (std::size_t i = 0; i < cells; ++i) {
                equal = equal && serial.surface_pressure_Pa[i] == parallel.surface_pressure_Pa[i];
            }
            for (std::size_t k = 0; k < 3U; ++k) {
                for (std::size_t i = 0; i < cells; ++i) {
                    equal = equal && serial.mass_theta.layer(k)[i] == parallel.mass_theta.layer(k)[i];
                }
                for (std::size_t e = 0; e < mesh.edge_count(); ++e) {
                    equal = equal && serial.normal_velocity_m_s.layer(k)[e] ==
                                         parallel.normal_velocity_m_s.layer(k)[e];
                }
            }
            PLANETSIM_EXPECT(test, equal);
        }

        auto state = start;
        planetsim::SubstepRule rule;
        model.advance(state, 10.0 * 86'400.0, rule, 4U);
        const auto spun = model.diagnose(state, 4U);
        PLANETSIM_EXPECT(test, std::isfinite(spun.energy_J()));
        // From rest the 40-day relaxation spins winds up slowly: 3.4 m/s at
        // day 10 at L3 (9.9 m/s at day 30).
        PLANETSIM_EXPECT(test, spun.max_wind_m_s > 2.0 && spun.max_wind_m_s < 100.0);
    }

    return test.result();
}
