#include "sim/planet/geology/plates.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr double pi_4 = 4.0 * 3.14159265358979323846;

// A3 and A4 for one planet.
void check_plates(planetsim::test::Context& test, const planetsim::PlanetMesh& mesh,
                  std::uint64_t seed, std::uint32_t plate_count) {
    planetsim::GeologyParameters parameters;
    parameters.plate_count = plate_count;
    planetsim::GeologyState geology;
    planetsim::generate_plates(mesh, seed, parameters, geology, 4U);

    const double sphere_area = pi_4 * mesh.radius_m() * mesh.radius_m();
    PLANETSIM_EXPECT(test, geology.plates.size() == plate_count);

    // Every cell assigned; every plate one connected region of >= 0.5 %.
    std::uint32_t total_cells = 0;
    double smallest_fraction = 1.0;
    for (std::uint32_t plate = 0; plate < plate_count; ++plate) {
        const auto& tectonic = geology.plates[plate];
        PLANETSIM_EXPECT(test, geology.plate_id[tectonic.seed_cell].value() == plate);
        std::vector<std::uint8_t> visited(mesh.cell_count(), 0U);
        std::vector<planetsim::CellId> stack{tectonic.seed_cell};
        visited[tectonic.seed_cell.to_index()] = 1U;
        std::uint32_t reached = 0;
        while (!stack.empty()) {
            const planetsim::CellId cell = stack.back();
            stack.pop_back();
            ++reached;
            for (const auto& edge : mesh.cell_edges(cell)) {
                const std::size_t neighbor = edge.neighbor.to_index();
                if (visited[neighbor] == 0U && geology.plate_id[neighbor].value() == plate) {
                    visited[neighbor] = 1U;
                    stack.push_back(edge.neighbor);
                }
            }
        }
        PLANETSIM_EXPECT(test, reached == tectonic.cell_count);
        total_cells += tectonic.cell_count;
        smallest_fraction = std::min(smallest_fraction, tectonic.area_m2 / sphere_area);

        // Euler-pole equatorial speed within 1--10 cm/yr.
        const double speed_cm_yr = tectonic.angular_speed_rad_s * mesh.radius_m() * 100.0 *
                                   planetsim::seconds_per_julian_year;
        PLANETSIM_EXPECT(test, speed_cm_yr >= 1.0 && speed_cm_yr <= 10.0);
    }
    PLANETSIM_EXPECT(test, total_cells == mesh.cell_count());
    for (std::size_t index = 0; index < mesh.cell_count(); ++index) {
        PLANETSIM_EXPECT(test, geology.plate_id[index].is_valid());
    }
    std::cout << "L" << mesh.subdivision() << " plates=" << plate_count
              << " smallest_plate_fraction: " << smallest_fraction << '\n';
    PLANETSIM_EXPECT(test, smallest_fraction >= 0.005);

    // Every cross-plate edge classified, and antisymmetric relative motion.
    const auto boundaries = planetsim::classify_plate_boundaries(mesh, geology);
    std::size_t cross_plate_edges = 0;
    for (const auto& edge : mesh.edges()) {
        if (geology.plate_id[edge.first_cell] != geology.plate_id[edge.second_cell]) {
            ++cross_plate_edges;
        }
    }
    PLANETSIM_EXPECT(test, boundaries.size() == cross_plate_edges);
    std::size_t counts[4] = {0, 0, 0, 0};
    double worst_antisymmetry = 0.0;
    for (const auto& boundary : boundaries) {
        PLANETSIM_EXPECT(test, boundary.boundary_class != planetsim::BoundaryClass::none);
        ++counts[static_cast<int>(boundary.boundary_class)];
        const auto forward = planetsim::plate_relative_motion(mesh, geology, boundary.first_cell,
                                                              boundary.second_cell);
        const auto backward = planetsim::plate_relative_motion(mesh, geology, boundary.second_cell,
                                                               boundary.first_cell);
        const planetsim::Vec3d sum = forward.relative_velocity + backward.relative_velocity;
        const double scale = planetsim::length(forward.relative_velocity);
        if (scale > 0.0) {
            worst_antisymmetry = std::max(worst_antisymmetry, planetsim::length(sum) / scale);
            PLANETSIM_EXPECT(test, std::abs(forward.convergence_m_s - backward.convergence_m_s) <=
                                       1e-12 * scale);
            PLANETSIM_EXPECT(test, std::abs(forward.tangential_m_s - backward.tangential_m_s) <=
                                       1e-12 * scale);
        }
        PLANETSIM_EXPECT(test, planetsim::classify_boundary(backward.convergence_m_s,
                                                            backward.tangential_m_s) ==
                                   boundary.boundary_class);
    }
    std::cout << "  boundaries convergent=" << counts[1] << " divergent=" << counts[2]
              << " transform=" << counts[3] << " worst_antisymmetry=" << worst_antisymmetry
              << '\n';
    PLANETSIM_EXPECT(test, worst_antisymmetry <= 1e-12);
}

}  // namespace

