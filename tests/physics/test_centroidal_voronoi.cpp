#include "sim/planet/mesh/icosphere.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

[[nodiscard]] double angle_between(const planetsim::Vec3d& first, const planetsim::Vec3d& second) {
    return std::acos(std::clamp(planetsim::dot(first, second), -1.0, 1.0));
}

// ADR-0002 §4.1: corners are circumcentres (equidistant from their three cell
// centres), so every edge normal points at the neighbouring centre, and Lloyd
// optimisation leaves each centre at its cell's area centroid.
void check_level(planetsim::test::Context& test, std::uint32_t level) {
    const auto mesh = planetsim::make_icosphere(level, 6'371'000.0);
    const auto corners = mesh.corners_unit();
    std::vector<double> min_corner_distance(mesh.corner_count(), 10.0);
    std::vector<double> max_corner_distance(mesh.corner_count(), -1.0);
    double max_normal_misalignment = 0.0;
    double max_centroid_offset_rad = 0.0;
    double spacing_sum_rad = 0.0;
    std::size_t spacing_count = 0;

    for (const auto& cell : mesh.cells()) {
        const auto cell_corners = mesh.cell_corners(cell.id);
        planetsim::Vec3d weighted_centroid;
        for (std::size_t local = 0; local < cell_corners.size(); ++local) {
            const auto corner = cell_corners[local];
            const double distance = angle_between(cell.center_unit, corners[corner]);
            min_corner_distance[corner] = std::min(min_corner_distance[corner], distance);
            max_corner_distance[corner] = std::max(max_corner_distance[corner], distance);

            const auto& first = corners[corner];
            const auto& second = corners[cell_corners[(local + 1U) % cell_corners.size()]];
            const double numerator =
                std::abs(planetsim::dot(cell.center_unit, planetsim::cross(first, second)));
            const double denominator = 1.0 + planetsim::dot(cell.center_unit, first) +
                                       planetsim::dot(first, second) +
                                       planetsim::dot(second, cell.center_unit);
            weighted_centroid =
                weighted_centroid + planetsim::normalized(cell.center_unit + first + second) *
                                        (2.0 * std::atan2(numerator, denominator));
        }
        max_centroid_offset_rad =
            std::max(max_centroid_offset_rad,
                     angle_between(planetsim::normalized(weighted_centroid), cell.center_unit));

        for (const auto& cell_edge : mesh.cell_edges(cell.id)) {
            const auto& neighbor = mesh.cell(cell_edge.neighbor).center_unit;
            const auto toward = planetsim::normalized(
                neighbor - cell.center_unit * planetsim::dot(neighbor, cell.center_unit));
            const auto normal =
                cell.east_unit * static_cast<double>(cell_edge.outward_normal_east) +
                cell.north_unit * static_cast<double>(cell_edge.outward_normal_north);
            max_normal_misalignment =
                std::max(max_normal_misalignment, 1.0 - planetsim::dot(toward, normal));
            spacing_sum_rad += angle_between(cell.center_unit, neighbor);
            ++spacing_count;
        }
    }

    double max_corner_spread = 0.0;
    for (std::size_t corner = 0; corner < mesh.corner_count(); ++corner) {
        max_corner_spread =
            std::max(max_corner_spread, max_corner_distance[corner] - min_corner_distance[corner]);
    }
    const double mean_spacing_rad = spacing_sum_rad / static_cast<double>(spacing_count);

    PLANETSIM_EXPECT(test, max_corner_spread <= 1.0e-12);
    PLANETSIM_EXPECT(test, max_normal_misalignment <= 1.0e-6);
    PLANETSIM_EXPECT(test, max_centroid_offset_rad <= 1.0e-3 * mean_spacing_rad);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    for (std::uint32_t level = 0; level <= 6U; ++level) {
        check_level(test, level);
    }
    return test.result();
}
