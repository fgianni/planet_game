// Probe-only ADR-0030: canonical relief on a bounded set of real dual-cell
// fan triangles. Not a production mesh: the outer hand-back is not stitched.
// Build against build-godot/libPlanetSimPresentation.a and libPlanetSim.a,
// with C++20, -O3, -ffp-contract=off, -lzstd and -pthread.
// Usage: coast_cell_fan_spike 4|6 seed existing-patch-prefix [subdivisions]
// Writes existing-patch-prefix-fan.bin for probe_coast_render_mesh.gd.
#include "sim/planet/run/planet_run.hpp"
#include "sim/presentation/terrain_relief.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace planetsim;
using namespace planetsim::presentation;
using Vertex = std::array<float, 4>;
struct Point { Vec3d direction; double height_m; };

Point interpolate(const Point& a, const Point& b, double t) {
    return {normalized(a.direction * (1.0 - t) + b.direction * t),
            a.height_m * (1.0 - t) + b.height_m * t};
}

void emit(const std::array<Point, 3>& triangle, const Vec3d& focus, double radius_m,
          std::vector<Vertex>& vertices) {
    for (const bool wet : {true, false}) {
        std::vector<Point> polygon;
        for (int i = 0; i < 3; ++i) {
            const Point& a = triangle[static_cast<std::size_t>(i)];
            const Point& b = triangle[static_cast<std::size_t>((i + 1) % 3)];
            const bool inside_a = wet ? a.height_m <= 0.0 : a.height_m >= 0.0;
            const bool inside_b = wet ? b.height_m <= 0.0 : b.height_m >= 0.0;
            if (inside_a) polygon.push_back(a);
            if (inside_a != inside_b)
                polygon.push_back(interpolate(a, b, a.height_m / (a.height_m - b.height_m)));
        }
        for (std::size_t i = 2; i < polygon.size(); ++i)
            for (const Point& point : {polygon[0], polygon[i - 1], polygon[i]}) {
                const Vec3d position = point.direction *
                    (1.0 + (wet ? 0.0 : std::max(0.0, point.height_m) / radius_m)) - focus;
                vertices.push_back({static_cast<float>(position.x), static_cast<float>(position.y),
                                    static_cast<float>(position.z), wet ? 1.0F : 0.0F});
            }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) return 2;
    const unsigned level = static_cast<unsigned>(std::stoul(argv[1]));
    if (level != 4 && level != 6) return 2;
    const unsigned divisions = argc == 5 ? static_cast<unsigned>(std::stoul(argv[4])) : 16U;
    if (divisions < 2 || divisions > 64) return 2;
    const std::string prefix = argv[3];
    std::ifstream focus_file(prefix + "-focus.txt");
    Vec3d focus{};
    if (!(focus_file >> focus.x >> focus.y >> focus.z))
        throw std::runtime_error("missing patch focus");
    focus = normalized(focus);
    Scenario scenario;
    scenario.subdivision = level;
    scenario.seed = std::stoull(argv[2]);
    const PlanetRun planet(scenario);
    const auto mesh = std::make_shared<const PlanetMesh>(planet.state().mesh());
    const auto terrain = planet.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    const double radius = mesh->radius_m();
    // Select whole fan triangles by their actual cell/corner directions.
    // This is deliberately a bounded local footprint, not an L4/L6 global
    // fine mesh. Its selected/unselected outer edge is NOT yet continuous.
    constexpr double cap_rad = 0.10;
    const double cap_dot = std::cos(cap_rad);
    std::vector<Vertex> vertices;
    std::size_t fan_triangles = 0;
    std::size_t coast_triangles = 0;
    for (const auto& cell : mesh->cells()) {
        const auto corners = mesh->cell_corners(cell.id);
        for (std::size_t edge = 0; edge < corners.size(); ++edge) {
            const std::array<Vec3d, 3> fan = {
                cell.center_unit, mesh->corners_unit()[corners[edge]],
                mesh->corners_unit()[corners[(edge + 1) % corners.size()]]};
            if (std::max({dot(focus, fan[0]), dot(focus, fan[1]), dot(focus, fan[2])}) < cap_dot)
                continue;
            ++fan_triangles;
            const unsigned stride = divisions + 1;
            std::vector<Point> samples(static_cast<std::size_t>(stride * stride));
            const auto index = [stride](unsigned i, unsigned j) { return i * stride + j; };
            for (unsigned i = 0; i <= divisions; ++i)
                for (unsigned j = 0; j + i <= divisions; ++j) {
                    const Vec3d direction = normalized(fan[0] * double(divisions - i - j) +
                                                       fan[1] * double(i) + fan[2] * double(j));
                    samples[index(i, j)] = {direction,
                        relief.sample(direction).elevation_m - terrain.sea_level_m};
                }
            const auto triangle = [&](unsigned i, unsigned j, bool upper) {
                const std::array<Point, 3> points = upper ?
                    std::array<Point, 3>{samples[index(i + 1, j)], samples[index(i + 1, j + 1)],
                                         samples[index(i, j + 1)]} :
                    std::array<Point, 3>{samples[index(i, j)], samples[index(i + 1, j)],
                                         samples[index(i, j + 1)]};
                const bool wet = points[0].height_m <= 0.0;
                if ((points[1].height_m <= 0.0) != wet || (points[2].height_m <= 0.0) != wet)
                    ++coast_triangles;
                emit(points, focus, radius, vertices);
            };
            for (unsigned i = 0; i < divisions; ++i)
                for (unsigned j = 0; i + j < divisions; ++j) {
                    triangle(i, j, false);
                    if (i + j + 1 < divisions) triangle(i, j, true);
                }
        }
    }
    std::ofstream output(prefix + "-fan.bin", std::ios::binary);
    if (!output || vertices.empty() || vertices.size() > UINT32_MAX)
        throw std::runtime_error("cannot write fan probe");
    const auto count = static_cast<std::uint32_t>(vertices.size());
    output.write(reinterpret_cast<const char*>(&count), sizeof(count));
    output.write(reinterpret_cast<const char*>(vertices.data()),
                 static_cast<std::streamsize>(vertices.size() * sizeof(Vertex)));
    if (!output) throw std::runtime_error("incomplete fan probe output");
    std::cout << "real dual-cell fan triangles " << fan_triangles << ", subdivided coast triangles "
              << coast_triangles << ", exported vertices " << count << " ("
              << vertices.size() * sizeof(Vertex) << " bytes); cap rad " << cap_rad
              << ", subdivisions " << divisions << '\n';
}
