// Probe-only clipped coastal patch for offscreen Godot raster validation.
// Usage: coast_render_mesh_spike 4|6 /tmp/opencode/coast-render
//        [seam|seam2|seam4|seam8|seam2b|seam4b|seam6b|adaptive0|adaptive1|adaptive1r|adaptive1s|adaptive1sr]
// Output: prefix-parent.bin / prefix-children.bin / prefix-detail.bin (uint32 vertex count,
// then float32 x,y,z,water per triangle-list vertex), prefix-focus.txt.
#include "sim/planet/run/planet_run.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace planetsim;
using namespace planetsim::presentation;
constexpr int grid = 128;
struct Point { double x, y, h; };
using Triangle = std::array<Point, 3>;
struct Patch {
    const TerrainRelief& relief;
    TileKey key;
    double sea;
    std::vector<double> h;
    Patch(const TerrainRelief& r, TileKey k, double s) : relief(r), key(k), sea(s),
        h(static_cast<std::size_t>((grid + 1) * (grid + 1))) {
        for (int y = 0; y <= grid; ++y) for (int x = 0; x <= grid; ++x)
            h[static_cast<std::size_t>(y * (grid + 1) + x)] =
                relief.sample(direction(x, y)).elevation_m - sea;
    }
    Vec3d direction(double x, double y) const {
        const double side = std::ldexp(1.0, key.level);
        return tile_face_direction(key.face, -1.0 + 2.0 * (key.x + x / grid) / side,
                                   -1.0 + 2.0 * (key.y + y / grid) / side);
    }
    double at(int x, int y) const { return h[static_cast<std::size_t>(y * (grid + 1) + x)]; }
};

std::vector<Point> clip(const Triangle& tri, bool water) {
    std::vector<Point> result;
    for (int i = 0; i < 3; ++i) {
        const Point& a = tri[static_cast<std::size_t>(i)];
        const Point& b = tri[static_cast<std::size_t>((i + 1) % 3)];
        const bool in_a = water ? a.h <= 0.0 : a.h >= 0.0;
        const bool in_b = water ? b.h <= 0.0 : b.h >= 0.0;
        if (in_a) result.push_back(a);
        if (in_a != in_b) {
            const double t = a.h / (a.h - b.h);
            result.push_back({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), 0.0});
        }
    }
    return result;
}

void emit(const Patch& patch, const Triangle& tri, const Vec3d& focus,
          std::vector<std::array<float, 4>>& vertices, bool flat = false) {
    for (const bool water : {true, false}) {
        const auto polygon = clip(tri, water);
        for (std::size_t i = 2; i < polygon.size(); ++i) {
            for (const Point& p : {polygon[0], polygon[i - 1], polygon[i]}) {
                const Vec3d position = patch.direction(p.x, p.y) *
                    (1.0 + (flat || water ? 0.0 : std::max(0.0, p.h) / patch.relief.mesh().radius_m())) - focus;
                vertices.push_back({static_cast<float>(position.x), static_cast<float>(position.y),
                                    static_cast<float>(position.z), water ? 1.0F : 0.0F});
            }
        }
    }
}

void region_bounds(const Patch& patch, int step, int x_begin, int x_end,
                   const Vec3d& focus,
             std::vector<std::array<float, 4>>& vertices, bool flat = false) {
    for (int y = 0; y < grid; y += step) for (int x = x_begin; x < x_end; x += step) {
        const Point a{static_cast<double>(x), static_cast<double>(y), patch.at(x, y)};
        const Point b{static_cast<double>(x + step), static_cast<double>(y), patch.at(x + step, y)};
        const Point c{static_cast<double>(x), static_cast<double>(y + step), patch.at(x, y + step)};
        const Point d{static_cast<double>(x + step), static_cast<double>(y + step), patch.at(x + step, y + step)};
        emit(patch, {a, d, b}, focus, vertices, flat);
        emit(patch, {a, c, d}, focus, vertices, flat);
    }
}

