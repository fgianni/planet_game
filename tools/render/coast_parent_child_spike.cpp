// Probe-only ADR-0030 parent/child shared-coast experiment, no renderer change.
// Build against libPlanetSimPresentation.a and libPlanetSim.a.
// Usage: coast_parent_child_spike 4|6 prefix [flat|raised|flat-boundary|raised-boundary|raised-sparse] [ultra]
//        coast_parent_child_spike 4|6 prefix MODE audit|case|face-render|corner-render seed face x y
// Writes prefix-{parent,children,detail}.bin and prefix-focus.txt for
// godot/tests/probe_coast_render_mesh.gd. Parent coarse step = 4, child = 2,
// shared coastal step = 1 in a 256 x 256 parent-local canonical lattice.
#include "sim/planet/run/planet_run.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace planetsim;
using namespace planetsim::presentation;
constexpr int parent_grid = 256;
struct Point { double x, y, h; };
using Vertex = std::array<float, 4>;
struct EdgeSegment {
    double start, end; // local coordinate on the tile boundary
    std::array<float, 3> first, last; // actual exported camera-relative positions
    bool water;
};
using EdgeSegments = std::array<std::vector<EdgeSegment>, 4>;

struct Tile {
    const TerrainRelief& relief;
    TileKey key;
    double sea;
    int n;
    std::vector<double> height;
    Tile(const TerrainRelief& r, TileKey k, double s, int count)
        : relief(r), key(k), sea(s), n(count),
          height(static_cast<std::size_t>((n + 1) * (n + 1))) {
        for (int y = 0; y <= n; ++y) for (int x = 0; x <= n; ++x)
            height[static_cast<std::size_t>(y * (n + 1) + x)] =
                relief.sample(direction(x, y)).elevation_m - sea;
    }
    Vec3d direction(double x, double y) const {
        const double side = std::ldexp(1.0, key.level);
        return tile_face_direction(key.face, -1.0 + 2.0 * (key.x + x / n) / side,
                                   -1.0 + 2.0 * (key.y + y / n) / side);
    }
    double at(int x, int y) const { return height[static_cast<std::size_t>(y * (n + 1) + x)]; }
    Point point(int x, int y) const { return {static_cast<double>(x), static_cast<double>(y), at(x, y)}; }
};

std::vector<Point> clip(const std::array<Point, 3>& triangle, bool water) {
    std::vector<Point> polygon;
    for (int i = 0; i < 3; ++i) {
        const Point& a = triangle[static_cast<std::size_t>(i)];
        const Point& b = triangle[static_cast<std::size_t>((i + 1) % 3)];
        const bool a_inside = water ? a.h <= 0.0 : a.h >= 0.0;
        const bool b_inside = water ? b.h <= 0.0 : b.h >= 0.0;
        if (a_inside) polygon.push_back(a);
        if (a_inside != b_inside) {
            const double t = a.h / (a.h - b.h);
            polygon.push_back({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), 0.0});
        }
    }
    return polygon;
}

std::array<float, 3> drawn_position(const Tile& tile, const Point& p,
                                    const Vec3d& focus, bool raised, bool water) {
    const Vec3d position = tile.direction(p.x, p.y) *
        (1.0 + (raised && !water ? std::max(0.0, p.h) /
                  tile.relief.mesh().radius_m() : 0.0)) - focus;
    return {static_cast<float>(position.x), static_cast<float>(position.y),
            static_cast<float>(position.z)};
}

void emit(const Tile& tile, const std::array<Point, 3>& triangle, const Vec3d& focus,
          bool raised, std::vector<Vertex>& out, EdgeSegments* edges = nullptr) {
    for (const bool water : {true, false}) {
        const auto polygon = clip(triangle, water);
        if (edges && polygon.size() >= 3) {
            double twice_area = 0.0;
            for (std::size_t i = 0; i < polygon.size(); ++i) {
                const Point& a = polygon[i], &b = polygon[(i + 1) % polygon.size()];
                twice_area += a.x * b.y - b.x * a.y;
            }
            if (std::abs(twice_area) > 1e-12)
                for (std::size_t i = 0; i < polygon.size(); ++i) {
                    const Point& a = polygon[i], &b = polygon[(i + 1) % polygon.size()];
                    for (int edge = 0; edge < 4; ++edge) {
                        const bool vertical = edge == 1 || edge == 3;
                        const double fixed = edge == 1 || edge == 2 ? tile.n : 0.0;
                        const double av = vertical ? a.x : a.y, bv = vertical ? b.x : b.y;
                        if (std::abs(av - fixed) > 1e-9 || std::abs(bv - fixed) > 1e-9) continue;
                        const double at = vertical ? a.y : a.x, bt = vertical ? b.y : b.x;
                        if (std::abs(at - bt) <= 1e-9) continue;
                        const bool forward = at < bt;
                        edges->at(static_cast<std::size_t>(edge)).push_back({
                            std::min(at, bt), std::max(at, bt),
                            drawn_position(tile, forward ? a : b, focus, raised, water),
                            drawn_position(tile, forward ? b : a, focus, raised, water), water});
                    }
                }
        }
        for (std::size_t i = 2; i < polygon.size(); ++i)
            for (const Point& p : {polygon[0], polygon[i - 1], polygon[i]}) {
                const auto position = drawn_position(tile, p, focus, raised, water);
                out.push_back({position[0], position[1], position[2], water ? 1.0F : 0.0F});
            }
    }
}

void square(const Tile& tile, int x, int y, int step, const Vec3d& focus,
            bool raised, std::vector<Vertex>& out, EdgeSegments* edges = nullptr) {
    const Point a = tile.point(x, y), b = tile.point(x + step, y);
    const Point c = tile.point(x, y + step), d = tile.point(x + step, y + step);
    emit(tile, {a, d, b}, focus, raised, out, edges);
    emit(tile, {a, c, d}, focus, raised, out, edges);
}

