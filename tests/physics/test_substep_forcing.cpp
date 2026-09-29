#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/climate_calendar.hpp"
#include "sim/planet/orbit/orbit_state.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using planetsim::PlanetParameters;

constexpr double pi = std::numbers::pi_v<double>;

[[nodiscard]] PlanetParameters earth() { return PlanetParameters::earth_development(); }

[[nodiscard]] double mean_flux_scale_W_m2() {
    const auto parameters = earth();
    return parameters.star_luminosity_W /
           (16.0 * pi * parameters.semi_major_axis_m * parameters.semi_major_axis_m);
}

// Gauss-Legendre rule of n points on [-1, 1], by Newton iteration.
[[nodiscard]] std::vector<std::pair<double, double>> gauss_legendre(int n) {
    std::vector<std::pair<double, double>> rule;
    for (int i = 1; i <= n; ++i) {
        double x = std::cos(pi * (i - 0.25) / (n + 0.5));
        double derivative = 0.0;
        for (int iteration = 0; iteration < 100; ++iteration) {
            double p0 = 1.0;
            double p1 = x;
            for (int k = 2; k <= n; ++k) {
                const double p2 = ((2.0 * k - 1.0) * x * p1 - (k - 1.0) * p0) / k;
                p0 = p1;
                p1 = p2;
            }
            derivative = n * (x * p1 - p0) / (x * x - 1.0);
            const double step = p1 / derivative;
            x -= step;
            if (std::abs(step) < 1e-16) {
                break;
            }
        }
        rule.emplace_back(x, 2.0 / ((1.0 - x * x) * derivative * derivative));
    }
    return rule;
}

// Area mean over the sphere of the daily-mean formula at one instant,
// integrating in μ = sin φ with the kinks at the polar circles as breakpoints.
// The integrand has a square-root edge there, so Gauss-Legendre converges
// algebraically: 512 points per segment reach about 4e-9.
[[nodiscard]] double sphere_mean_daily_insolation(double declination_rad, double flux_W_m2) {
    static const auto rule = gauss_legendre(512);
    const double edge = std::cos(declination_rad);
    const std::array<double, 4> breaks{-1.0, -edge, edge, 1.0};
    double integral = 0.0;
    for (std::size_t segment = 0; segment + 1U < breaks.size(); ++segment) {
        const double low = breaks[segment];
        const double high = breaks[segment + 1U];
        for (const auto& [x, w] : rule) {
            const double mu = 0.5 * (high - low) * x + 0.5 * (high + low);
            const double cosine = std::sqrt(std::max(0.0, 1.0 - mu * mu));
            integral += 0.5 * (high - low) * w *
                        planetsim::daily_mean_insolation_W_m2(mu, cosine,
                                                              std::sin(declination_rad),
                                                              std::cos(declination_rad),
                                                              flux_W_m2);
        }
    }
    return 0.5 * integral;
}

// E1: closed-form cases of the daily-mean formula.
void check_formula(planetsim::test::Context& test) {
    const double s = 1361.0;
    PLANETSIM_EXPECT_NEAR(test, planetsim::daily_mean_insolation_W_m2(0.0, 0.0, s), s / pi,
                          1e-12 * s);
    PLANETSIM_EXPECT(test, planetsim::daily_mean_insolation_W_m2(80.0 * pi / 180.0,
                                                                 -20.0 * pi / 180.0, s) == 0.0);
    const double tilt = 0.4090926006005829;
    PLANETSIM_EXPECT_NEAR(test,
                          planetsim::daily_mean_insolation_W_m2(1.0, 0.0, std::sin(tilt),
                                                                std::cos(tilt), s),
                          s * std::sin(tilt), 1e-12 * s);
    PLANETSIM_EXPECT(test, planetsim::daily_mean_insolation_W_m2(-1.0, 0.0, std::sin(tilt),
                                                                 std::cos(tilt), s) == 0.0);
    double worst_symmetry = 0.0;
    for (double latitude = -90.0; latitude <= 90.0; latitude += 7.5) {
        for (double declination = -23.0; declination <= 23.0; declination += 4.6) {
            const double q = planetsim::daily_mean_insolation_W_m2(
                latitude * pi / 180.0, declination * pi / 180.0, s);
            const double mirrored = planetsim::daily_mean_insolation_W_m2(
                -latitude * pi / 180.0, -declination * pi / 180.0, s);
            worst_symmetry = std::max(worst_symmetry, std::abs(q - mirrored));
        }
    }
    PLANETSIM_EXPECT(test, worst_symmetry <= 1e-12 * s);
    // Continuity across the polar-day and polar-night edges.
    const double declination = 0.3;
    for (const double edge : {pi / 2.0 - declination, -(pi / 2.0 - declination)}) {
        const double below = planetsim::daily_mean_insolation_W_m2(edge - 1e-9, declination, s);
        const double above = planetsim::daily_mean_insolation_W_m2(edge + 1e-9, declination, s);
        PLANETSIM_EXPECT(test, std::abs(below - above) <= 1e-6 * s);
    }
    // The formula integrates to S/4 over the sphere at any instant.
    double worst_sphere = 0.0;
    for (double declination_deg = -23.4; declination_deg <= 23.4; declination_deg += 2.6) {
        worst_sphere = std::max(
            worst_sphere, std::abs(sphere_mean_daily_insolation(declination_deg * pi / 180.0, s) /
                                       (s / 4.0) -
                                   1.0));
    }
    PLANETSIM_EXPECT(test, worst_sphere <= 1e-8);
}