void square(const Patch& patch, int x, int y, int step, const Vec3d& focus,
             std::vector<std::array<float, 4>>& vertices, bool flat = true) {
    const Point a{static_cast<double>(x), static_cast<double>(y), patch.at(x, y)};
    const Point b{static_cast<double>(x + step), static_cast<double>(y), patch.at(x + step, y)};
    const Point c{static_cast<double>(x), static_cast<double>(y + step), patch.at(x, y + step)};
    const Point d{static_cast<double>(x + step), static_cast<double>(y + step), patch.at(x + step, y + step)};
    emit(patch, {a, d, b}, focus, vertices, flat);
    emit(patch, {a, c, d}, focus, vertices, flat);
}

void conforming_square(const Patch& patch, int x, int y,
                       const std::array<bool, 4>& split, const Vec3d& focus,
                       std::vector<std::array<float, 4>>& vertices, bool flat) {
    const auto point = [&](int u, int v) {
        return Point{static_cast<double>(u), static_cast<double>(v), patch.at(u, v)};
    };
    // Ordered perimeter: top, right, bottom, left. Split only edges facing
    // a refined neighbour. Their odd vertices are the same sampled vertices
    // that neighbour emits, rather than interpolated coarse-chord positions.
    std::vector<Point> perimeter{point(x, y)};
    if (split[0]) perimeter.push_back(point(x + 1, y));
    perimeter.push_back(point(x + 2, y));
    if (split[1]) perimeter.push_back(point(x + 2, y + 1));
    perimeter.push_back(point(x + 2, y + 2));
    if (split[2]) perimeter.push_back(point(x + 1, y + 2));
    perimeter.push_back(point(x, y + 2));
    if (split[3]) perimeter.push_back(point(x, y + 1));
    const Point center = point(x + 1, y + 1);
    const bool water = center.h <= 0.0;
    for (const Point& vertex : perimeter)
        if ((vertex.h <= 0.0) != water)
            throw std::runtime_error("conforming square is not uniformly classified");
    for (std::size_t i = 0; i < perimeter.size(); ++i)
        emit(patch, {center, perimeter[i], perimeter[(i + 1) % perimeter.size()]},
             focus, vertices, flat);
}

void finer_reference(const Patch& patch, const Vec3d& focus,
                     std::vector<std::array<float, 4>>& vertices) {
    const auto point = [&](double x, double y) {
        return Point{x, y, patch.relief.sample(patch.direction(x, y)).elevation_m - patch.sea};
    };
    for (int y = 0; y < grid * 2; ++y) for (int x = 0; x < 16; ++x) {
        const double u = x * 0.5, v = y * 0.5;
        const Point a = point(u, v), b = point(u + 0.5, v);
        const Point c = point(u, v + 0.5), d = point(u + 0.5, v + 0.5);
        emit(patch, {a, d, b}, focus, vertices, true);
        emit(patch, {a, c, d}, focus, vertices, true);
    }
}

void region(const Patch& patch, int step, const Vec3d& focus,
            std::vector<std::array<float, 4>>& vertices) {
    region_bounds(patch, step, 0, grid, focus, vertices);
}

