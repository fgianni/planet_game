#include "sim/presentation/terrain_relief.hpp"

#include "sim/core/random/counter_rng.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "tests/test_support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace {

using planetsim::CellId;
using planetsim::Vec3d;
using planetsim::presentation::TerrainRelief;

constexpr std::uint32_t subdivision = 4U;

struct Fixture {
    std::shared_ptr<const planetsim::PlanetMesh> mesh;
    planetsim::TerrainSnapshot terrain;
};

[[nodiscard]] Fixture make_fixture() {
    planetsim::Scenario scenario;
    scenario.subdivision = subdivision;
    const planetsim::PlanetRun run(scenario);
    Fixture fixture;
    fixture.mesh = std::make_shared<const planetsim::PlanetMesh>(
        planetsim::make_icosphere(subdivision, run.parameters().radius_m));
    fixture.terrain = run.terrain_snapshot();
    return fixture;
}

// A point spread uniformly by area over the cell's fan of triangles, from
// the validation stream (independent of the points F_cell is built from).
[[nodiscard]] Vec3d point_in_cell(const planetsim::PlanetMesh& mesh, CellId cell, std::uint32_t index,
                                  const std::vector<double>& cumulative_area) {
    const auto corners = mesh.cell_corners(cell);
    const auto draw = [&](std::uint32_t sample) {
        return planetsim::keyed_random_unit_double(20260928U, planetsim::RandomStreamId::validation, 0,
                                                   cell.value(), 3U * index + sample);
    };
    const double pick = draw(0U) * cumulative_area.back();
    const auto k = static_cast<std::size_t>(
        std::upper_bound(cumulative_area.begin(), cumulative_area.end(), pick) - cumulative_area.begin());
    double u = draw(1U);
    double v = draw(2U);
    if (u + v > 1.0) {
        u = 1.0 - u;
        v = 1.0 - v;
    }
    const Vec3d center = mesh.cell(cell).center_unit;
    const Vec3d a = mesh.corners_unit()[corners[std::min(k, corners.size() - 1U)]];
    const Vec3d b = mesh.corners_unit()[corners[(std::min(k, corners.size() - 1U) + 1U) % corners.size()]];
    return planetsim::normalized(center * (1.0 - u - v) + a * u + b * v);
}

[[nodiscard]] std::vector<double> fan_cumulative_area(const planetsim::PlanetMesh& mesh, CellId cell) {
    const auto corners = mesh.cell_corners(cell);
    const Vec3d center = mesh.cell(cell).center_unit;
    std::vector<double> cumulative;
    double total = 0.0;
    for (std::size_t k = 0; k < corners.size(); ++k) {
        const Vec3d a = mesh.corners_unit()[corners[k]];
        const Vec3d b = mesh.corners_unit()[corners[(k + 1U) % corners.size()]];
        total += 0.5 * planetsim::length(planetsim::cross(a - center, b - center));
        cumulative.push_back(total);
    }
    return cumulative;
}