// E2 (ADR-0006 V3): the twelve sub-steps of one orbital year carry the
// analytic annual mean energy.
void check_annual_energy(planetsim::test::Context& test) {
    const auto parameters = earth();
    double weighted = 0.0;
    double total_ticks = 0.0;
    for (std::int64_t index = 0; index < 12; ++index) {
        const auto substep = planetsim::climate_substep(index, parameters);
        double mean = 0.0;
        for (const auto& node : planetsim::substep_forcing_nodes(parameters, substep)) {
            mean += node.weight * sphere_mean_daily_insolation(
                                      std::atan2(node.sin_declination, node.cos_declination),
                                      node.incident_flux_W_m2);
        }
        const auto length = static_cast<double>(substep.length_ticks());
        weighted += length * mean;
        total_ticks += length;
    }
    const double expected =
        mean_flux_scale_W_m2() /
        std::sqrt(1.0 - parameters.eccentricity * parameters.eccentricity);
    const double error = std::abs(weighted / total_ticks / expected - 1.0);
    std::cout << "E2 annual mean relative error: " << error << '\n';
    PLANETSIM_EXPECT(test, error <= 1e-6);
}

// E3 (ADR-0006 V4a): the analytic daily mean equals the instantaneous forcing
// averaged over one apparent solar day. Each day is centred on local noon, so
// the declination's drift through the day cancels to first order, and the
// sum is divided by the apparent (noon-to-noon) day, which differs from the
// mean synodic day by up to about 26 s through the year. Latitudes at the
// polar-day edge (|tan φ tan δ| > 0.95) are excluded: there the constant-
// declination assumption itself errs by up to about 0.1 W/m² (ADR-0006 §9).
void check_diurnal_mean(planetsim::test::Context& test) {
    const auto parameters = earth();
    const double tolerance = 1e-4 * mean_flux_scale_W_m2();
    const planetsim::Vec3d equator_point{1.0, 0.0, 0.0};
    double worst = 0.0;
    std::size_t compared = 0;
    for (int date = 0; date < 12; ++date) {
        const std::int64_t start = static_cast<std::int64_t>(date) * 43'829 + 7'000;
        std::int64_t noon = start;
        double highest = -2.0;
        for (std::int64_t tick = start; tick < start + 1'440; ++tick) {
            const auto orbit = evaluate_orbit(parameters, planetsim::simulation_time_s(tick));
            const double height = planetsim::dot(equator_point, orbit.sun_direction_body_unit);
            if (height > highest) {
                highest = height;
                noon = tick;
            }
        }
        const auto before =
            evaluate_orbit(parameters, planetsim::simulation_time_s(noon - 720)).sun_direction_body_unit;
        const auto after =
            evaluate_orbit(parameters, planetsim::simulation_time_s(noon + 720)).sun_direction_body_unit;
        const double residual =
            std::remainder(std::atan2(after.y, after.x) - std::atan2(before.y, before.x), 2.0 * pi);
        const double apparent_day_ticks = 1'440.0 * 2.0 * pi / (2.0 * pi - residual);
        const auto at_noon = evaluate_orbit(parameters, planetsim::simulation_time_s(noon));
        for (const double latitude_deg : {-80.0, -55.0, -20.0, 0.0, 30.0, 60.0, 85.0}) {
            const double latitude = latitude_deg * pi / 180.0;
            if (std::abs(std::tan(latitude) * std::tan(at_noon.solar_declination_rad)) > 0.95) {
                continue;
            }
            const planetsim::Vec3d normal{std::cos(latitude), 0.0, std::sin(latitude)};
            double sum = 0.0;
            for (std::int64_t tick = noon - 720; tick < noon + 720; ++tick) {
                const auto orbit = evaluate_orbit(parameters, planetsim::simulation_time_s(tick));
                sum += planetsim::top_of_atmosphere_insolation_W_m2(
                    normal, orbit.sun_direction_body_unit, orbit.incident_solar_flux_W_m2);
            }
            const double analytic = planetsim::daily_mean_insolation_W_m2(
                latitude, at_noon.solar_declination_rad, at_noon.incident_solar_flux_W_m2);
            worst = std::max(worst, std::abs(sum / apparent_day_ticks - analytic));
            ++compared;
        }
    }
    std::cout << "E3 worst daily-mean difference W/m2: " << worst << " over " << compared
              << " cases\n";
    PLANETSIM_EXPECT(test, compared >= 60U);
    PLANETSIM_EXPECT(test, worst <= tolerance);
}