void save(const std::string& path, const std::vector<std::array<float, 4>>& vertices) {
    if (vertices.empty() || vertices.size() % 3 != 0 ||
        vertices.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("invalid probe triangle-list size");
    for (const auto& vertex : vertices)
        if (!std::all_of(vertex.begin(), vertex.end(), [](float value) { return std::isfinite(value); }) ||
            (vertex[3] != 0.0F && vertex[3] != 1.0F))
            throw std::runtime_error("non-finite position or invalid water classification");
    std::ofstream file(path, std::ios::binary);
    const auto count = static_cast<std::uint32_t>(vertices.size());
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    file.write(reinterpret_cast<const char*>(vertices.data()),
               static_cast<std::streamsize>(vertices.size() * sizeof(vertices[0])));
    if (!file) throw std::runtime_error("could not write " + path);
}

void seam(const TerrainRelief& relief, double sea, std::uint32_t level,
          const std::string& prefix, int interior_width, bool far_ribbon,
          int adaptive_halo, bool conforming, bool raised) {
    // The two known 6-versus-4 crossing edges of coast_seam_spike.cpp.
    const TileKey left_key = level == 4 ? TileKey{0, 4, 11, 10} : TileKey{0, 6, 55, 0};
    const TileKey right_key{0, static_cast<std::uint8_t>(level), left_key.x + 1, left_key.y};
    const Patch left(relief, left_key, sea), right(relief, right_key, sea);
    int crossing = -1;
    int fine_crossings = 0, coarse_crossings = 0;
    double max_boundary_height_gap_m = 0.0;
    for (int j = 0; j < grid; ++j) {
        max_boundary_height_gap_m = std::max(max_boundary_height_gap_m,
            std::abs(left.at(grid, j) - right.at(0, j)));
        fine_crossings += (right.at(0, j) <= 0.0) != (right.at(0, j + 1) <= 0.0);
        if (j % 2 == 0)
            coarse_crossings += (right.at(0, j) <= 0.0) != (right.at(0, j + 2) <= 0.0);
        if ((right.at(0, j) <= 0.0) != (right.at(0, j + 1) <= 0.0) &&
            (crossing < 0 || std::abs(j - grid / 2) < std::abs(crossing - grid / 2)))
            crossing = j;
    }
    max_boundary_height_gap_m = std::max(max_boundary_height_gap_m,
        std::abs(left.at(grid, grid) - right.at(0, grid)));
    if (crossing < 0 || fine_crossings != 6 || coarse_crossings != 4 ||
        max_boundary_height_gap_m > 1e-6)
        throw std::runtime_error("selected 6/4 seam boundary is no longer reproducible");
    int transition_odd_flips = 0;
    double transition_chord_gap_m = 0.0;
    if (interior_width > 0 && interior_width < 8) {
        for (int j = 1; j < grid; j += 2) {
            const double interpolated = 0.5 * (right.at(interior_width, j - 1) +
                                                right.at(interior_width, j + 1));
            transition_odd_flips += (right.at(interior_width, j) <= 0.0) !=
                                    (interpolated <= 0.0);
            const Vec3d coarse_chord = (right.direction(interior_width, j - 1) +
                                        right.direction(interior_width, j + 1)) * 0.5;
            transition_chord_gap_m = std::max(transition_chord_gap_m,
                length(right.direction(interior_width, j) - coarse_chord) *
                relief.mesh().radius_m());
        }
    }
    const Vec3d focus = right.direction(0, crossing + 0.5);
    std::vector<std::array<float, 4>> baseline, stitched, reference, left_vertices;
    region_bounds(left, 1, grid - 8, grid, focus, left_vertices, !raised);
    region_bounds(right, 2, 0, 8, focus, baseline, !raised);
    region_bounds(right, 1, 0, 8, focus, reference, !raised);
    std::size_t refined_squares = 0, coastal_squares = 0;
    if (adaptive_halo >= 0) {
        constexpr int coarse_x = 4, coarse_y = grid / 2;
        std::array<std::array<bool, coarse_y>, coarse_x> coastal{}, refined{};
        std::vector<std::array<float, 4>> refined_vertices, coarse_vertices;
        for (int x = 0; x < coarse_x; ++x) for (int y = 0; y < coarse_y; ++y) {
            bool water = false, land = false;
            for (int dx = 0; dx <= 2; ++dx) for (int dy = 0; dy <= 2; ++dy) {
                const double h = right.at(2 * x + dx, 2 * y + dy);
                water |= h <= 0.0;
                land |= h > 0.0;
            }
            coastal[x][y] = water && land;
            coastal_squares += coastal[x][y];
        }
        for (int x = 0; x < coarse_x; ++x) for (int y = 0; y < coarse_y; ++y) {
            for (int dx = -adaptive_halo; dx <= adaptive_halo; ++dx)
                for (int dy = -adaptive_halo; dy <= adaptive_halo; ++dy)
                    if (x + dx >= 0 && x + dx < coarse_x && y + dy >= 0 && y + dy < coarse_y)
                        refined[x][y] |= coastal[x + dx][y + dy];
            refined_squares += refined[x][y];
        }
        // All neighbour flags must exist before edge ownership is decided.
        for (int x = 0; x < coarse_x; ++x) for (int y = 0; y < coarse_y; ++y) {
            if (conforming && !refined[x][y]) {
                const std::array<bool, 4> split{
                    y > 0 && refined[x][y - 1],
                    x + 1 < coarse_x && refined[x + 1][y],
                    y + 1 < coarse_y && refined[x][y + 1],
                    x > 0 && refined[x - 1][y]};
                if (std::any_of(split.begin(), split.end(), [](bool value) { return value; })) {
                    conforming_square(right, 2 * x, 2 * y, split, focus, coarse_vertices, !raised);
                    continue;
                }
            }
            for (int yy = 2 * y; yy < 2 * y + 2; yy += refined[x][y] ? 1 : 2)
                for (int xx = 2 * x; xx < 2 * x + 2; xx += refined[x][y] ? 1 : 2)
                    square(right, xx, yy, refined[x][y] ? 1 : 2, focus,
                           refined[x][y] ? refined_vertices : coarse_vertices, !raised);
        }
        stitched.insert(stitched.end(), refined_vertices.begin(), refined_vertices.end());
        stitched.insert(stitched.end(), coarse_vertices.begin(), coarse_vertices.end());
        std::size_t transition_edges = 0, odd_class_flips = 0;
        std::size_t minimum_refined_uses = std::numeric_limits<std::size_t>::max();
        std::size_t minimum_coarse_uses = std::numeric_limits<std::size_t>::max();
        double max_raised_gap_m = 0.0;
        const auto check_transition = [&](int x0, int y0, int xm, int ym, int x1, int y1) {
            ++transition_edges;
            const double a = right.at(x0, y0), mid = right.at(xm, ym), b = right.at(x1, y1);
            odd_class_flips += (mid <= 0.0) != ((a + b) * 0.5 <= 0.0);
            const double radius_m = relief.mesh().radius_m();
            const Vec3d coarse_chord = (right.direction(x0, y0) * (radius_m + std::max(0.0, a)) +
                                        right.direction(x1, y1) * (radius_m + std::max(0.0, b))) * 0.5;
            const Vec3d midpoint = right.direction(xm, ym) * (radius_m + std::max(0.0, mid));
            max_raised_gap_m = std::max(max_raised_gap_m, length(midpoint - coarse_chord));
            const Vec3d drawn = right.direction(xm, ym) *
                (1.0 + (raised ? std::max(0.0, mid) / radius_m : 0.0)) - focus;
            const std::array<float, 3> key{static_cast<float>(drawn.x),
                                           static_cast<float>(drawn.y),
                                           static_cast<float>(drawn.z)};
            const auto count_uses = [&](const auto& vertices) {
                return static_cast<std::size_t>(std::count_if(
                vertices.begin(), vertices.end(), [&](const auto& vertex) {
                    return vertex[0] == key[0] && vertex[1] == key[1] && vertex[2] == key[2];
                }));
            };
            const auto fine_uses = count_uses(refined_vertices);
            const auto coarse_uses = count_uses(coarse_vertices);
            minimum_refined_uses = std::min(minimum_refined_uses, fine_uses);
            minimum_coarse_uses = std::min(minimum_coarse_uses, coarse_uses);
            if (conforming && (fine_uses == 0 || coarse_uses == 0))
                throw std::runtime_error("refined/coarse midpoint missing at " +
                    std::to_string(xm) + "/" + std::to_string(ym) + " uses " +
                    std::to_string(fine_uses) + "/" + std::to_string(coarse_uses));
        };
        for (int x = 0; x < coarse_x; ++x) for (int y = 0; y < coarse_y; ++y) {
            if (x + 1 < coarse_x && refined[x][y] != refined[x + 1][y])
                check_transition(2 * x + 2, 2 * y, 2 * x + 2, 2 * y + 1,
                                 2 * x + 2, 2 * y + 2);
            if (y + 1 < coarse_y && refined[x][y] != refined[x][y + 1])
                check_transition(2 * x, 2 * y + 2, 2 * x + 1, 2 * y + 2,
                                 2 * x + 2, 2 * y + 2);
        }
        std::cout << "adaptive boundary: " << transition_edges << " T-junction edges, "
                  << odd_class_flips << " midpoint class flips, max raised midpoint/chord gap "
                  << max_raised_gap_m << " m if unsplit; edges split " << conforming
                  << ", minimum fine/coarse midpoint uses " << minimum_refined_uses
                  << '/' << minimum_coarse_uses << "\n";
    } else {
        region_bounds(right, 2, far_ribbon ? interior_width + 2 :
                      (interior_width > 0 ? interior_width : 2),
                      8, focus, stitched, true);
    }
    if (adaptive_halo < 0 && interior_width > 0) {
        // Without the second ribbon, the far edge is intentionally unmatched.
        region_bounds(right, 1, 0, interior_width, focus, stitched, true);
        if (far_ribbon) {
            // Fine samples at x=width meet the coarse samples at width+2.
            // The same constrained three-triangle strip is now placed on
            // the *far* edge; adjacent mesh shares its exact endpoints.
            for (int j = 0; j < grid; j += 2) {
                const auto boundary = [&](int y) {
                    return Point{static_cast<double>(interior_width), static_cast<double>(y),
                                 right.at(interior_width, y)};
                };
                const auto inside = [&](int y) {
                    return Point{static_cast<double>(interior_width + 2), static_cast<double>(y),
                                 right.at(interior_width + 2, y)};
                };
                emit(right, {boundary(j), boundary(j + 1), inside(j)}, focus, stitched, true);
                emit(right, {boundary(j + 1), boundary(j + 2), inside(j)}, focus, stitched, true);
                emit(right, {boundary(j + 2), inside(j + 2), inside(j)}, focus, stitched, true);
            }
        }
    } else if (adaptive_halo < 0) {
        // Original edge-only ribbon: three triangles for two fine intervals.
        for (int j = 0; j < grid; j += 2) {
            const Point b0{0.0, static_cast<double>(j), right.at(0, j)};
            const Point b1{0.0, static_cast<double>(j + 1), right.at(0, j + 1)};
            const Point b2{0.0, static_cast<double>(j + 2), right.at(0, j + 2)};
            const Point i0{2.0, static_cast<double>(j), right.at(2, j)};
            const Point i2{2.0, static_cast<double>(j + 2), right.at(2, j + 2)};
            emit(right, {b0, b1, i0}, focus, stitched, true);
            emit(right, {b1, b2, i0}, focus, stitched, true);
            emit(right, {b2, i2, i0}, focus, stitched, true);
        }
    }
    for (auto* output : {&baseline, &stitched, &reference})
        output->insert(output->end(), left_vertices.begin(), left_vertices.end());
    save(prefix + "-parent.bin", baseline);
    save(prefix + "-children.bin", stitched);
    save(prefix + "-detail.bin", reference);
    if (adaptive_halo >= 0) {
        std::vector<std::array<float, 4>> finer;
        if (!raised) {
            finer_reference(right, focus, finer);
            finer.insert(finer.end(), left_vertices.begin(), left_vertices.end());
            save(prefix + "-ultra.bin", finer);
            std::cout << "twice-fine reference " << finer.size() << " vertices\n";
        }
    }
    std::ofstream info(prefix + "-focus.txt");
    info.precision(17);
    info << focus.x << ' ' << focus.y << ' ' << focus.z << '\n';
    std::cout << "L" << level << " seam 0/" << level << '/' << left_key.x << '/'
              << left_key.y << " crossings " << fine_crossings << '/' << coarse_crossings
              << " max shared height gap " << max_boundary_height_gap_m
               << " m; fine interior width " << interior_width << " far ribbon " << far_ribbon
               << " adaptive halo " << adaptive_halo << " conforming " << conforming
               << " raised " << raised << " coastal/refined squares "
               << coastal_squares << '/' << refined_squares
               << " focus interval " << crossing
               << "; far-side odd class flips " << transition_odd_flips
               << " max flat-sphere chord gap " << transition_chord_gap_m << " m;"
               << " baseline/stitched/reference "
              << baseline.size() << '/' << stitched.size() << '/' << reference.size() << " vertices\n";
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) return 2;
    const auto level = static_cast<std::uint32_t>(std::stoul(argv[1]));
    if (level != 4 && level != 6) return 2;
    Scenario scenario;
    scenario.seed = 20260928U;
    scenario.subdivision = level;
    const PlanetRun run(scenario);
    auto mesh = std::make_shared<const PlanetMesh>(run.state().mesh());
    const auto terrain = run.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    if (argc == 4) {
        const std::string mode(argv[3]);
        int width = 0;
        int adaptive_halo = -1;
        const bool conforming = mode == "adaptive1s" || mode == "adaptive1sr";
        const bool raised = mode == "adaptive1r" || mode == "adaptive1sr";
        const bool far_ribbon = mode == "seam2b" || mode == "seam4b" || mode == "seam6b";
        if (mode == "seam2") width = 2;
        else if (mode == "seam4") width = 4;
        else if (mode == "seam8") width = 8;
        else if (mode == "seam2b") width = 2;
        else if (mode == "seam4b") width = 4;
        else if (mode == "seam6b") width = 6;
        else if (mode == "adaptive0") adaptive_halo = 0;
        else if (mode == "adaptive1") adaptive_halo = 1;
        else if (mode == "adaptive1r") adaptive_halo = 1;
        else if (conforming) adaptive_halo = 1;
        else if (mode != "seam") return 2;
        seam(relief, terrain.sea_level_m, level, argv[2], width, far_ribbon,
             adaptive_halo, conforming, raised);
        return 0;
    }
    const TileKey key = level == 4 ? TileKey{0, 4, 7, 0} : TileKey{0, 6, 16, 0};
    const Patch parent(relief, key, terrain.sea_level_m);
    int focus_x = 0, focus_y = 0;
    double nearest = std::numeric_limits<double>::infinity();
    for (int y = 0; y < grid; ++y) for (int x = 0; x < grid; ++x) {
        const std::array<double, 4> h{parent.at(x, y), parent.at(x + 1, y),
                                      parent.at(x, y + 1), parent.at(x + 1, y + 1)};
        const bool coast = std::any_of(h.begin(), h.end(), [&](double value) {
            return (value <= 0.0) != (h[0] <= 0.0);
        });
        const double distance = std::hypot(x - grid / 2.0, y - grid / 2.0);
        if (coast && distance < nearest) { nearest = distance; focus_x = x; focus_y = y; }
    }
    if (!std::isfinite(nearest)) throw std::runtime_error("chosen tile has no coast");
    const Vec3d focus = parent.direction(focus_x + 0.5, focus_y + 0.5);
    std::vector<std::array<float, 4>> parent_vertices, child_vertices, detail_vertices;
    region(parent, 1, focus, parent_vertices);
    for (int cy = 0; cy < 2; ++cy) for (int cx = 0; cx < 2; ++cx) {
        const TileKey child{key.face, static_cast<std::uint8_t>(key.level + 1),
                            key.x * 2U + static_cast<std::uint32_t>(cx),
                            key.y * 2U + static_cast<std::uint32_t>(cy)};
        const Patch independent(relief, child, terrain.sea_level_m);
        region(independent, 2, focus, child_vertices);
        region(independent, 1, focus, detail_vertices);
    }
    const std::string prefix = argv[2];
    save(prefix + "-parent.bin", parent_vertices);
    save(prefix + "-children.bin", child_vertices);
    save(prefix + "-detail.bin", detail_vertices);
    std::ofstream info(prefix + "-focus.txt");
    info.precision(17);
    info << focus.x << ' ' << focus.y << ' ' << focus.z << '\n';
    std::cout << "L" << level << " tile 0/" << level << '/' << key.x << '/' << key.y
              << " focus grid " << focus_x << '/' << focus_y << " direction " << focus.x << '/'
              << focus.y << '/' << focus.z << " parent/children vertices "
              << parent_vertices.size() << '/' << child_vertices.size()
              << " detail " << detail_vertices.size() << "\n";
}