int main() {
    planetsim::test::Context test;

    PLANETSIM_EXPECT(test, planetsim::classify_boundary(2.0, 1.0) ==
                               planetsim::BoundaryClass::convergent);
    PLANETSIM_EXPECT(test, planetsim::classify_boundary(-2.0, 2.0) ==
                               planetsim::BoundaryClass::divergent);
    PLANETSIM_EXPECT(test, planetsim::classify_boundary(1.0, -3.0) ==
                               planetsim::BoundaryClass::transform);
    PLANETSIM_EXPECT(test, planetsim::classify_boundary(0.0, 0.0) ==
                               planetsim::BoundaryClass::transform);

    const planetsim::PlanetMesh l4 = planetsim::make_icosphere(4, 6'371'000.0);
    const planetsim::PlanetMesh l5 = planetsim::make_icosphere(5, 6'371'000.0);
    for (const std::uint64_t seed : {1ULL, 42ULL, 0xC0FFEEULL}) {
        check_plates(test, l4, seed, 12U);
        check_plates(test, l5, seed, 12U);
    }
    check_plates(test, l4, 7ULL, 2U);
    check_plates(test, l5, 7ULL, 40U);

    // Seed cells are distinct and deterministic.
    const auto seeds = planetsim::choose_plate_seed_cells(l4, 99ULL, 40U, 0.5);
    const auto again = planetsim::choose_plate_seed_cells(l4, 99ULL, 40U, 0.5);
    PLANETSIM_EXPECT(test, seeds == again);
    auto sorted = seeds;
    std::sort(sorted.begin(), sorted.end());
    PLANETSIM_EXPECT(test, std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
    const double spacing_rad = 0.5 * std::sqrt(4.0 * 3.14159265358979323846 / 40.0);
    for (std::size_t first = 0; first < seeds.size(); ++first) {
        for (std::size_t second = first + 1U; second < seeds.size(); ++second) {
            const double alignment = planetsim::dot(l4.cell(seeds[first]).center_unit,
                                                    l4.cell(seeds[second]).center_unit);
            PLANETSIM_EXPECT(test, std::acos(std::clamp(alignment, -1.0, 1.0)) >= spacing_rad);
        }
    }
    // Spacing zero reduces to the original duplicate-skipping rule.
    const auto unspaced = planetsim::choose_plate_seed_cells(l4, 99ULL, 40U, 0.0);
    PLANETSIM_EXPECT(test, unspaced.size() == 40U);

    planetsim::GeologyParameters invalid;
    invalid.plate_count = 41U;
    planetsim::GeologyState geology;
    PLANETSIM_EXPECT_THROWS(test, std::invalid_argument,
                            planetsim::generate_plates(l4, 1ULL, invalid, geology));

    return test.result();
}