[[nodiscard]] std::shared_ptr<const planetsim::PlanetMesh> mesh_at(std::uint32_t level) {
    return std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(level, 6'371'000.0));
}

// E4, E7, E8: the mesh field over one year at L5.
void check_mesh_year(planetsim::test::Context& test) {
    const auto parameters = earth();
    const auto mesh = mesh_at(5);
    planetsim::PlanetState state(mesh);
    double total_area = 0.0;
    for (const auto& cell : mesh->cells()) {
        total_area += cell.area_m2;
    }
    double weighted = 0.0;
    double total_ticks = 0.0;
    bool finite_non_negative = true;
    bool bounded = true;
    bool polar_night_zero = true;
    for (std::int64_t index = 0; index < 12; ++index) {
        const auto substep = planetsim::climate_substep(index, parameters);
        planetsim::update_substep_mean_insolation(state, parameters, substep, 4U);
        const auto nodes = planetsim::substep_forcing_nodes(parameters, substep);
        double maximum_flux = 0.0;
        for (const auto& node : nodes) {
            maximum_flux = std::max(maximum_flux, node.incident_flux_W_m2);
        }
        const auto& field = state.forcing().substep_mean_insolation_W_m2;
        double mean = 0.0;
        double cap_north = 0.0;
        double cap_south = 0.0;
        double equator = 0.0;
        double cap_north_area = 0.0;
        double cap_south_area = 0.0;
        double equator_area = 0.0;
        for (const auto& cell : mesh->cells()) {
            const double value = field[cell.id];
            const double latitude_deg = std::asin(cell.center_unit.z) * 180.0 / pi;
            finite_non_negative = finite_non_negative && std::isfinite(value) && value >= 0.0;
            bounded = bounded && value <= maximum_flux * (1.0 + 1e-6);
            mean += cell.area_m2 * value;
            if (latitude_deg > 80.0) {
                cap_north += cell.area_m2 * value;
                cap_north_area += cell.area_m2;
            } else if (latitude_deg < -80.0) {
                cap_south += cell.area_m2 * value;
                cap_south_area += cell.area_m2;
            } else if (std::abs(latitude_deg) < 5.0) {
                equator += cell.area_m2 * value;
                equator_area += cell.area_m2;
            }
            // Sub-step 3 spans the June solstice: the far south is dark throughout.
            if (index == 3 && latitude_deg < -71.0) {
                polar_night_zero = polar_night_zero && value == 0.0;
            }
        }
        if (index == 3) {   // E8: the June-solstice sub-step
            PLANETSIM_EXPECT(test, cap_north / cap_north_area > equator / equator_area);
        }
        if (index == 9) {   // the December-solstice sub-step
            PLANETSIM_EXPECT(test, cap_south / cap_south_area > equator / equator_area);
        }
        const auto length = static_cast<double>(substep.length_ticks());
        weighted += length * mean / total_area;
        total_ticks += length;
    }
    const double expected =
        mean_flux_scale_W_m2() /
        std::sqrt(1.0 - parameters.eccentricity * parameters.eccentricity);
    const double error = std::abs(weighted / total_ticks / expected - 1.0);
    std::cout << "E4 L5 mesh annual mean relative error: " << error << '\n';
    PLANETSIM_EXPECT(test, error <= 1e-4);
    PLANETSIM_EXPECT(test, finite_non_negative);
    PLANETSIM_EXPECT(test, bounded);
    PLANETSIM_EXPECT(test, polar_night_zero);
}