// V4 (ADR-0022): over points sampled across the whole cell, edge blends
// included, each of the nine quantiles of the generated elevation is within
// 2 % of the cell's elevation range. Quantiles 1-7 are read in elevation;
// the extremes (the cell's minimum and maximum, which a single point moves)
// as the share of the cell beyond them by more than 2 % of the range. The
// whole curve is also read by area: the share below each quantile within
// 2 % of its rank.
void check_hypsometry(planetsim::test::Context& test, const Fixture& fixture, const TerrainRelief& relief) {
    constexpr std::uint32_t samples = 40000U;
    std::size_t checked = 0U;
    std::size_t failed = 0U;
    double worst = 0.0;
    double error_sum = 0.0;
    double worst_outside = 0.0;
    double rank_worst = 0.0;
    double rank_sum = 0.0;
    std::size_t rank_failed = 0U;
    double outside_sum = 0.0;
    for (std::size_t index = 0; index < fixture.mesh->cell_count(); index += 7U) {
        const CellId cell{static_cast<std::uint32_t>(index)};
        const float* q = fixture.terrain.hypsometry_m.data() + index * planetsim::hypsometry_quantile_count;
        const double range = static_cast<double>(q[planetsim::hypsometry_quantile_count - 1U] - q[0]);
        if (range < 1.0) continue; // a flat cell has nothing to match
        const auto cumulative = fan_cumulative_area(*fixture.mesh, cell);
        std::vector<double> heights;
        heights.reserve(samples);
        CellId hint = cell;
        for (std::uint32_t i = 0; i < samples; ++i) {
            const auto sample = relief.sample(point_in_cell(*fixture.mesh, cell, i, cumulative), hint);
            hint = sample.cell;
            heights.push_back(sample.elevation_m);
        }
        std::sort(heights.begin(), heights.end());
        double cell_worst = 0.0;
        for (std::size_t k = 1; k + 1U < planetsim::hypsometry_quantile_count; ++k) {
            const double position = static_cast<double>(k) / 8.0 * static_cast<double>(samples - 1U);
            const double measured = heights[static_cast<std::size_t>(std::lround(position))];
            cell_worst = std::max(cell_worst, std::abs(measured - static_cast<double>(q[k])) / range);
        }
        // The extremes are the cell's minimum and maximum: measured as the
        // share of its area beyond them by more than 2 % of the range.
        const double low = static_cast<double>(q[0]) - 0.02 * range;
        const double high = static_cast<double>(q[8]) + 0.02 * range;
        const auto outside = static_cast<double>(
            (std::lower_bound(heights.begin(), heights.end(), low) - heights.begin()) +
            (heights.end() - std::upper_bound(heights.begin(), heights.end(), high)));
        const double outside_share = outside / static_cast<double>(samples);
        // The same curve read by area: the share of the cell below each
        // quantile against its rank.
        double cell_rank_worst = 0.0;
        for (std::size_t k = 1; k + 1U < planetsim::hypsometry_quantile_count; ++k) {
            const auto below = static_cast<double>(
                std::lower_bound(heights.begin(), heights.end(), static_cast<double>(q[k])) - heights.begin());
            cell_rank_worst = std::max(cell_rank_worst,
                                       std::abs(below / static_cast<double>(samples) - static_cast<double>(k) / 8.0));
        }
        rank_worst = std::max(rank_worst, cell_rank_worst);
        rank_sum += cell_rank_worst;
        if (cell_rank_worst > 0.02 || outside_share > 0.02) ++rank_failed;
        ++checked;
        error_sum += cell_worst;
        worst = std::max(worst, cell_worst);
        worst_outside = std::max(worst_outside, outside_share);
        outside_sum += outside_share;
        if (cell_worst > 0.02 || outside_share > 0.02) ++failed;
    }
    std::cout << "V4: " << checked << " cells; quantiles 1-7: worst error " << worst
              << " of the cell range, mean " << error_sum / static_cast<double>(checked)
              << "; area beyond the extremes: worst " << worst_outside << ", mean "
              << outside_sum / static_cast<double>(checked) << "; " << failed << " cells over 2 %\n"
              << "V4 by area: quantiles 1-7 worst " << rank_worst << " of the cell's area, mean "
              << rank_sum / static_cast<double>(checked) << "; " << rank_failed << " cells over 2 %\n";
    PLANETSIM_EXPECT(test, checked > 100U);
    PLANETSIM_EXPECT(test, failed == 0U);
    PLANETSIM_EXPECT(test, rank_failed == 0U);
}

// V7: the same position gives the same terrain from any hint and from
// another instance built from the same inputs.
void check_determinism(planetsim::test::Context& test, const Fixture& fixture, const TerrainRelief& relief) {
    const TerrainRelief again(fixture.mesh, fixture.terrain, 20260928U);
    const TerrainRelief other_world(fixture.mesh, fixture.terrain, 20260929U);
    bool same = true;
    std::size_t differs_between_worlds = 0U;
    for (std::uint32_t i = 0; i < 2000U; ++i) {
        const Vec3d point = planetsim::normalized(Vec3d{
            planetsim::keyed_random_unit_double(1U, planetsim::RandomStreamId::validation, 0, i, 0U) - 0.5,
            planetsim::keyed_random_unit_double(1U, planetsim::RandomStreamId::validation, 0, i, 1U) - 0.5,
            planetsim::keyed_random_unit_double(1U, planetsim::RandomStreamId::validation, 0, i, 2U) - 0.5});
        const auto a = relief.sample(point, CellId{0});
        const auto b = again.sample(point, CellId{static_cast<std::uint32_t>((i * 37U) % fixture.mesh->cell_count())});
        same = same && a.elevation_m == b.elevation_m && a.rank == b.rank && a.cell == b.cell;
        if (other_world.sample(point).elevation_m != a.elevation_m) ++differs_between_worlds;
    }
    PLANETSIM_EXPECT(test, same);
    PLANETSIM_EXPECT(test, differs_between_worlds > 1000U);
}

