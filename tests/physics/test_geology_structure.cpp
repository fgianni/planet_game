#include "sim/planet/geology/crust.hpp"
#include "sim/planet/geology/geology_generator.hpp"
#include "sim/planet/geology/structural_elevation.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using planetsim::BoundaryClass;
using planetsim::CrustType;
using planetsim::PlateBoundaryEdge;

struct AreaMean {
    double weighted_sum = 0.0;
    double area = 0.0;

    void add(double value, double cell_area) {
        weighted_sum += value * cell_area;
        area += cell_area;
    }
    [[nodiscard]] double mean() const { return area > 0.0 ? weighted_sum / area : 0.0; }
};

[[nodiscard]] bool continent_continent(const PlateBoundaryEdge& boundary) {
    return boundary.first_crust == CrustType::continental &&
           boundary.second_crust == CrustType::continental;
}

[[nodiscard]] bool ocean_continent(const PlateBoundaryEdge& boundary) {
    return boundary.first_crust != boundary.second_crust;
}

void check_age_depth(planetsim::test::Context& test) {
    const planetsim::GeologyParameters parameters;
    const double myr = planetsim::seconds_per_million_years;
    PLANETSIM_EXPECT_NEAR(test, planetsim::oceanic_depth_m(0.0, parameters), 2'500.0, 1e-9);
    PLANETSIM_EXPECT_NEAR(test, planetsim::oceanic_depth_m(25.0 * myr, parameters),
                          2'500.0 + 350.0 * 5.0, 1e-6);
    // Continuous and C1 at the 70 Myr transition, flattening towards 6,400 m.
    const double t = 70.0 * myr;
    const double h = 1e-3 * myr;
    PLANETSIM_EXPECT_NEAR(test, planetsim::oceanic_depth_m(t - h, parameters),
                          planetsim::oceanic_depth_m(t + h, parameters), 0.1);
    const double slope_before = (planetsim::oceanic_depth_m(t, parameters) -
                                 planetsim::oceanic_depth_m(t - h, parameters)) / h;
    const double slope_after = (planetsim::oceanic_depth_m(t + h, parameters) -
                                planetsim::oceanic_depth_m(t, parameters)) / h;
    PLANETSIM_EXPECT(test, std::abs(slope_before - slope_after) < 1e-3 * std::abs(slope_before));
    double previous = 0.0;
    for (double age_myr = 0.0; age_myr <= 400.0; age_myr += 5.0) {
        const double depth = planetsim::oceanic_depth_m(age_myr * myr, parameters);
        PLANETSIM_EXPECT(test, depth >= previous && depth < 6'400.0);
        previous = depth;
    }
    std::cout << "depth_at_180_myr_m: " << planetsim::oceanic_depth_m(180.0 * myr, parameters)
              << '\n';
}

// A6: continental area and oceanic age.
void check_crust(planetsim::test::Context& test, const planetsim::PlanetMesh& mesh,
                 std::uint64_t seed) {
    const planetsim::GeologyParameters parameters;
    planetsim::GeologyState geology = planetsim::generate_geology(mesh, seed, parameters, 4U);

    double sphere_area = 0.0;
    double continental_area = 0.0;
    double largest_cell = 0.0;
    for (const auto& cell : mesh.cells()) {
        sphere_area += cell.area_m2;
        largest_cell = std::max(largest_cell, cell.area_m2);
        if (geology.crust_type[cell.id] == CrustType::continental) {
            continental_area += cell.area_m2;
        }
    }
    const double target = parameters.continental_area_fraction * sphere_area;
    std::cout << "L" << mesh.subdivision() << " seed " << seed
              << " continental_fraction: " << continental_area / sphere_area
              << " excess_in_cells: " << (continental_area - target) / largest_cell << '\n';
    PLANETSIM_EXPECT(test, continental_area >= target && continental_area - target <= largest_cell);

    // Re-run the age assignment to inspect its spreading paths.
    const auto spreading = planetsim::assign_crust_age(mesh, seed, parameters, geology);
    float minimum_age = std::numeric_limits<float>::max();
    float maximum_age = 0.0F;
    for (std::size_t index = 0; index < mesh.cell_count(); ++index) {
        if (geology.crust_type[index] != CrustType::oceanic) {
            continue;
        }
        minimum_age = std::min(minimum_age, geology.crust_age_s[index]);
        maximum_age = std::max(maximum_age, geology.crust_age_s[index]);
        const planetsim::CellId parent = spreading.predecessor[index];
        if (parent.is_valid() && geology.crust_type[parent] == CrustType::oceanic) {
            PLANETSIM_EXPECT(test, geology.crust_age_s[parent] <= geology.crust_age_s[index]);
            PLANETSIM_EXPECT(test, spreading.label[parent.to_index()] == spreading.label[index]);
        }
    }
    for (const auto& boundary : geology.boundaries) {
        if (boundary.boundary_class != BoundaryClass::divergent) {
            continue;
        }
        for (const planetsim::CellId cell : {boundary.first_cell, boundary.second_cell}) {
            if (geology.crust_type[cell] == CrustType::oceanic) {
                PLANETSIM_EXPECT(test, geology.crust_age_s[cell] == 0.0F);
            }
        }
    }
    const double myr = planetsim::seconds_per_million_years;
    std::cout << "  oceanic_age_range_myr: " << minimum_age / myr << " -- " << maximum_age / myr
              << '\n';
    PLANETSIM_EXPECT(test, maximum_age <= static_cast<float>(parameters.oceanic_age_cap_s));
    for (std::size_t index = 0; index < mesh.cell_count(); ++index) {
        PLANETSIM_EXPECT(test, std::isfinite(geology.structural_elevation_m[index]));
    }
}

// A5: structures correlate with boundaries (L5, earth_like).
void check_structures(planetsim::test::Context& test, const planetsim::PlanetMesh& mesh,
                      std::uint64_t seed) {
    const planetsim::GeologyParameters parameters;
    const planetsim::GeologyState geology = planetsim::generate_geology(mesh, seed, parameters, 4U);
    const auto& elevation = geology.structural_elevation_m;

    const auto collision = planetsim::along_mesh_distance(
        mesh, planetsim::boundary_sources(geology, [](const PlateBoundaryEdge& boundary) {
            return boundary.boundary_class == BoundaryClass::convergent &&
                   continent_continent(boundary);
        }));
    const auto subduction = planetsim::along_mesh_distance(
        mesh, planetsim::boundary_sources(geology, [](const PlateBoundaryEdge& boundary) {
            return boundary.boundary_class == BoundaryClass::convergent && ocean_continent(boundary);
        }));
    const auto spreading = planetsim::along_mesh_distance(
        mesh, planetsim::boundary_sources(geology, [](const PlateBoundaryEdge& boundary) {
            return boundary.boundary_class == BoundaryClass::divergent;
        }));

    AreaMean continental;
    AreaMean near_collision;
    AreaMean near_ridge;
    AreaMean abyssal;
    double subduction_minimum = std::numeric_limits<double>::infinity();
    for (const auto& cell : mesh.cells()) {
        const std::size_t index = cell.id.to_index();
        const double value = elevation[index];
        const bool oceanic = geology.crust_type[index] == CrustType::oceanic;
        if (!oceanic) {
            continental.add(value, cell.area_m2);
        }
        if (collision.cost[index] <= 300'000.0) {
            near_collision.add(value, cell.area_m2);
        }
        if (subduction.cost[index] <= 100'000.0) {
            subduction_minimum = std::min(subduction_minimum, value);
        }
        if (oceanic && spreading.cost[index] <= 200'000.0) {
            near_ridge.add(value, cell.area_m2);
        }
        if (oceanic && spreading.cost[index] > 1'000'000.0) {
            abyssal.add(value, cell.area_m2);
        }
    }
    std::cout << "A5 L" << mesh.subdivision() << " seed " << seed
              << ": continental_mean_m=" << continental.mean()
              << " near_collision_mean_m=" << near_collision.mean()
              << " (excess " << near_collision.mean() - continental.mean() << ")"
              << " subduction_min_m=" << subduction_minimum
              << " abyssal_mean_m=" << abyssal.mean()
              << " (trench depth below abyssal " << abyssal.mean() - subduction_minimum << ")"
              << " near_ridge_mean_m=" << near_ridge.mean()
              << " (ridge height " << near_ridge.mean() - abyssal.mean() << ")\n";
    if (near_collision.area > 0.0) {
        PLANETSIM_EXPECT(test, near_collision.mean() - continental.mean() >= 1'000.0);
    } else {
        std::cout << "  no continent-continent convergent boundary for this seed\n";
    }
    if (std::isfinite(subduction_minimum)) {
        PLANETSIM_EXPECT(test, abyssal.mean() - subduction_minimum >= 1'000.0);
    }
    PLANETSIM_EXPECT(test, near_ridge.mean() - abyssal.mean() >= 1'000.0);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    check_age_depth(test);

    const planetsim::PlanetMesh l4 = planetsim::make_icosphere(4, 6'371'000.0);
    const planetsim::PlanetMesh l5 = planetsim::make_icosphere(5, 6'371'000.0);
    for (const std::uint64_t seed : {1ULL, 42ULL, 0xC0FFEEULL}) {
        check_crust(test, l4, seed);
        check_crust(test, l5, seed);
        check_structures(test, l5, seed);
    }
    return test.result();
}
