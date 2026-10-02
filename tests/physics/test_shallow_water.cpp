#include "sim/core/random/counter_rng.hpp"
#include "sim/planet/dynamics/shallow_water.hpp"
#include "sim/planet/dynamics/williamson_cases.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "tests/test_support.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

// ADR-0011 V3 and V11 for the shallow-water core (task M6-02).
namespace {

using planetsim::CGridGeometry;
using planetsim::PlanetMesh;
using planetsim::ShallowWaterModel;
using planetsim::ShallowWaterState;

constexpr double day = planetsim::williamson_day_s;

[[nodiscard]] planetsim::Vec3d tilted_axis() {
    return {std::sin(0.7) * std::cos(0.4), std::sin(0.7) * std::sin(0.4), std::cos(0.7)};
}

[[nodiscard]] double relative(double final, double initial) {
    return (final - initial) / std::abs(initial);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    constexpr std::size_t workers = 4U;

    // Test 2: steady geostrophic flow about a tilted axis, 5 days, L3–L5.
    std::vector<double> l2_errors;
    for (std::uint32_t level = 3; level <= 5U; ++level) {
        const auto mesh = planetsim::make_icosphere(level, planetsim::williamson_radius_m);
        const auto grid = CGridGeometry::build(mesh);
        auto test2 = planetsim::williamson_case_2(mesh, grid, tilted_axis());
        const ShallowWaterModel model(mesh, grid, test2.parameters, test2.bottom_height_m);
        ShallowWaterState state = test2.initial;
        const auto initial = model.diagnose(state, workers);
        planetsim::SubstepRule rule;
        rule.wave_speed_m_s = 180.0;
        rule.max_wind_m_s = 60.0;
        model.advance(state, 5.0 * day, rule, workers);
        const auto final = model.diagnose(state, workers);
        PLANETSIM_EXPECT(test, std::abs(relative(final.mass_m3, initial.mass_m3)) <= 1.0e-13);
        PLANETSIM_EXPECT(test, std::abs(relative(final.energy_J(), initial.energy_J())) <= 1.0e-7);
        l2_errors.push_back(
            planetsim::thickness_errors(mesh, state.thickness_m, test2.initial.thickness_m).l2);
    }
    for (std::size_t index = 1; index < l2_errors.size(); ++index) {
        // ADR-0011 §12: L2 order ≥ 1 decides that TRiSK is accurate enough.
        PLANETSIM_EXPECT(test, std::log2(l2_errors[index - 1U] / l2_errors[index]) >= 1.0);
    }
    PLANETSIM_EXPECT(test, l2_errors.back() <= 2.0e-4);

    // Test 5 and the remaining checks run at L4.
    const auto mesh = planetsim::make_icosphere(4U, planetsim::williamson_radius_m);
    const auto grid = CGridGeometry::build(mesh);
    const auto test5 = planetsim::williamson_case_5(mesh, grid);
    const ShallowWaterModel inviscid(mesh, grid, test5.parameters, test5.bottom_height_m);
    planetsim::SubstepRule rule5;
    rule5.wave_speed_m_s = 250.0;
    rule5.max_wind_m_s = 60.0;
    {
        ShallowWaterState state = test5.initial;
        const auto initial = inviscid.diagnose(state, workers);
        inviscid.advance(state, 15.0 * day, rule5, workers);
        const auto final = inviscid.diagnose(state, workers);
        PLANETSIM_EXPECT(test, std::abs(relative(final.mass_m3, initial.mass_m3)) <= 1.0e-13);
        PLANETSIM_EXPECT(test, std::abs(relative(final.energy_J(), initial.energy_J())) <= 1.0e-6);
        PLANETSIM_EXPECT(test, final.max_wind_m_s > 20.0 && final.max_wind_m_s < 60.0);
    }

    // Energy: TRiSK conserves it in space, so the change over a day is
    // RK3's, and it falls by at least 4 when the step halves.
    {
        const std::size_t steps = planetsim::substep_count(mesh, day, rule5);
        std::vector<double> changes;
        for (const std::size_t count : {steps, 2U * steps}) {
            ShallowWaterState state = test5.initial;
            const double initial = inviscid.diagnose(state, workers).energy_J();
            for (std::size_t index = 0; index < count; ++index) {
                inviscid.step(state, day / static_cast<double>(count), workers);
            }
            changes.push_back(std::abs(inviscid.diagnose(state, workers).energy_J() - initial) /
                              initial);
        }
        PLANETSIM_EXPECT(test, changes[0] / changes[1] >= 4.0);
    }

    // Hyperviscosity: the fastest mode of the vector Laplacian decays in
    // between τ and 1.25 τ, and the dissipation only removes energy.
    {
        const double tau = 6.0 * 3600.0;
        auto parameters = test5.parameters;
        parameters.hyperviscosity_m4_s = planetsim::hyperviscosity_for_damping_time(mesh, tau);
        const ShallowWaterModel viscous(mesh, grid, parameters, test5.bottom_height_m);

        planetsim::EdgeField<double> mode(mesh.edge_count());
        planetsim::EdgeField<double> image(mesh.edge_count());
        for (std::size_t index = 0; index < mesh.edge_count(); ++index) {
            mode.values()[index] =
                planetsim::keyed_random_unit_double(0x0602U, planetsim::RandomStreamId::validation,
                                                    0, static_cast<std::uint32_t>(index)) -
                0.5;
        }
        double eigenvalue = 0.0;
        for (int iteration = 0; iteration < 400; ++iteration) {
            viscous.vector_laplacian(mode, image, workers);
            double numerator = 0.0;
            double denominator = 0.0;
            double norm = 0.0;
            for (std::size_t index = 0; index < mesh.edge_count(); ++index) {
                numerator += mode.values()[index] * image.values()[index];
                denominator += mode.values()[index] * mode.values()[index];
                norm += image.values()[index] * image.values()[index];
            }
            eigenvalue = numerator / denominator;
            norm = std::sqrt(norm);
            for (std::size_t index = 0; index < mesh.edge_count(); ++index) {
                mode.values()[index] = image.values()[index] / norm;
            }
        }
        const double decay_time = 1.0 / (parameters.hyperviscosity_m4_s * eigenvalue * eigenvalue);
        PLANETSIM_EXPECT(test, eigenvalue < 0.0);
        PLANETSIM_EXPECT(test, decay_time >= tau && decay_time <= 1.25 * tau);

        ShallowWaterState with = test5.initial;
        ShallowWaterState without = test5.initial;
        const std::size_t steps = planetsim::substep_count(mesh, day, rule5);
        double previous = viscous.diagnose(with, workers).energy_J();
        bool monotone = true;
        for (std::size_t index = 0; index < steps; ++index) {
            viscous.step(with, day / static_cast<double>(steps), workers);
            inviscid.step(without, day / static_cast<double>(steps), workers);
            const double energy = viscous.diagnose(with, workers).energy_J();
            monotone = monotone && energy <= previous;
            previous = energy;
        }
        PLANETSIM_EXPECT(test, monotone);
        PLANETSIM_EXPECT(test, viscous.diagnose(with, workers).energy_J() <
                                   inviscid.diagnose(without, workers).energy_J());
    }

    // Determinism: bit-identical on 1, 2 and 8 workers.
    {
        const auto run = [&](std::size_t count) {
            ShallowWaterState state = test5.initial;
            for (int index = 0; index < 20; ++index) {
                inviscid.step(state, 600.0, count);
            }
            return state;
        };
        const ShallowWaterState serial = run(1U);
        for (const std::size_t count : {2U, 8U}) {
            const ShallowWaterState parallel = run(count);
            bool equal = true;
            for (std::size_t index = 0; index < mesh.cell_count(); ++index) {
                equal = equal && serial.thickness_m[index] == parallel.thickness_m[index];
            }
            for (std::size_t index = 0; index < mesh.edge_count(); ++index) {
                equal = equal && serial.normal_velocity_m_s.values()[index] ==
                                     parallel.normal_velocity_m_s.values()[index];
            }
            PLANETSIM_EXPECT(test, equal);
        }
    }

    // The sub-step rule refuses winds above its bound, and is a function of
    // the mesh and the bounds only.
    {
        planetsim::SubstepRule tight = rule5;
        tight.max_wind_m_s = 1.0;
        ShallowWaterState state = test5.initial;
        PLANETSIM_EXPECT_THROWS(test, std::runtime_error,
                                inviscid.advance(state, 3600.0, tight, workers));
        PLANETSIM_EXPECT(test, planetsim::substep_count(mesh, day, rule5) ==
                                   planetsim::substep_count(mesh, day, rule5));
        PLANETSIM_EXPECT(test, planetsim::substep_count(mesh, 2.0 * day, rule5) >=
                                   2U * planetsim::substep_count(mesh, day, rule5) - 1U);
    }

    return test.result();
}
