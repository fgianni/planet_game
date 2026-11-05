// Probe-only ADR-0030: canonical relief on a bounded set of real dual-cell
// fan triangles. Not a production mesh: the outer hand-back is not stitched.
// Build against build-godot/libPlanetSimPresentation.a and libPlanetSim.a,
// with C++20, -O3, -ffp-contract=off, -lzstd and -pthread.
// Usage: coast_cell_fan_spike 4|6 seed existing-patch-prefix
//        [subdivisions [adaptive [--cap-rad=0.2] [--factor=8]]]
// Writes existing-patch-prefix-fan.bin for probe_coast_render_mesh.gd;
// adaptive mode also writes open-boundary georeferenced samples to CSV.
#include "sim/planet/run/planet_run.hpp"
#include "sim/presentation/terrain_relief.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace planetsim;
using namespace planetsim::presentation;
using Vertex = std::array<float, 4>;
struct Point { Vec3d direction; double height_m; };
using EdgePoint = std::array<std::int64_t, 3>;
using EdgeKey = std::pair<EdgePoint, EdgePoint>;
struct BaseTriangle { std::array<Point, 3> points; bool refine; };
struct EdgeAudit { std::vector<Point> points; unsigned visits = 0; };
struct MacroPoint { Vec3d direction; double drawn_height_m; };
struct MacroEdge { std::array<MacroPoint, 2> ends; unsigned visits = 0; };

EdgePoint point_key(const Point& point) {
    constexpr double quantization = 1e10; // ~0.6 mm on an Earth-radius sphere
    return {std::llround(point.direction.x * quantization),
            std::llround(point.direction.y * quantization),
            std::llround(point.direction.z * quantization)};
}

EdgeKey edge_key(const Point& a, const Point& b) {
    const EdgePoint first = point_key(a), second = point_key(b);
    return std::minmax(first, second);
}

Point sample(const TerrainRelief& relief, double sea_level_m, const Vec3d& direction) {
    const Vec3d unit = normalized(direction);
    return {unit, relief.sample(unit).elevation_m - sea_level_m};
}

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
        for (std::size_t i = 2; i < polygon.size(); ++i) {
            std::array<Vertex, 3> triangle_vertices{};
            std::size_t vertex = 0;
            for (const Point& point : {polygon[0], polygon[i - 1], polygon[i]}) {
                const Vec3d position = point.direction *
                    (1.0 + (wet ? 0.0 : std::max(0.0, point.height_m) / radius_m)) - focus;
                triangle_vertices[vertex++] = {static_cast<float>(position.x),
                    static_cast<float>(position.y), static_cast<float>(position.z),
                    wet ? 1.0F : 0.0F};
            }
            // Sea-level clipping can create a nominal triangle with two
            // identical exported float32 positions. It has zero area and
            // double-counts neighbouring edges in the incidence audit.
            const auto same_position = [&](int a, int b) {
                return triangle_vertices[a][0] == triangle_vertices[b][0] &&
                       triangle_vertices[a][1] == triangle_vertices[b][1] &&
                       triangle_vertices[a][2] == triangle_vertices[b][2];
            };
            if (same_position(0, 1) || same_position(1, 2) || same_position(0, 2))
                continue;
            vertices.insert(vertices.end(), triangle_vertices.begin(), triangle_vertices.end());
        }
    }
}

// Every base triangle has the same fine edge intervals, regardless of
// whether its interior is refined. This avoids a T-junction at the *base*
// lattice edge. It does not join the outer cap to the production fan.
bool needs_refinement(const std::array<Point, 3>& base, const TerrainRelief& relief,
                      double sea_level_m, unsigned factor) {
    const bool wet = base[0].height_m <= 0.0;
    bool mixed = false;
    for (unsigned i = 0; i <= factor; ++i)
        for (unsigned j = 0; i + j <= factor; ++j) {
            const Vec3d direction = base[0].direction * double(factor - i - j) +
                                    base[1].direction * double(i) + base[2].direction * double(j);
            mixed |= (sample(relief, sea_level_m, direction).height_m <= 0.0) != wet;
        }
    mixed |= (sample(relief, sea_level_m, base[0].direction + base[1].direction +
                     base[2].direction).height_m <= 0.0) != wet;
    return mixed;
}