// A uniform coarse square shares every sampled boundary vertex with an
// adjacent refined coastal square. Interior is uniformly classified.
void transition_square(const Tile& tile, int x, int y, int step,
                       const std::array<std::vector<int>, 4>& offsets, const Vec3d& focus,
                       bool raised, std::vector<Vertex>& out, EdgeSegments* edges) {
    std::vector<Point> perimeter{tile.point(x, y)};
    for (int t : offsets[0]) perimeter.push_back(tile.point(x + t, y));
    perimeter.push_back(tile.point(x + step, y));
    for (int t : offsets[1]) perimeter.push_back(tile.point(x + step, y + t));
    perimeter.push_back(tile.point(x + step, y + step));
    for (int t : offsets[2]) perimeter.push_back(tile.point(x + t, y + step));
    perimeter.push_back(tile.point(x, y + step));
    for (int t : offsets[3]) perimeter.push_back(tile.point(x, y + t));
    const Point center = tile.point(x + step / 2, y + step / 2);
    for (const auto& p : perimeter)
        if ((p.h <= 0.0) != (center.h <= 0.0))
            throw std::runtime_error("uniform transition square changes water class");
    for (std::size_t i = 0; i < perimeter.size(); ++i)
        emit(tile, {center, perimeter[i], perimeter[(i + 1) % perimeter.size()]},
             focus, raised, out, edges);
}

struct Counts {
    std::size_t coastal = 0, refined = 0, transitions = 0, vertices = 0;
    // Local external edge vertices emitted by the adaptive mesh. Order:
    // top, right, bottom, left; indexing increases in local x or y.
    std::array<std::vector<std::uint8_t>, 4> boundary;
    EdgeSegments segments;
};

// A shared, position-keyed two-sample edge plan. Both tiles independently
// inspect the same 8-sample-wide strips on both sides of each edge. The
// strip includes the parent step-4, one-block-halo neighbourhood. At cube
// face edges use the full fine boundary until cross-face strip ownership is
// demonstrated; this is intentionally conservative.
using BoundaryPlan = std::array<std::vector<std::uint8_t>, 4>;
BoundaryPlan sparse_boundary(const Tile& tile) {
    BoundaryPlan plan;
    const int face_n = tile.n * (1 << tile.key.level);
    constexpr int radius = 8;
    for (int edge = 0; edge < 4; ++edge) {
        auto& segments = plan[static_cast<std::size_t>(edge)];
        segments.resize(static_cast<std::size_t>(tile.n / 2));
        const bool vertical = edge == 1 || edge == 3;
        const int fixed = (vertical ? static_cast<int>(tile.key.x) : static_cast<int>(tile.key.y)) * tile.n +
                          (edge == 1 || edge == 2 ? tile.n : 0);
        if (fixed == 0 || fixed == face_n) {
            std::fill(segments.begin(), segments.end(), 1);
            continue;
        }
        const int origin = (vertical ? static_cast<int>(tile.key.y) : static_cast<int>(tile.key.x)) * tile.n;
        const int variable_min = std::max(0, origin - radius);
        const int variable_max = std::min(face_n, origin + tile.n + radius);
        const int fixed_min = std::max(0, fixed - radius);
        const int fixed_max = std::min(face_n, fixed + radius);
        const int width = variable_max - variable_min + 1;
        std::vector<std::uint8_t> wet(static_cast<std::size_t>((fixed_max - fixed_min + 1) * width));
        for (int f = fixed_min; f <= fixed_max; ++f) for (int v = variable_min; v <= variable_max; ++v) {
            const int gx = vertical ? f : v, gy = vertical ? v : f;
            const Vec3d direction = tile_face_direction(tile.key.face,
                -1.0 + 2.0 * gx / face_n, -1.0 + 2.0 * gy / face_n);
            wet[static_cast<std::size_t>((f - fixed_min) * width + v - variable_min)] =
                tile.relief.sample(direction).elevation_m <= tile.sea;
        }
        for (int s = 0; s < tile.n / 2; ++s) {
            bool water = false, land = false;
            for (int f = fixed_min; f <= fixed_max; ++f)
                for (int v = std::max(variable_min, origin + 2 * s - radius);
                     v <= std::min(variable_max, origin + 2 * s + 2 + radius); ++v) {
                    const bool is_wet = wet[static_cast<std::size_t>((f - fixed_min) * width + v - variable_min)];
                    water |= is_wet;
                    land |= !is_wet;
                }
            segments[static_cast<std::size_t>(s)] = static_cast<std::uint8_t>(water && land);
        }
    }
    return plan;
}