// E5 (ADR-0006 V4b): each sub-step mean against a dense (hourly) time mean
// of the daily-mean formula, per cell at L4.
void check_dense_time_mean(planetsim::test::Context& test) {
    const auto parameters = earth();
    const auto mesh = mesh_at(4);
    planetsim::PlanetState state(mesh);
    const double tolerance = 1e-4 * mean_flux_scale_W_m2();
    double worst = 0.0;
    for (std::int64_t index = 0; index < 12; ++index) {
        const auto substep = planetsim::climate_substep(index, parameters);
        planetsim::update_substep_mean_insolation(state, parameters, substep);
        const double begin_s = planetsim::simulation_time_s(substep.begin_tick);
        const double span_s = planetsim::simulation_time_s(substep.end_tick) - begin_s;
        const auto intervals = static_cast<int>(std::ceil(span_s / 3'600.0));
        std::vector<std::array<double, 3>> orbit_samples;
        for (int k = 0; k < intervals; ++k) {
            const auto orbit =
                evaluate_orbit(parameters, begin_s + (k + 0.5) * span_s / intervals);
            orbit_samples.push_back({std::sin(orbit.solar_declination_rad),
                                     std::cos(orbit.solar_declination_rad),
                                     orbit.incident_solar_flux_W_m2});
        }
        for (const auto& cell : mesh->cells()) {
            const double sin_latitude = cell.center_unit.z;
            const double cos_latitude = std::sqrt(cell.center_unit.x * cell.center_unit.x +
                                                  cell.center_unit.y * cell.center_unit.y);
            double dense = 0.0;
            for (const auto& sample : orbit_samples) {
                dense += planetsim::daily_mean_insolation_W_m2(sin_latitude, cos_latitude,
                                                               sample[0], sample[1], sample[2]);
            }
            dense /= static_cast<double>(intervals);
            worst = std::max(
                worst, std::abs(dense - state.forcing().substep_mean_insolation_W_m2[cell.id]));
        }
    }
    std::cout << "E5 worst sub-step vs dense mean W/m2: " << worst << '\n';
    PLANETSIM_EXPECT(test, worst <= tolerance);
}

// E6: bit-identical for any worker count.
void check_workers(planetsim::test::Context& test) {
    const auto parameters = earth();
    const auto mesh = mesh_at(4);
    const auto substep = planetsim::climate_substep(5, parameters);
    planetsim::PlanetState reference(mesh);
    planetsim::update_substep_mean_insolation(reference, parameters, substep, 1U);
    for (const std::size_t workers : {2U, 8U, 16U}) {
        planetsim::PlanetState candidate(mesh);
        planetsim::update_substep_mean_insolation(candidate, parameters, substep, workers);
        bool identical = true;
        for (const auto& cell : mesh->cells()) {
            identical = identical &&
                        candidate.forcing().substep_mean_insolation_W_m2[cell.id] ==
                            reference.forcing().substep_mean_insolation_W_m2[cell.id];
        }
        PLANETSIM_EXPECT(test, identical);
    }
}

// E9: the diurnal mean is refused where it does not apply.
void check_guard(planetsim::test::Context& test) {
    auto slow = earth();
    slow.sidereal_rotation_period_s = slow.orbital_period_s / 12.0;
    const auto substep = planetsim::climate_substep(0, earth());
    PLANETSIM_EXPECT_THROWS(test, std::domain_error,
                            planetsim::substep_forcing_nodes(slow, substep));
    auto locked = earth();
    locked.sidereal_rotation_period_s = locked.orbital_period_s;
    PLANETSIM_EXPECT_THROWS(test, std::domain_error, planetsim::synodic_day_s(locked));
    PLANETSIM_EXPECT_NEAR(test, planetsim::synodic_day_s(earth()), 86'400.0, 1.0);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_formula(test);
    check_annual_energy(test);
    check_diurnal_mean(test);
    check_mesh_year(test);
    check_dense_time_mean(test);
    check_workers(test);
    check_guard(test);
    return test.result();
}
