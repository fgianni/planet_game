#include "sim/planet/geology/geology_generator.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using planetsim::ElevationSample;
using planetsim::HypsometryQuantiles;

[[nodiscard]] bool non_decreasing(const HypsometryQuantiles& quantiles) {
    return std::is_sorted(quantiles.begin(), quantiles.end());
}

}  // namespace

int main() {
    planetsim::test::Context test;

    // Quantiles of equal-weight samples 0..7 (given out of order): midpoints at
    // (i + 1/2) / 8, so fraction k/8 lies halfway between samples k-1 and k.
    {
        std::vector<ElevationSample> samples;
        for (const int value : {5, 2, 7, 0, 3, 6, 1, 4}) {
            samples.push_back({static_cast<double>(value) * 100.0, 1.0});
        }
        const HypsometryQuantiles quantiles = planetsim::weighted_elevation_quantiles(samples);
        PLANETSIM_EXPECT(test, quantiles[0] == 0.0F);
        PLANETSIM_EXPECT(test, quantiles[8] == 700.0F);
        for (std::size_t level = 1; level < 8; ++level) {
            PLANETSIM_EXPECT_NEAR(test, quantiles[level], (static_cast<double>(level) - 0.5) * 100.0,
                                  1e-4);
        }
    }
    // Weights matter: a heavy low sample drags the median down.
    {
        std::vector<ElevationSample> samples{{0.0, 3.0}, {100.0, 1.0}};
        const HypsometryQuantiles quantiles = planetsim::weighted_elevation_quantiles(samples);
        PLANETSIM_EXPECT(test, non_decreasing(quantiles));
        // Midpoints at 3/8 and 7/8: the median is a quarter of the way up.
        PLANETSIM_EXPECT_NEAR(test, quantiles[4], 25.0, 1e-5);
        PLANETSIM_EXPECT(test, quantiles[8] == 100.0F);
    }
    // A single sample gives a flat cell.
    {
        std::vector<ElevationSample> samples{{-42.0, 2.0}};
        const HypsometryQuantiles quantiles = planetsim::weighted_elevation_quantiles(samples);
        PLANETSIM_EXPECT(test, std::all_of(quantiles.begin(), quantiles.end(),
                                           [](float value) { return value == -42.0F; }));
    }
    {
        std::vector<ElevationSample> empty;
        PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                                planetsim::weighted_elevation_quantiles(empty));
    }
    {
        // A NaN is rejected before sorting, not left to an invalid comparator.
        std::vector<ElevationSample> with_nan{
            {10.0, 1.0}, {std::numeric_limits<double>::quiet_NaN(), 1.0}, {-5.0, 1.0}};
        PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                                planetsim::weighted_elevation_quantiles(with_nan));
    }

    // below_fraction and mean elevation.
    {
        const HypsometryQuantiles linear{0.0F, 10.0F, 20.0F, 30.0F, 40.0F,
                                         50.0F, 60.0F, 70.0F, 80.0F};
        PLANETSIM_EXPECT(test, planetsim::below_fraction(linear, -1.0) == 0.0);
        PLANETSIM_EXPECT(test, planetsim::below_fraction(linear, 0.0) == 0.0);
        PLANETSIM_EXPECT_NEAR(test, planetsim::below_fraction(linear, 25.0), 25.0 / 80.0, 1e-15);
        PLANETSIM_EXPECT(test, planetsim::below_fraction(linear, 80.0) == 1.0);
        PLANETSIM_EXPECT(test, planetsim::below_fraction(linear, 1e9) == 1.0);
        PLANETSIM_EXPECT_NEAR(test, planetsim::mean_elevation_m(linear), 40.0, 1e-12);
        PLANETSIM_EXPECT_NEAR(test, planetsim::mean_elevation_above_m(linear, -1.0), 40.0,
                              1e-12);
        PLANETSIM_EXPECT_NEAR(test, planetsim::mean_elevation_above_m(linear, 20.0), 50.0,
                              1e-12);
        PLANETSIM_EXPECT_NEAR(test, planetsim::mean_elevation_above_m(linear, 25.0), 52.5,
                              1e-12);
        PLANETSIM_EXPECT_NEAR(test, planetsim::mean_elevation_above_m(linear, 80.0), 80.0,
                              1e-12);
        PLANETSIM_EXPECT_NEAR(test, planetsim::mean_elevation_above_m(linear, 1e9), 80.0,
                              1e-12);

        // Flat segments (a plateau and a flat floor) stay continuous and monotone.
        const HypsometryQuantiles stepped{-100.0F, -100.0F, -100.0F, 0.0F, 0.0F,
                                          0.0F,    0.0F,    50.0F,   50.0F};
        double previous = 0.0;
        for (double level = -150.0; level <= 100.0; level += 0.5) {
            const double fraction = planetsim::below_fraction(stepped, level);
            PLANETSIM_EXPECT(test, fraction >= previous && fraction >= 0.0 && fraction <= 1.0);
            // Continuous away from the flat stretches at -100, 0 and 50 m, where
            // 2/8, 3/8 and 1/8 of the area flood at once. The steepest sloped
            // segment rises 1/8 over 50 m.
            const bool near_plateau = std::abs(level + 100.0) <= 1.0 || std::abs(level) <= 1.0 ||
                                      std::abs(level - 50.0) <= 1.0;
            if (!near_plateau) {
                PLANETSIM_EXPECT(test, fraction - previous <= 1.0 / 8.0 * 0.5 / 50.0 + 1e-12);
            }
            previous = fraction;
        }
        PLANETSIM_EXPECT_NEAR(test, planetsim::below_fraction(stepped, -50.0), 2.5 / 8.0, 1e-15);
        PLANETSIM_EXPECT_NEAR(test, planetsim::below_fraction(stepped, 0.0), 3.0 / 8.0, 1e-15);
        PLANETSIM_EXPECT_NEAR(test, planetsim::below_fraction(stepped, 1e-9), 6.0 / 8.0, 1e-9);
        PLANETSIM_EXPECT_NEAR(test, planetsim::below_fraction(stepped, 50.0), 7.0 / 8.0, 1e-15);
        PLANETSIM_EXPECT(test, planetsim::below_fraction(stepped, 50.001) == 1.0);
        PLANETSIM_EXPECT(test, planetsim::below_fraction(linear, 80.0) == 1.0);
        PLANETSIM_EXPECT_NEAR(test, planetsim::below_fraction(stepped, 1.0), 6.0 / 8.0 + 1.0 / 400.0,
                              1e-15);
        PLANETSIM_EXPECT_NEAR(test, planetsim::mean_elevation_above_m(stepped, -50.0),
                              125.0 / 11.0, 1e-12);
        PLANETSIM_EXPECT_NEAR(test, planetsim::mean_elevation_above_m(stepped, 0.0), 15.0,
                              1e-12);

        const HypsometryQuantiles flat{-42.0F, -42.0F, -42.0F, -42.0F, -42.0F,
                                       -42.0F, -42.0F, -42.0F, -42.0F};
        PLANETSIM_EXPECT_NEAR(test, planetsim::mean_elevation_above_m(flat, -42.0), -42.0,
                              1e-12);
        PLANETSIM_EXPECT_NEAR(test, planetsim::mean_elevation_above_m(flat, -41.0), -42.0,
                              1e-12);
    }

    // Sampling on a mesh: the fan-triangle weights close on each cell's area;
    // a flat planet with no sub-cell roughness gives flat cells; a generated
    // planet gives finite, non-decreasing quantiles (ADR-0005 V1, A7).
    const planetsim::PlanetMesh mesh = planetsim::make_icosphere(3, 6'371'000.0);
    {
        double worst = 0.0;
        for (const auto& cell : mesh.cells()) {
            const auto corners = mesh.cell_corners(cell.id);
            double area = 0.0;
            for (std::size_t k = 0; k < corners.size(); ++k) {
                area += planetsim::spherical_triangle_area_unit(
                    cell.center_unit, mesh.corners_unit()[corners[k]],
                    mesh.corners_unit()[corners[(k + 1U) % corners.size()]]);
            }
            const double relative =
                std::abs(area * mesh.radius_m() * mesh.radius_m() - cell.area_m2) / cell.area_m2;
            worst = std::max(worst, relative);
        }
        PLANETSIM_EXPECT(test, worst < 1e-12);
    }
    {
        planetsim::GeologyState flat;
        flat.structural_elevation_m = planetsim::Field2D<float>(mesh.cell_count(), 123.0F);
        flat.crust_type = planetsim::Field2D<planetsim::CrustType>(mesh.cell_count(),
                                                                   planetsim::CrustType::oceanic);
        planetsim::GeologyParameters smooth;
        smooth.oceanic_subcell_roughness_amplitude_m = 0.0;
        planetsim::Field3D<float> hypsometry(planetsim::hypsometry_quantile_count, mesh.cell_count());
        planetsim::sample_hypsometry(mesh, 1U, smooth, flat, hypsometry);
        for (std::size_t level = 0; level < planetsim::hypsometry_quantile_count; ++level) {
            for (const float value : hypsometry.layer(level)) {
                PLANETSIM_EXPECT_NEAR(test, value, 123.0, 1e-4);
            }
        }
    }
    {
        const planetsim::GeologyParameters parameters;
        const planetsim::GeologyState geology = planetsim::generate_geology(mesh, 5U, parameters);
        planetsim::Field3D<float> hypsometry(planetsim::hypsometry_quantile_count, mesh.cell_count());
        planetsim::sample_hypsometry(mesh, 5U, parameters, geology, hypsometry);
        double worst_offset = 0.0;
        for (const auto& cell : mesh.cells()) {
            const HypsometryQuantiles quantiles = planetsim::cell_hypsometry(hypsometry, cell.id);
            PLANETSIM_EXPECT(test, non_decreasing(quantiles));
            PLANETSIM_EXPECT(test, std::all_of(quantiles.begin(), quantiles.end(),
                                               [](float value) { return std::isfinite(value); }));
            PLANETSIM_EXPECT(test, quantiles[8] > quantiles[0]);
            worst_offset = std::max(
                worst_offset, std::abs(planetsim::mean_elevation_m(quantiles) -
                                       static_cast<double>(geology.structural_elevation_m[cell.id])));
        }
        std::cout << "max |mean hypsometry - structural elevation| at L3: " << worst_offset
                  << " m\n";
    }
    {
        planetsim::GeologyState missing;
        planetsim::Field3D<float> hypsometry(planetsim::hypsometry_quantile_count, mesh.cell_count());
        PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                                planetsim::sample_hypsometry(mesh, 1U, planetsim::GeologyParameters{},
                                                             missing, hypsometry));
    }

    return test.result();
}