Counts adaptive(const Tile& tile, int step, const Vec3d& focus, bool raised,
                bool fine_boundary, bool sparse,
                std::vector<Vertex>& out) {
    const int count = tile.n / step;
    const auto index = [count](int x, int y) { return static_cast<std::size_t>(y * count + x); };
    std::vector<std::uint8_t> coastal(static_cast<std::size_t>(count * count), 0);
    std::vector<std::uint8_t> refined(coastal.size(), 0);
    Counts totals;
    for (int y = 0; y < count; ++y) for (int x = 0; x < count; ++x) {
        bool wet = false, dry = false;
        for (int dy = 0; dy <= step; ++dy) for (int dx = 0; dx <= step; ++dx) {
            wet |= tile.at(x * step + dx, y * step + dy) <= 0.0;
            dry |= tile.at(x * step + dx, y * step + dy) > 0.0;
        }
        coastal[index(x, y)] = static_cast<std::uint8_t>(wet && dry);
        totals.coastal += wet && dry;
    }
    for (int y = 0; y < count; ++y) for (int x = 0; x < count; ++x) {
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
            if (x + dx >= 0 && x + dx < count && y + dy >= 0 && y + dy < count)
                refined[index(x, y)] |= coastal[index(x + dx, y + dy)];
        totals.refined += refined[index(x, y)];
    }
    const BoundaryPlan boundary = sparse ? sparse_boundary(tile) : BoundaryPlan{};
    const auto edge_vertex = [&](int edge, int coordinate) {
        return fine_boundary || (sparse &&
            (coordinate % 2 == 0 || boundary[static_cast<std::size_t>(edge)]
                [static_cast<std::size_t>(coordinate / 2)]));
    };
    for (auto& edge : totals.boundary) edge.resize(static_cast<std::size_t>(tile.n + 1));
    const auto mark = [&](int edge, int block, bool fine) {
        for (int t = 0; t <= step; ++t)
            if (t == 0 || t == step || fine || (t < step && edge_vertex(edge, block * step + t)))
                totals.boundary[static_cast<std::size_t>(edge)]
                               [static_cast<std::size_t>(block * step + t)] = 1;
    };
    for (int i = 0; i < count; ++i) {
        mark(0, i, refined[index(i, 0)]);
        mark(1, i, refined[index(count - 1, i)]);
        mark(2, i, refined[index(i, count - 1)]);
        mark(3, i, refined[index(0, i)]);
    }
    const std::size_t before = out.size();
    for (int y = 0; y < count; ++y) for (int x = 0; x < count; ++x) {
        if (refined[index(x, y)]) {
            for (int dy = 0; dy < step; ++dy) for (int dx = 0; dx < step; ++dx)
                square(tile, x * step + dx, y * step + dy, 1, focus, raised, out, &totals.segments);
            continue;
        }
        const std::array<bool, 4> facing_refined{
            y > 0 && refined[index(x, y - 1)],
            x + 1 < count && refined[index(x + 1, y)],
            y + 1 < count && refined[index(x, y + 1)],
            x > 0 && refined[index(x - 1, y)]};
        const std::array<bool, 4> external{y == 0, x == count - 1, y == count - 1, x == 0};
        std::array<std::vector<int>, 4> offsets;
        for (int edge = 0; edge < 4; ++edge) {
            const int begin = (edge == 0 || edge == 2 ? x : y) * step;
            for (int t = 1; t < step; ++t) {
                if (facing_refined[static_cast<std::size_t>(edge)] ||
                    (external[static_cast<std::size_t>(edge)] && edge_vertex(edge, begin + t)))
                    offsets[static_cast<std::size_t>(edge)].push_back(t);
            }
            if (edge >= 2) std::reverse(offsets[static_cast<std::size_t>(edge)].begin(),
                                        offsets[static_cast<std::size_t>(edge)].end());
        }
        if (std::any_of(offsets.begin(), offsets.end(), [](const auto& a) { return !a.empty(); })) {
            ++totals.transitions;
            transition_square(tile, x * step, y * step, step, offsets, focus, raised,
                              out, &totals.segments);
        } else {
            square(tile, x * step, y * step, step, focus, raised, out, &totals.segments);
        }
    }
    totals.vertices = out.size() - before;
    return totals;
}

void save(const std::string& path, const std::vector<Vertex>& vertices) {
    if (vertices.empty() || vertices.size() % 3 ||
        vertices.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("invalid mesh output");
    std::ofstream file(path, std::ios::binary);
    const auto count = static_cast<std::uint32_t>(vertices.size());
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    file.write(reinterpret_cast<const char*>(vertices.data()),
               static_cast<std::streamsize>(vertices.size() * sizeof(vertices[0])));
    if (!file) throw std::runtime_error("cannot write " + path);
}

struct EdgeComparison { std::size_t mismatches = 0, segments = 0; double max_position_gap_m = 0.0; };
EdgeComparison compare_segments(std::vector<EdgeSegment> a, std::vector<EdgeSegment> b,
                                double radius_m) {
    const auto order = [](const EdgeSegment& x, const EdgeSegment& y) {
        if (x.start != y.start) return x.start < y.start;
        if (x.end != y.end) return x.end < y.end;
        return x.water < y.water;
    };
    std::sort(a.begin(), a.end(), order);
    std::sort(b.begin(), b.end(), order);
    EdgeComparison result;
    result.segments = a.size();
    result.mismatches = a.size() > b.size() ? a.size() - b.size() : b.size() - a.size();
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        const EdgeSegment& x = a[i], &y = b[i];
        if (std::abs(x.start - y.start) > 1e-8 ||
            std::abs(x.end - y.end) > 1e-8 || x.water != y.water) {
            ++result.mismatches;
            continue; // Positions of different intervals cannot be compared.
        }
        for (int j = 0; j < 3; ++j) {
            result.max_position_gap_m = std::max(result.max_position_gap_m,
                std::abs(static_cast<double>(x.first[static_cast<std::size_t>(j)]) -
                         y.first[static_cast<std::size_t>(j)]) * radius_m);
            result.max_position_gap_m = std::max(result.max_position_gap_m,
                std::abs(static_cast<double>(x.last[static_cast<std::size_t>(j)]) -
                         y.last[static_cast<std::size_t>(j)]) * radius_m);
        }
    }
    return result;
}
std::size_t edge_coverage_errors(const EdgeSegments& edges, int n) {
    std::size_t errors = 0;
    for (auto edge : edges) {
        std::sort(edge.begin(), edge.end(), [](const EdgeSegment& a, const EdgeSegment& b) {
            if (a.start != b.start) return a.start < b.start;
            return a.end < b.end;
        });
        double end = 0.0;
        for (const auto& segment : edge) {
            errors += std::abs(segment.start - end) > 1e-8;
            end = segment.end;
        }
        errors += std::abs(end - n) > 1e-8;
    }
    return errors;
}