void emit_adaptive(const BaseTriangle& triangle, const std::map<EdgeKey, bool>& refined_edges,
                   const TerrainRelief& relief, double sea_level_m, const Vec3d& focus,
                   double radius_m, std::vector<Vertex>& vertices,
                   std::size_t& coast_triangles, unsigned factor) {
    const auto& base = triangle.points;
    const auto at = [factor](unsigned i, unsigned j) { return i * (factor + 1) + j; };
    const auto fine = [&](unsigned i, unsigned j) {
        return sample(relief, sea_level_m,
            base[0].direction * double(factor - i - j) +
            base[1].direction * double(i) + base[2].direction * double(j));
    };
    if (triangle.refine) {
        std::vector<Point> samples(static_cast<std::size_t>((factor + 1) * (factor + 1)));
        for (unsigned i = 0; i <= factor; ++i)
            for (unsigned j = 0; i + j <= factor; ++j) samples[at(i, j)] = fine(i, j);
        for (unsigned i = 0; i < factor; ++i)
            for (unsigned j = 0; i + j < factor; ++j) {
                const std::array<Point, 3> lower{samples[at(i, j)], samples[at(i + 1, j)],
                                                  samples[at(i, j + 1)]};
                const auto draw = [&](const std::array<Point, 3>& tri) {
                    const bool first_wet = tri[0].height_m <= 0.0;
                    if ((tri[1].height_m <= 0.0) != first_wet ||
                        (tri[2].height_m <= 0.0) != first_wet) ++coast_triangles;
                    emit(tri, focus, radius_m, vertices);
                };
                draw(lower);
                if (i + j + 1 < factor)
                    draw({samples[at(i + 1, j)], samples[at(i + 1, j + 1)],
                          samples[at(i, j + 1)]});
            }
    } else {
        std::vector<Point> perimeter;
        for (unsigned edge = 0; edge < 3; ++edge) {
            perimeter.push_back(base[edge]);
            if (!refined_edges.contains(edge_key(base[edge], base[(edge + 1) % 3])))
                continue;
            for (unsigned t = 1; t < factor; ++t) {
                const unsigned i = edge == 0 ? t : edge == 1 ? factor - t : 0;
                const unsigned j = edge == 0 ? 0 : edge == 1 ? t : factor - t;
                perimeter.push_back(fine(i, j));
            }
        }
        if (perimeter.size() == 3) {
            emit(base, focus, radius_m, vertices);
        } else {
            const Point center = sample(relief, sea_level_m,
                base[0].direction + base[1].direction + base[2].direction);
            for (std::size_t t = 0; t < perimeter.size(); ++t)
                emit({center, perimeter[t], perimeter[(t + 1) % perimeter.size()]},
                     focus, radius_m, vertices);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4 || argc > 8) return 2;
    const unsigned level = static_cast<unsigned>(std::stoul(argv[1]));
    if (level != 4 && level != 6) return 2;
    const unsigned divisions = argc == 5 ? static_cast<unsigned>(std::stoul(argv[4])) : 16U;
    if (divisions < 2 || divisions > 64) return 2;
    const bool adaptive = argc >= 6 && std::string(argv[5]) == "adaptive";
    if (argc >= 6 && !adaptive) return 2;
    double cap_rad = 0.10;
    unsigned factor = 4;
    bool cap_seen = false, factor_seen = false;
    for (int arg = 6; arg < argc; ++arg) {
        const std::string option = argv[arg];
        if (option.starts_with("--cap-rad=") && !cap_seen) {
            cap_rad = std::stod(option.substr(10));
            cap_seen = true;
        } else if (option.starts_with("--factor=") && !factor_seen) {
            factor = static_cast<unsigned>(std::stoul(option.substr(9)));
            factor_seen = true;
        } else return 2;
    }
    if (!std::isfinite(cap_rad) || cap_rad < 0.10 || cap_rad > 0.40 ||
        (factor != 4 && factor != 8)) return 2;
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
    // Mirror the existing cell fan's *geometry* heights only. Its map-style
    // water classification is a separate fractional shader and is not
    // represented by this raised-height proxy.
    const auto drawn_height = [&](double elevation, double land) {
        return land >= 0.5 ? std::max(0.0, elevation - terrain.sea_level_m) : 0.0;
    };
    std::vector<double> corner_height(mesh->corner_count(), 0.0);
    if (adaptive) {
        std::vector<double> corner_elevation(mesh->corner_count(), 0.0);
        std::vector<double> corner_land(mesh->corner_count(), 0.0);
        std::vector<unsigned> corner_visits(mesh->corner_count(), 0);
        for (const auto& cell : mesh->cells())
            for (const auto corner : mesh->cell_corners(cell.id)) {
                corner_elevation[corner] += terrain.mean_elevation_m[cell.id.to_index()];
                corner_land[corner] += terrain.land_fraction[cell.id.to_index()];
                ++corner_visits[corner];
            }
        for (std::size_t corner = 0; corner < corner_height.size(); ++corner) {
            if (corner_visits[corner] != 3) throw std::runtime_error("invalid dual corner");
            corner_height[corner] = drawn_height(corner_elevation[corner] / 3.0,
                                                  corner_land[corner] / 3.0);
        }
    }
    // Select whole fan triangles by their actual cell/corner directions.
    // This is deliberately a bounded local footprint, not an L4/L6 global
    // fine mesh. Its selected/unselected outer edge is NOT yet continuous.
    const double cap_dot = std::cos(cap_rad);
    std::vector<Vertex> vertices;
    std::size_t fan_triangles = 0;
    std::size_t coast_triangles = 0;
    std::size_t refined_base_triangles = 0;
    std::vector<BaseTriangle> base_triangles;
    std::map<EdgeKey, bool> refined_edges;
    std::map<EdgeKey, MacroEdge> macro_edges;
    for (const auto& cell : mesh->cells()) {
        const auto corners = mesh->cell_corners(cell.id);
        for (std::size_t edge = 0; edge < corners.size(); ++edge) {
            const std::array<Vec3d, 3> fan = {
                cell.center_unit, mesh->corners_unit()[corners[edge]],
                mesh->corners_unit()[corners[(edge + 1) % corners.size()]]};
            if (std::max({dot(focus, fan[0]), dot(focus, fan[1]), dot(focus, fan[2])}) < cap_dot)
                continue;
            ++fan_triangles;
            if (adaptive) {
                const std::array<MacroPoint, 3> coarse = {{
                    {fan[0], drawn_height(terrain.mean_elevation_m[cell.id.to_index()],
                                          terrain.land_fraction[cell.id.to_index()])},
                    {fan[1], corner_height[corners[edge]]},
                    {fan[2], corner_height[corners[(edge + 1) % corners.size()]]}}};
                for (unsigned side = 0; side < 3; ++side) {
                    const EdgeKey key = edge_key({coarse[side].direction, 0.0},
                                                 {coarse[(side + 1) % 3].direction, 0.0});
                    auto& record = macro_edges[key];
                    if (!record.visits) record.ends = {coarse[side], coarse[(side + 1) % 3]};
                    if (++record.visits > 2) throw std::runtime_error("non-manifold macro edge");
                }
            }
            const unsigned stride = divisions + 1;
            std::vector<Point> samples(static_cast<std::size_t>(stride * stride));
            const auto index = [stride](unsigned i, unsigned j) { return i * stride + j; };
            for (unsigned i = 0; i <= divisions; ++i)
                for (unsigned j = 0; j + i <= divisions; ++j) {
                    const Vec3d direction = normalized(fan[0] * double(divisions - i - j) +
                                                       fan[1] * double(i) + fan[2] * double(j));
                    samples[index(i, j)] = sample(relief, terrain.sea_level_m, direction);
                }
            const auto triangle = [&](unsigned i, unsigned j, bool upper) {
                const std::array<Point, 3> points = upper ?
                    std::array<Point, 3>{samples[index(i + 1, j)], samples[index(i + 1, j + 1)],
                                         samples[index(i, j + 1)]} :
                    std::array<Point, 3>{samples[index(i, j)], samples[index(i + 1, j)],
                                         samples[index(i, j + 1)]};
                if (adaptive) {
                    const bool refine = needs_refinement(points, relief, terrain.sea_level_m,
                                                         factor);
                    base_triangles.push_back({points, refine});
                    if (refine) {
                        ++refined_base_triangles;
                        for (int side = 0; side < 3; ++side)
                            refined_edges[edge_key(points[side], points[(side + 1) % 3])] = true;
                    }
                } else {
                    const bool wet = points[0].height_m <= 0.0;
                    if ((points[1].height_m <= 0.0) != wet || (points[2].height_m <= 0.0) != wet)
                        ++coast_triangles;
                    emit(points, focus, radius, vertices);
                }
            };
            for (unsigned i = 0; i < divisions; ++i)
                for (unsigned j = 0; i + j < divisions; ++j) {
                    triangle(i, j, false);
                    if (i + j + 1 < divisions) triangle(i, j, true);
                }
        }
    }
    if (adaptive)
        for (const auto& triangle : base_triangles)
            emit_adaptive(triangle, refined_edges, relief, terrain.sea_level_m,
                          focus, radius, vertices, coast_triangles, factor);
    std::size_t internal_refined_edges = 0, open_refined_edges = 0;
    if (adaptive) {
        std::map<EdgeKey, EdgeAudit> audited;
        for (const auto& triangle : base_triangles)
            for (int side = 0; side < 3; ++side) {
                const auto& a = triangle.points[side];
                const auto& b = triangle.points[(side + 1) % 3];
                const EdgeKey key = edge_key(a, b);
                if (!refined_edges.contains(key)) continue;
                auto& record = audited[key];
                if (!record.visits) record.points.resize(factor + 1);
                for (unsigned t = 0; t <= factor; ++t) {
                    const double weight = point_key(a) == key.first ? double(t) / factor :
                                          double(factor - t) / factor;
                    const Point point = sample(relief, terrain.sea_level_m,
                        a.direction * (1.0 - weight) + b.direction * weight);
                    if (record.visits && (length(record.points[t].direction - point.direction) > 1e-8 ||
                                          std::abs(record.points[t].height_m - point.height_m) > 1e-4))
                        throw std::runtime_error("inconsistent independently sampled refined edge");
                    record.points[t] = point;
                }
                if (++record.visits > 2) throw std::runtime_error("non-manifold base edge");
            }
        for (const auto& [key, record] : audited) {
            (void)key;
            if (record.visits == 2) ++internal_refined_edges;
            else ++open_refined_edges;
        }
    }
    std::size_t boundary_edges = 0, boundary_crossed = 0;
    std::size_t boundary_crossings = 0, boundary_proxy_misses = 0;
    std::size_t smooth_crossings = 0, smooth_class_misses = 0;
    std::size_t unmatched_detailed_crossings = 0;
    double max_matched_crossing_gap_m = 0.0;
    double boundary_max_height_gap_m = 0.0;
    std::ofstream boundary_file;
    if (adaptive) {
        boundary_file.open(prefix + "-fan-boundary.csv");
        if (!boundary_file) throw std::runtime_error("cannot write boundary samples");
        boundary_file << std::setprecision(17)
                      << "edge,t,detailed_m,filtered_m,coarse_drawn_m,edge_length_m,"
                         "direction_x,direction_y,direction_z\n";
    }
    if (adaptive) for (const auto& [key, record] : macro_edges) {
        (void)key;
        if (record.visits != 1) continue;
        ++boundary_edges;
        constexpr unsigned segments = 64;
        const double edge_length_m = radius * std::acos(std::clamp(
            dot(record.ends[0].direction, record.ends[1].direction), -1.0, 1.0));
        bool crossed = false;
        double previous_height = 0.0, previous_smooth = 0.0;
        std::vector<double> detailed_roots, smooth_roots;
        for (unsigned i = 0; i <= segments; ++i) {
            const double t = double(i) / segments;
            const auto& a = record.ends[0];
            const auto& b = record.ends[1];
            const Vec3d direction = normalized(a.direction * (1.0 - t) + b.direction * t);
            const double height = relief.sample(direction).elevation_m - terrain.sea_level_m;
            const double filtered = relief.smooth_sample(direction).elevation_m - terrain.sea_level_m;
            const bool wet = height <= 0.0;
            if (i) {
                if (wet != (previous_height <= 0.0)) {
                    detailed_roots.push_back((i - 1 + previous_height /
                        (previous_height - height)) / segments);
                    ++boundary_crossings;
                    crossed = true;
                }
                if ((filtered <= 0.0) != (previous_smooth <= 0.0)) {
                    smooth_roots.push_back((i - 1 + previous_smooth /
                        (previous_smooth - filtered)) / segments);
                    ++smooth_crossings;
                }
            }
            previous_height = height;
            previous_smooth = filtered;
            const double coarse = a.drawn_height_m * (1.0 - t) + b.drawn_height_m * t;
            boundary_file << boundary_edges - 1 << ',' << t << ',' << height << ','
                          << filtered << ',' << coarse << ',' << edge_length_m << ','
                          << direction.x << ',' << direction.y << ',' << direction.z << '\n';
            boundary_max_height_gap_m = std::max(boundary_max_height_gap_m,
                                                  std::abs(std::max(0.0, height) - coarse));
            // Heights alone are not the cell shader's water mask. This only
            // flags how often a geometric sea-level proxy disagrees.
            if (i > 0 && i < segments && wet != (coarse <= 0.0))
                ++boundary_proxy_misses;
            if (i > 0 && i < segments && wet != (filtered <= 0.0))
                ++smooth_class_misses;
        }
        boundary_crossed += crossed;
        for (const double root : detailed_roots) {
            if (smooth_roots.empty()) { ++unmatched_detailed_crossings; continue; }
            double nearest = 1.0;
            for (const double smooth_root : smooth_roots)
                nearest = std::min(nearest, std::abs(root - smooth_root));
            max_matched_crossing_gap_m = std::max(max_matched_crossing_gap_m,
                                                  nearest * edge_length_m);
        }
    }
    if (adaptive && !boundary_file) throw std::runtime_error("incomplete boundary samples");
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
              << ", subdivisions " << divisions << ", adaptive " << adaptive
              << ", coastal factor " << factor
              << ", refined base triangles " << refined_base_triangles << '/'
              << base_triangles.size() << ", refined edges " << refined_edges.size()
              << " (internal/open " << internal_refined_edges << '/'
              << open_refined_edges << ")" << '\n';
    if (adaptive)
        std::cout << "outer macro boundary edges/crossed/crossings " << boundary_edges << '/'
                  << boundary_crossed << '/' << boundary_crossings
                  << ", raised-height proxy misses " << boundary_proxy_misses
                  << ", max raised-height gap m " << boundary_max_height_gap_m << '\n'
                  << "smooth-relief boundary crossings/class misses " << smooth_crossings << '/'
                  << smooth_class_misses << ", detailed crossings without an edge counterpart "
                  << unmatched_detailed_crossings << ", max nearest matched gap m "
                  << max_matched_crossing_gap_m << '\n';
}