// The surface is continuous: across every sampled edge, two points a
// millionth of a cell apart differ by a tiny share of the cells' range.
void check_continuity(planetsim::test::Context& test, const Fixture& fixture, const TerrainRelief& relief) {
    const double step = 1e-6 * relief.cell_spacing_rad();
    double worst = 0.0;
    for (std::size_t index = 0; index < fixture.mesh->edge_count(); index += 5U) {
        const auto& edge = fixture.mesh->edge(planetsim::EdgeId{static_cast<std::uint32_t>(index)});
        const Vec3d a = fixture.mesh->cell(edge.first_cell).center_unit;
        const Vec3d b = fixture.mesh->cell(edge.second_cell).center_unit;
        const Vec3d middle = planetsim::normalized(a + b);
        const Vec3d across = planetsim::normalized(b - a);
        const auto left = relief.sample(planetsim::normalized(middle - across * step), edge.first_cell);
        const auto right = relief.sample(planetsim::normalized(middle + across * step), edge.second_cell);
        const float* qa = fixture.terrain.hypsometry_m.data() + edge.first_cell.to_index() * 9U;
        const float* qb = fixture.terrain.hypsometry_m.data() + edge.second_cell.to_index() * 9U;
        const double range = std::max({1.0, static_cast<double>(qa[8] - qa[0]),
                                       static_cast<double>(qb[8] - qb[0]),
                                       static_cast<double>(std::abs(qa[4] - qb[4]))});
        worst = std::max(worst, std::abs(left.elevation_m - right.elevation_m) / range);
    }
    std::cout << "continuity: worst jump across an edge " << worst << " of the range\n";
    PLANETSIM_EXPECT(test, worst < 1e-3);
}

// A schema-2 terrain has no hypsometry: every cell is flat at its mean.
void check_absent_hypsometry(planetsim::test::Context& test, const Fixture& fixture) {
    planetsim::TerrainSnapshot old = fixture.terrain;
    old.hypsometry_m.clear();
    const TerrainRelief relief(fixture.mesh, old, 20260928U);
    bool flat = true;
    for (std::size_t index = 0; index < fixture.mesh->cell_count(); index += 11U) {
        const CellId cell{static_cast<std::uint32_t>(index)};
        const auto sample = relief.sample(fixture.mesh->cell(cell).center_unit, cell);
        flat = flat && std::abs(sample.elevation_m - old.mean_elevation_m[index]) < 1e-3;
    }
    PLANETSIM_EXPECT(test, flat);
}

void check_speed(const TerrainRelief& relief) {
    const auto begin = std::chrono::steady_clock::now();
    CellId hint{0};
    double sum = 0.0;
    constexpr int count = 200000;
    for (int i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / count;
        const auto sample = relief.sample(planetsim::normalized(Vec3d{std::cos(t * 0.3), std::sin(t * 0.3), 0.2}), hint);
        hint = sample.cell;
        sum += sample.elevation_m;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::cout << "terrain relief: " << count / seconds / 1e6 << " M samples/s along a path (checksum "
              << sum << ")\n";
}

}  // namespace

int main() {
    planetsim::test::Context test;
    const Fixture fixture = make_fixture();
    PLANETSIM_EXPECT(test, fixture.terrain.hypsometry_m.size() ==
                               fixture.mesh->cell_count() * planetsim::hypsometry_quantile_count);
    const TerrainRelief relief(fixture.mesh, fixture.terrain, 20260928U);
    check_hypsometry(test, fixture, relief);
    check_determinism(test, fixture, relief);
    check_continuity(test, fixture, relief);
    check_absent_hypsometry(test, fixture);
    check_speed(relief);
    return test.result();
}