struct FaceFrame { Vec3d n, u, v; };
// Independent inverse of the R1-04 cube-face convention; do not infer the
// opposite tile by copying heights or a boundary plan from the first tile.
constexpr std::array<FaceFrame, 6> faces{{
    {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
    {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
    {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
}};
std::uint8_t containing_face(const Vec3d& p) {
    std::uint8_t best = 0;
    double largest = -1e300;
    for (std::uint8_t i = 0; i < faces.size(); ++i)
        if (dot(p, faces[i].n) > largest) {
            best = i;
            largest = dot(p, faces[i].n);
        }
    return best;
}
std::array<double, 2> face_coordinates(std::uint8_t face, const Vec3d& p) {
    const auto& f = faces[face];
    const double normal = dot(p, f.n);
    return {std::atan2(dot(p, f.u), normal) * 4.0 / std::numbers::pi,
            std::atan2(dot(p, f.v), normal) * 4.0 / std::numbers::pi};
}
struct OpposingFace { TileKey key; int edge; bool reversed; };
OpposingFace across_face(const Tile& tile, int edge) {
    const auto direction = [&](double t, double offset) {
        const double fixed = (edge == 0 || edge == 3 ? -1.0 : 1.0) + offset;
        return edge == 1 || edge == 3 ? tile_face_direction(tile.key.face, fixed, t) :
                                         tile_face_direction(tile.key.face, t, fixed);
    };
    const int ntiles = 1 << tile.key.level;
    const std::uint8_t face = containing_face(direction(0.0, edge == 0 || edge == 3 ? -0.001 : 0.001));
    if (face == tile.key.face) throw std::runtime_error("cube-face boundary has no neighbour");
    const double begin = edge == 1 || edge == 3 ? -1.0 + 2.0 * tile.key.y / ntiles :
                                                 -1.0 + 2.0 * tile.key.x / ntiles;
    const double end = begin + 2.0 / ntiles;
    const auto middle = face_coordinates(face, direction((begin + end) * 0.5, 0.0));
    const bool fixed_u = std::abs(middle[0]) > std::abs(middle[1]);
    const double fixed = fixed_u ? middle[0] : middle[1];
    const int other_edge = fixed_u ? (fixed > 0.0 ? 1 : 3) : (fixed > 0.0 ? 2 : 0);
    const auto first = face_coordinates(face, direction(begin, 0.0));
    const auto last = face_coordinates(face, direction(end, 0.0));
    const bool reversed = (fixed_u ? last[1] < first[1] : last[0] < first[0]);
    const double variable = fixed_u ? middle[1] : middle[0];
    const auto index = static_cast<std::uint32_t>(std::clamp(
        static_cast<int>(std::floor((variable + 1.0) * ntiles * 0.5)), 0, ntiles - 1));
    const auto boundary_index = static_cast<std::uint32_t>(fixed > 0.0 ? ntiles - 1 : 0);
    return {{face, tile.key.level, fixed_u ? boundary_index : index,
             fixed_u ? index : boundary_index}, other_edge, reversed};
}
struct CornerTile { TileKey key; int x, y; };
std::array<CornerTile, 3> corner_tiles(const Tile& tile, int x, int y) {
    const Vec3d direction = tile.direction(x, y);
    const int ntiles = 1 << tile.key.level;
    std::array<CornerTile, 3> tiles{};
    int count = 0;
    for (std::uint8_t face = 0; face < faces.size(); ++face) {
        if (dot(direction, faces[face].n) < 0.5) continue;
        const auto uv = face_coordinates(face, direction);
        if (std::abs(std::abs(uv[0]) - 1.0) > 1e-10 ||
            std::abs(std::abs(uv[1]) - 1.0) > 1e-10)
            throw std::runtime_error("cube corner maps outside face corners");
        if (count >= 3) throw std::runtime_error("more than three faces at cube corner");
        const bool high_x = uv[0] > 0.0, high_y = uv[1] > 0.0;
        tiles[static_cast<std::size_t>(count++)] = {
            {face, tile.key.level, static_cast<std::uint32_t>(high_x ? ntiles - 1 : 0),
             static_cast<std::uint32_t>(high_y ? ntiles - 1 : 0)},
            high_x ? tile.n : 0, high_y ? tile.n : 0};
    }
    if (count != 3) throw std::runtime_error("cube corner does not have three faces");
    return tiles;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3 || (argc > 5 && argc != 9)) return 2;
    const auto level = static_cast<std::uint32_t>(std::stoul(argv[1]));
    if (level != 4 && level != 6) return 2;
    const std::string mode = argc >= 4 ? argv[3] : "flat";
    if (mode != "flat" && mode != "raised" && mode != "flat-boundary" &&
        mode != "raised-boundary" && mode != "raised-sparse") return 2;
    const bool selected = argc == 9;
    const bool audit = selected && std::string(argv[4]) == "audit";
    const bool face_render = selected && std::string(argv[4]) == "face-render";
    const bool corner_render = selected && std::string(argv[4]) == "corner-render";
    if (argc >= 5 && (selected ? (std::string(argv[4]) != "audit" &&
                                   std::string(argv[4]) != "case" &&
                                   std::string(argv[4]) != "face-render" &&
                                   std::string(argv[4]) != "corner-render") :
                                  std::string(argv[4]) != "ultra")) return 2;
    const bool raised = mode == "raised" || mode == "raised-boundary" || mode == "raised-sparse";
    const bool fine_boundary = mode == "flat-boundary" || mode == "raised-boundary";
    const bool sparse = mode == "raised-sparse";
    if ((face_render || corner_render) && !sparse) return 2;
    Scenario scenario;
    scenario.seed = selected ? std::stoull(argv[5]) : 20260928U;
    scenario.subdivision = level;
    const PlanetRun planet(scenario);
    const auto mesh = std::make_shared<const PlanetMesh>(planet.state().mesh());
    const auto terrain = planet.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    const auto start = std::chrono::steady_clock::now();
    const TileKey parent_key = selected ?
        TileKey{static_cast<std::uint8_t>(std::stoul(argv[6])), static_cast<std::uint8_t>(level),
                static_cast<std::uint32_t>(std::stoul(argv[7])), static_cast<std::uint32_t>(std::stoul(argv[8]))} :
        (level == 4 ? TileKey{0, 4, 7, 0} : TileKey{0, 6, 16, 0});
    if (parent_key.face >= 6 || parent_key.x >= (1U << level) || parent_key.y >= (1U << level))
        throw std::invalid_argument("tile key outside cube face");
    const Tile parent(relief, parent_key, terrain.sea_level_m, parent_grid);
    int focus_x = -1, focus_y = -1;
    double nearest = std::numeric_limits<double>::infinity();
    for (int y = 0; y < parent_grid; ++y) for (int x = 0; x < parent_grid; ++x) {
        const bool wet = parent.at(x, y) <= 0.0;
        if (((parent.at(x + 1, y) <= 0.0) != wet ||
             (parent.at(x, y + 1) <= 0.0) != wet ||
             (parent.at(x + 1, y + 1) <= 0.0) != wet) &&
            std::hypot(x - parent_grid / 2.0, y - parent_grid / 2.0) < nearest) {
            focus_x = x; focus_y = y;
            nearest = std::hypot(x - parent_grid / 2.0, y - parent_grid / 2.0);
        }
    }
    if (focus_x < 0) {
        if (!audit && !corner_render) throw std::runtime_error("parent has no sampled coast");
        focus_x = focus_y = parent_grid / 2;
    }
    int render_edge = -1, render_crossing = -1;
    if (face_render) {
        double best = std::numeric_limits<double>::infinity();
        for (int edge = 0; edge < 4; ++edge) {
            const bool on_face = edge == 3 ? parent_key.x == 0 :
                                 edge == 1 ? parent_key.x + 1 == (1U << level) :
                                 edge == 0 ? parent_key.y == 0 :
                                             parent_key.y + 1 == (1U << level);
            if (!on_face) continue;
            for (int t = 0; t < parent_grid; ++t) {
                const auto height = [&](int k) {
                    return edge == 1 || edge == 3 ?
                        parent.at(edge == 3 ? 0 : parent_grid, k) :
                        parent.at(k, edge == 0 ? 0 : parent_grid);
                };
                if ((height(t) <= 0.0) == (height(t + 1) <= 0.0)) continue;
                const double distance = std::abs(t + 0.5 - parent_grid / 2.0);
                if (distance < best) {
                    best = distance;
                    render_edge = edge;
                    render_crossing = t;
                }
            }
        }
        if (render_edge < 0) throw std::runtime_error("face-render tile has no edge crossing");
    }
    if (corner_render && !((parent_key.x == 0 || parent_key.x + 1 == (1U << level)) &&
                           (parent_key.y == 0 || parent_key.y + 1 == (1U << level))))
        throw std::runtime_error("corner-render requires a cube-corner tile");
    const Vec3d focus = face_render ? parent.direction(
        render_edge == 1 ? parent_grid : render_edge == 3 ? 0 : render_crossing + 0.5,
        render_edge == 2 ? parent_grid : render_edge == 0 ? 0 : render_crossing + 0.5) :
        corner_render ? parent.direction(parent_key.x == 0 ? 0 : parent_grid,
                                         parent_key.y == 0 ? 0 : parent_grid) :
        parent.direction(focus_x + 0.5, focus_y + 0.5);
    std::vector<Vertex> parent_vertices, child_vertices, detail;
    const Counts parent_counts = adaptive(parent, 4, focus, raised, fine_boundary, sparse, parent_vertices);
    std::array<Counts, 4> child_counts{};
    std::size_t coverage_errors = edge_coverage_errors(parent_counts.segments, parent_grid);
    std::size_t paired_samples = 0, class_flips = 0;
    double max_height_gap_m = 0.0, max_position_gap_m = 0.0;
    for (int cy = 0; cy < 2; ++cy) for (int cx = 0; cx < 2; ++cx) {
        const TileKey key{parent_key.face, static_cast<std::uint8_t>(parent_key.level + 1),
                          parent_key.x * 2U + static_cast<std::uint32_t>(cx),
                          parent_key.y * 2U + static_cast<std::uint32_t>(cy)};
        const Tile child(relief, key, terrain.sea_level_m, parent_grid / 2);
        for (int y = 0; y <= child.n; ++y) for (int x = 0; x <= child.n; ++x) {
            const int px = cx * child.n + x, py = cy * child.n + y;
            const double a = parent.at(px, py), b = child.at(x, y);
            max_height_gap_m = std::max(max_height_gap_m, std::abs(a - b));
            max_position_gap_m = std::max(max_position_gap_m,
                length(parent.direction(px, py) - child.direction(x, y)) * mesh->radius_m());
            class_flips += (a <= 0.0) != (b <= 0.0);
            ++paired_samples;
        }
        child_counts[static_cast<std::size_t>(cy * 2 + cx)] =
            adaptive(child, 2, focus, raised, fine_boundary, sparse, child_vertices);
        coverage_errors += edge_coverage_errors(
            child_counts[static_cast<std::size_t>(cy * 2 + cx)].segments, child.n);
    }
    if (coverage_errors) throw std::runtime_error("clipped boundary does not cover exactly once");
    if (class_flips || max_height_gap_m > 1e-6 || max_position_gap_m > 1e-6)
        throw std::runtime_error("parent and independently sampled children disagree at canonical points");
    std::size_t child_seam_vertex_mismatches = 0, wet_seam_mismatches = 0;
    double max_seam_chord_gap_m = 0.0;
    const auto check_seam = [&](const std::vector<std::uint8_t>& a,
                                const std::vector<std::uint8_t>& b, bool vertical, int half) {
        for (int t = 0; t <= parent_grid / 2; ++t) {
            if (a[static_cast<std::size_t>(t)] == b[static_cast<std::size_t>(t)]) continue;
            ++child_seam_vertex_mismatches;
            const int x = vertical ? parent_grid / 2 : half * parent_grid / 2 + t;
            const int y = vertical ? half * parent_grid / 2 + t : parent_grid / 2;
            const bool water = parent.at(x, y) <= 0.0;
            wet_seam_mismatches += water;
            const auto& coarse = a[static_cast<std::size_t>(t)] ? b : a;
            int lo = t, hi = t;
            while (lo > 0 && !coarse[static_cast<std::size_t>(lo)]) --lo;
            while (hi < parent_grid / 2 && !coarse[static_cast<std::size_t>(hi)]) ++hi;
            const auto position = [&](int local) {
                const int px = vertical ? parent_grid / 2 : half * parent_grid / 2 + local;
                const int py = vertical ? half * parent_grid / 2 + local : parent_grid / 2;
                return parent.direction(px, py) *
                    (mesh->radius_m() + (raised ? std::max(0.0, parent.at(px, py)) : 0.0));
            };
            const Vec3d chord = position(lo) + (position(hi) - position(lo)) *
                (static_cast<double>(t - lo) / (hi - lo));
            max_seam_chord_gap_m = std::max(max_seam_chord_gap_m, length(position(t) - chord));
        }
    };
    for (int row = 0; row < 2; ++row)
        check_seam(child_counts[static_cast<std::size_t>(row * 2)].boundary[1],
                   child_counts[static_cast<std::size_t>(row * 2 + 1)].boundary[3], true, row);
    for (int col = 0; col < 2; ++col)
        check_seam(child_counts[static_cast<std::size_t>(col)].boundary[2],
                   child_counts[static_cast<std::size_t>(col + 2)].boundary[0], false, col);
    std::size_t parent_child_outer_mismatches = 0;
    for (int edge = 0; edge < 4; ++edge) for (int t = 0; t <= parent_grid; ++t) {
        const int half = t == parent_grid ? 1 : t / (parent_grid / 2);
        const int local = t - half * (parent_grid / 2);
        // top: 0,1; right: 1,3; bottom: 2,3; left: 0,2.
        const int child = (edge == 0 ? half : edge == 1 ? half * 2 + 1 :
                           edge == 2 ? half + 2 : half * 2);
        parent_child_outer_mismatches +=
            parent_counts.boundary[static_cast<std::size_t>(edge)][static_cast<std::size_t>(t)] !=
            child_counts[static_cast<std::size_t>(child)].boundary[static_cast<std::size_t>(edge)]
                        [static_cast<std::size_t>(local)];
    }
    if ((fine_boundary || sparse) && (child_seam_vertex_mismatches || parent_child_outer_mismatches))
        throw std::runtime_error("shared fine boundary failed to conform tile edges");
    EdgeComparison child_edge_segments, outer_edge_segments;
    const auto accumulate = [](EdgeComparison& into, const EdgeComparison& from) {
        into.mismatches += from.mismatches;
        into.segments += from.segments;
        into.max_position_gap_m = std::max(into.max_position_gap_m, from.max_position_gap_m);
    };
    for (int row = 0; row < 2; ++row)
        accumulate(child_edge_segments, compare_segments(
            child_counts[static_cast<std::size_t>(row * 2)].segments[1],
            child_counts[static_cast<std::size_t>(row * 2 + 1)].segments[3], mesh->radius_m()));
    for (int col = 0; col < 2; ++col)
        accumulate(child_edge_segments, compare_segments(
            child_counts[static_cast<std::size_t>(col)].segments[2],
            child_counts[static_cast<std::size_t>(col + 2)].segments[0], mesh->radius_m()));
    for (int edge = 0; edge < 4; ++edge) {
        std::vector<EdgeSegment> combined;
        for (int half = 0; half < 2; ++half) {
            const int child = edge == 0 ? half : edge == 1 ? half * 2 + 1 :
                              edge == 2 ? half + 2 : half * 2;
            for (EdgeSegment segment : child_counts[static_cast<std::size_t>(child)].segments
                                               [static_cast<std::size_t>(edge)]) {
                segment.start += half * (parent_grid / 2);
                segment.end += half * (parent_grid / 2);
                combined.push_back(segment);
            }
        }
        accumulate(outer_edge_segments, compare_segments(
            parent_counts.segments[static_cast<std::size_t>(edge)], combined, mesh->radius_m()));
    }
    if ((fine_boundary || sparse) && (child_edge_segments.mismatches ||
                                     outer_edge_segments.mismatches ||
                                     child_edge_segments.max_position_gap_m > 1e-5 ||
                                     outer_edge_segments.max_position_gap_m > 1e-5))
        throw std::runtime_error("clipped triangle edges disagree across handoff");
    const auto adaptive_done = std::chrono::steady_clock::now();
    std::size_t same_level_neighbour_mismatches = 0;
    EdgeComparison same_level_segments;
    if (sparse) for (int axis = 0; axis < 2; ++axis) {
        TileKey adjacent = parent_key;
        if (axis == 0) ++adjacent.x;
        else ++adjacent.y;
        if (adjacent.x >= (1U << level) || adjacent.y >= (1U << level)) continue;
        const Tile neighbour(relief, adjacent, terrain.sea_level_m, parent_grid);
        std::vector<Vertex> unused;
        const Counts neighbour_counts = adaptive(neighbour, 4, focus, raised, false, true, unused);
        const int edge = axis == 0 ? 1 : 2, opposite = axis == 0 ? 3 : 0;
        for (int t = 0; t <= parent_grid; ++t)
            same_level_neighbour_mismatches +=
                parent_counts.boundary[static_cast<std::size_t>(edge)][static_cast<std::size_t>(t)] !=
                neighbour_counts.boundary[static_cast<std::size_t>(opposite)][static_cast<std::size_t>(t)];
        accumulate(same_level_segments, compare_segments(
            parent_counts.segments[static_cast<std::size_t>(edge)],
            neighbour_counts.segments[static_cast<std::size_t>(opposite)], mesh->radius_m()));
    }
    if (sparse && (same_level_neighbour_mismatches || same_level_segments.mismatches ||
                   same_level_segments.max_position_gap_m > 1e-5))
        throw std::runtime_error("sparse boundary disagrees with same-face neighbour");
    EdgeComparison face_segments, mixed_face_segments;
    std::size_t face_edges = 0, face_shore_crossings = 0;
    if (sparse) for (int edge = 0; edge < 4; ++edge) {
        const bool on_face = edge == 3 ? parent_key.x == 0 :
                             edge == 1 ? parent_key.x + 1 == (1U << level) :
                             edge == 0 ? parent_key.y == 0 :
                                         parent_key.y + 1 == (1U << level);
        if (!on_face) continue;
        const OpposingFace opposite = across_face(parent, edge);
        const Tile neighbour(relief, opposite.key, terrain.sea_level_m, parent_grid);
        std::vector<Vertex> unused;
        const Counts neighbour_counts = adaptive(neighbour, 4, focus, raised, false, true, unused);
        if (edge_coverage_errors(neighbour_counts.segments, neighbour.n))
            throw std::runtime_error("opposing face boundary coverage failed");
        std::vector<EdgeSegment> converted = neighbour_counts.segments[static_cast<std::size_t>(opposite.edge)];
        if (opposite.reversed) for (auto& segment : converted) {
            const double start = segment.start;
            segment.start = parent_grid - segment.end;
            segment.end = parent_grid - start;
            std::swap(segment.first, segment.last);
        }
        const EdgeComparison pair = compare_segments(
            parent_counts.segments[static_cast<std::size_t>(edge)], converted, mesh->radius_m());
        accumulate(face_segments, pair);
        // Independently generate the *finer* tiles on the opposite face,
        // rather than subdividing the same-level neighbour's edge array.
        std::vector<EdgeSegment> finer;
        for (int half = 0; half < 2; ++half) {
            const int cx = opposite.edge == 1 ? 1 :
                           opposite.edge == 3 ? 0 : half;
            const int cy = opposite.edge == 2 ? 1 :
                           opposite.edge == 0 ? 0 : half;
            const TileKey child_key{opposite.key.face,
                static_cast<std::uint8_t>(opposite.key.level + 1),
                opposite.key.x * 2U + static_cast<std::uint32_t>(cx),
                opposite.key.y * 2U + static_cast<std::uint32_t>(cy)};
            const Tile child(relief, child_key, terrain.sea_level_m, parent_grid / 2);
            std::vector<Vertex> child_vertices_unused;
            const Counts child_counts_opposite = adaptive(child, 2, focus, raised, false, true,
                                                          child_vertices_unused);
            if (edge_coverage_errors(child_counts_opposite.segments, child.n))
                throw std::runtime_error("finer opposing face boundary coverage failed");
            for (EdgeSegment segment : child_counts_opposite.segments
                                           [static_cast<std::size_t>(opposite.edge)]) {
                segment.start += half * (parent_grid / 2);
                segment.end += half * (parent_grid / 2);
                finer.push_back(segment);
            }
        }
        if (opposite.reversed) for (auto& segment : finer) {
            const double start = segment.start;
            segment.start = parent_grid - segment.end;
            segment.end = parent_grid - start;
            std::swap(segment.first, segment.last);
        }
        accumulate(mixed_face_segments, compare_segments(
            parent_counts.segments[static_cast<std::size_t>(edge)], finer, mesh->radius_m()));
        ++face_edges;
        for (int t = 1; t <= parent_grid; ++t) {
            const auto height = [&](int k) {
                return edge == 1 || edge == 3 ?
                    parent.at(edge == 3 ? 0 : parent_grid, k) :
                    parent.at(k, edge == 0 ? 0 : parent_grid);
            };
            face_shore_crossings += (height(t - 1) <= 0.0) != (height(t) <= 0.0);
        }
    }
    if (sparse && (face_segments.mismatches || face_segments.max_position_gap_m > 1e-5))
        throw std::runtime_error("clipped edge disagrees across cube faces: " +
            std::to_string(face_segments.mismatches) + " intervals, max endpoint gap " +
            std::to_string(face_segments.max_position_gap_m) + " m");
    if (sparse && (mixed_face_segments.mismatches || mixed_face_segments.max_position_gap_m > 1e-5))
        throw std::runtime_error("clipped parent edge disagrees with finer opposite-face tiles: " +
            std::to_string(mixed_face_segments.mismatches) + " intervals, max endpoint gap " +
            std::to_string(mixed_face_segments.max_position_gap_m) + " m");
    std::size_t corner_faces = 0, corner_class_mismatches = 0, corner_endpoint_mismatches = 0;
    double corner_max_gap_m = 0.0;
    if (sparse && (parent_key.x == 0 || parent_key.x + 1 == (1U << level)) &&
                  (parent_key.y == 0 || parent_key.y + 1 == (1U << level))) {
        const int x = parent_key.x == 0 ? 0 : parent_grid;
        const int y = parent_key.y == 0 ? 0 : parent_grid;
        const auto triplet = corner_tiles(parent, x, y);
        std::array<float, 3> first_position{};
        bool first_class = false;
        for (const auto& item : triplet) {
            const Tile tile(relief, item.key, terrain.sea_level_m, parent_grid);
            std::vector<Vertex> vertices;
            const Counts counts = adaptive(tile, 4, focus, raised, false, true, vertices);
            const bool wet = tile.at(item.x, item.y) <= 0.0;
            const auto position = drawn_position(tile, tile.point(item.x, item.y), focus, raised, wet);
            // The clipped polygon must actually emit this corner on both
            // incident exterior edges, not merely sample the same height.
            for (int edge : {item.x == 0 ? 3 : 1, item.y == 0 ? 0 : 2}) {
                bool found = false;
                const bool at_start = (edge == 1 || edge == 3 ? item.y : item.x) == 0;
                for (const auto& segment : counts.segments[static_cast<std::size_t>(edge)]) {
                    const auto& endpoint = at_start ? segment.first : segment.last;
                    if ((at_start ? segment.start == 0.0 : segment.end == parent_grid) &&
                        segment.water == wet && endpoint == position)
                        found = true;
                }
                corner_endpoint_mismatches += !found;
            }
            if (corner_faces++) {
                corner_class_mismatches += wet != first_class;
                for (int axis = 0; axis < 3; ++axis)
                    corner_max_gap_m = std::max(corner_max_gap_m,
                        std::abs(static_cast<double>(position[static_cast<std::size_t>(axis)]) -
                                 first_position[static_cast<std::size_t>(axis)]) * mesh->radius_m());
            } else {
                first_class = wet;
                first_position = position;
            }
        }
        if (corner_class_mismatches || corner_endpoint_mismatches || corner_max_gap_m > 1e-5)
            throw std::runtime_error("independently emitted three-face corner disagrees");
    }
    if (face_render) {
        const OpposingFace opposite = across_face(parent, render_edge);
        std::vector<Vertex> same = parent_vertices, mixed = parent_vertices;
        const Tile neighbour(relief, opposite.key, terrain.sea_level_m, parent_grid);
        adaptive(neighbour, 4, focus, raised, false, true, same);
        // Render the whole neighbour footprint, not just the two children
        // touching the seam (the clipped-edge audit above uses those two).
        for (int cy = 0; cy < 2; ++cy) for (int cx = 0; cx < 2; ++cx) {
            const TileKey key{opposite.key.face, static_cast<std::uint8_t>(level + 1),
                opposite.key.x * 2U + static_cast<std::uint32_t>(cx),
                opposite.key.y * 2U + static_cast<std::uint32_t>(cy)};
            const Tile finer_tile(relief, key, terrain.sea_level_m, parent_grid / 2);
            adaptive(finer_tile, 2, focus, raised, false, true, mixed);
        }
        save(std::string(argv[2]) + "-face-same.bin", same);
        save(std::string(argv[2]) + "-face-mixed.bin", mixed);
        std::cout << "face render edge " << render_edge << " crossing " << render_crossing
                  << " same/mixed vertices " << same.size() << '/' << mixed.size() << '\n';
    }
    if (corner_render) {
        const auto triplet = corner_tiles(parent, parent_key.x == 0 ? 0 : parent_grid,
                                         parent_key.y == 0 ? 0 : parent_grid);
        std::vector<Vertex> same = parent_vertices, mixed = child_vertices;
        for (const auto& item : triplet) {
            if (item.key.face == parent_key.face) continue;
            const Tile neighbour(relief, item.key, terrain.sea_level_m, parent_grid);
            adaptive(neighbour, 4, focus, raised, false, true, same);
            for (int cy = 0; cy < 2; ++cy) for (int cx = 0; cx < 2; ++cx) {
                const TileKey key{item.key.face, static_cast<std::uint8_t>(level + 1),
                    item.key.x * 2U + static_cast<std::uint32_t>(cx),
                    item.key.y * 2U + static_cast<std::uint32_t>(cy)};
                const Tile finer_tile(relief, key, terrain.sea_level_m, parent_grid / 2);
                adaptive(finer_tile, 2, focus, raised, false, true, mixed);
            }
        }
        save(std::string(argv[2]) + "-face-same.bin", same);
        save(std::string(argv[2]) + "-face-mixed.bin", mixed);
        std::cout << "three-face corner render same/mixed vertices " << same.size()
                  << '/' << mixed.size() << '\n';
    }
    if (!audit && !face_render && !corner_render)
        for (int y = 0; y < parent_grid; ++y) for (int x = 0; x < parent_grid; ++x)
        square(parent, x, y, 1, focus, raised, detail);
    const std::string prefix = argv[2];
    if (!audit && !face_render && !corner_render) {
        save(prefix + "-parent.bin", parent_vertices);
        save(prefix + "-children.bin", child_vertices);
        save(prefix + "-detail.bin", detail);
    }
    if (argc == 5) {
        const Tile denser(relief, parent_key, terrain.sea_level_m, parent_grid * 2);
        std::vector<Vertex> ultra;
        for (int y = 0; y < denser.n; ++y) for (int x = 0; x < denser.n; ++x)
            square(denser, x, y, 1, focus, raised, ultra);
        save(prefix + "-ultra.bin", ultra);
        std::cout << "twice-fine reference vertices " << ultra.size() << '\n';
    }
    if (!audit) {
        std::ofstream info(prefix + "-focus.txt");
        info.precision(17);
        info << focus.x << ' ' << focus.y << ' ' << focus.z << '\n';
    }
    std::size_t child_coast = 0, child_refined = 0, child_transitions = 0;
    for (const auto& child : child_counts) {
        child_coast += child.coastal;
        child_refined += child.refined;
        child_transitions += child.transitions;
    }
    std::cout << "seed " << scenario.seed << " key " << static_cast<int>(parent_key.face)
              << '/' << level << '/' << parent_key.x << '/' << parent_key.y
              << " mode " << mode << " focus " << focus_x << '/'
              << focus_y << " parent coast/refined/transition squares " << parent_counts.coastal
              << '/' << parent_counts.refined << '/' << parent_counts.transitions
              << " children " << child_coast << '/' << child_refined << '/' << child_transitions
              << " vertex counts " << parent_vertices.size() << '/' << child_vertices.size()
              << '/' << detail.size() << "; " << paired_samples << " shared samples, "
              << class_flips << " class flips, max height/position difference "
              << max_height_gap_m << " / " << max_position_gap_m
              << " m; child seam unmatched vertices " << child_seam_vertex_mismatches
              << " (wet " << wet_seam_mismatches << "), max raised/flat chord gap "
              << max_seam_chord_gap_m << " m; parent/child outer edge unmatched vertices "
              << parent_child_outer_mismatches << "; same-face neighbour unmatched vertices "
              << same_level_neighbour_mismatches
              << "; clipped child/outer segments " << child_edge_segments.segments
              << '/' << outer_edge_segments.segments << " mismatches "
              << child_edge_segments.mismatches << '/' << outer_edge_segments.mismatches
              << "; boundary coverage errors " << coverage_errors
              << " max float-position gap " << child_edge_segments.max_position_gap_m
              << '/' << outer_edge_segments.max_position_gap_m << " m"
              << "; same-face clipped segments " << same_level_segments.segments
              << " mismatches " << same_level_segments.mismatches
              << "; cube-face edges/crossings/segments " << face_edges << '/'
               << face_shore_crossings << '/' << face_segments.segments
               << " mismatches " << face_segments.mismatches
               << " max float-position gap " << face_segments.max_position_gap_m << " m"
               << "; mixed-face clipped segments " << mixed_face_segments.segments
               << " mismatches " << mixed_face_segments.mismatches
                << " max float-position gap " << mixed_face_segments.max_position_gap_m << " m"
               << "; three-face corner faces/class/endpoint mismatches " << corner_faces
               << '/' << corner_class_mismatches << '/' << corner_endpoint_mismatches
               << " max float-position gap " << corner_max_gap_m << " m"
              << "; parent + children sampling/mesh "
              << std::chrono::duration<double>(adaptive_done - start).count() << " s\n";
}
