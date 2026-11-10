// ADR-0030 Amendment 1, task 2 (probe only; no tile, shader or state
// change): the coast drawn from canonical pages against the canonical field
// itself, in screen space.
//
// A nadir pinhole camera over a focus casts A x A rays a pixel onto the unit
// sphere (a flat footprint: raised geometry is a later gate). Each pixel's
// water coverage is computed against the reference, the canonical field s,
// and from pages at the coarsest level with at least two nodes a pixel at
// the nadir, one level coarser and one level finer. A pixel is water at
// coverage above 0.5 (with one ray, at s > 0). A differing pixel is "deep"
// when the reference is on its side over its one-pixel neighbourhood: the
// coast there has moved by more than about a pixel, which the screen-space
// gate forbids. Differences are grouped into 8-connected regions; one of at
// least four pixels with a deep pixel is a candidate island or inlet that
// appears or disappears.
//
// Two page kinds:
// - point: the library's CoastPage, s at each node;
// - coverage: each node holds its footprint's water coverage minus 0.5,
//   from S x S sub-samples of s with S set by the level, so that pages
//   coarser than the relief's finest features hold a filtered coast.
//   The reference then casts up to 64 x 64 rays a pixel, enough to resolve
//   those features, and a pixel's candidate coverage is the mean of the
//   bilinear coverage under its rays. Levels whose nodes resolve the
//   finest features (one sub-sample) keep point pages: s's zero line is
//   more exact than a binary node.
//
// Three focus kinds: coast (points on the canonical contour), seam (coast
// points on cube-face edges), depression (the centres of L6's inland
// depression cells, where no water may appear).
//
// Build (Release): c++ -std=c++20 -O3 -ffp-contract=off -I . \
//   tools/render/coast_screen_probe.cpp build/libPlanetSimPresentation.a \
//   build/libPlanetSim.a -lzstd -pthread -o build/coast_screen_probe
// Run: ./build/coast_screen_probe level seed foci rays point|coverage coast|seam|depression
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "sim/presentation/coast_page.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace {

using namespace planetsim;
using namespace planetsim::presentation;

constexpr int image = 512;
constexpr double fov_rad = 60.0 * std::numbers::pi / 180.0;
constexpr int max_reference_rays = 64;
constexpr int max_footprint_samples = 16;

int antialiasing = 1;
bool coverage_pages = false;
double finest_rad = 0.0;   // the relief's finest octave wavelength
unsigned threads = 1;

template <typename Body>
void parallel_rows(int rows, Body body) {
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < threads; ++w) {
        pool.emplace_back([&] {
            for (int row; (row = next.fetch_add(1)) < rows;) body(row);
        });
    }
    for (auto& t : pool) t.join();
}

struct FaceFrame {
    Vec3d n, u, v;
};

// The frames, recovered from tile_face_direction so they cannot drift from it.
std::array<FaceFrame, 6> face_frames() {
    std::array<FaceFrame, 6> frames{};
    for (std::uint8_t f = 0; f < 6U; ++f) {
        const Vec3d n = tile_face_direction(f, 0.0, 0.0);
        const Vec3d a = tile_face_direction(f, 0.5, 0.0);
        const Vec3d b = tile_face_direction(f, 0.0, 0.5);
        frames[f] = {n, normalized(a / dot(a, n) - n), normalized(b / dot(b, n) - n)};
    }
    return frames;
}

// The face holding a direction (the most aligned one) and its u, v.
std::tuple<std::uint8_t, double, double> face_coordinates(const std::array<FaceFrame, 6>& frames, const Vec3d& d) {
    std::uint8_t face = 0;
    double best = -2.0;
    for (std::uint8_t f = 0; f < 6U; ++f) {
        const double a = dot(d, frames[f].n);
        if (a > best) {
            best = a;
            face = f;
        }
    }
    const Vec3d p = d / best;
    const double u = std::atan(std::clamp(dot(p, frames[face].u), -1.0, 1.0)) * 4.0 / std::numbers::pi;
    const double v = std::atan(std::clamp(dot(p, frames[face].v), -1.0, 1.0)) * 4.0 / std::numbers::pi;
    return {face, u, v};
}

// A page's node values: s (point) or coverage − 0.5 (coverage); water > 0.
struct ProbePage {
    TileKey key;
    std::vector<double> values;   // coast_page_nodes², row-major
    int footprint_samples = 1;
    bool coverage = false;   // the values are coverage − 0.5
    [[nodiscard]] double at(std::uint32_t x, std::uint32_t y) const {
        return values[static_cast<std::size_t>(y) * coast_page_nodes + x];
    }
    [[nodiscard]] double bilinear(double x, double y) const {
        const auto x0 = std::min(static_cast<std::uint32_t>(x), coast_page_intervals - 1U);
        const auto y0 = std::min(static_cast<std::uint32_t>(y), coast_page_intervals - 1U);
        const double tx = x - x0, ty = y - y0;
        return (1 - ty) * ((1 - tx) * at(x0, y0) + tx * at(x0 + 1U, y0)) +
               ty * ((1 - tx) * at(x0, y0 + 1U) + tx * at(x0 + 1U, y0 + 1U));
    }
};

// The sub-samples a node's footprint needs at `level`: its spacing over half
// the finest wavelength, at most max_footprint_samples.
int footprint_samples(std::uint8_t level) {
    const double spacing = std::numbers::pi / 2.0 / (coast_page_intervals * std::ldexp(1.0, level));
    return std::clamp(static_cast<int>(std::ceil(spacing / (finest_rad / 2.0))), 1, max_footprint_samples);
}

ProbePage make_probe_page(const TileKey& key, const CoastWaterField& field) {
    ProbePage page;
    page.key = key;
    page.values.resize(static_cast<std::size_t>(coast_page_nodes) * coast_page_nodes);
    if (!coverage_pages || footprint_samples(key.level) == 1) {
        const CoastPage point = make_coast_page(key, field);
        for (std::uint32_t y = 0; y < coast_page_nodes; ++y)
            for (std::uint32_t x = 0; x < coast_page_nodes; ++x)
                page.values[static_cast<std::size_t>(y) * coast_page_nodes + x] = point.signed_water_m(x, y);
        return page;
    }
    // Coverage of each node's footprint: the square of one node spacing
    // around it, in face coordinates (it reaches past a face edge on the
    // plane's extension, so edge nodes are not yet shared between faces).
    const int samples = footprint_samples(key.level);
    page.footprint_samples = samples;
    page.coverage = true;
    const double du = 2.0 / (coast_page_intervals * std::ldexp(1.0, key.level));
    parallel_rows(static_cast<int>(coast_page_nodes), [&](int y) {
        CellId hint{0};
        const double v0 = coast_page_coordinate(key.y, key.level, static_cast<std::uint32_t>(y));
        for (std::uint32_t x = 0; x < coast_page_nodes; ++x) {
            const double u0 = coast_page_coordinate(key.x, key.level, x);
            int wet = 0;
            for (int b = 0; b < samples; ++b) {
                for (int a = 0; a < samples; ++a) {
                    const double u = u0 + du * ((a + 0.5) / samples - 0.5);
                    const double v = v0 + du * ((b + 0.5) / samples - 0.5);
                    const auto s = field.sample(tile_face_direction(key.face, u, v), hint);
                    hint = s.cell;
                    wet += s.signed_water_m > 0.0 ? 1 : 0;
                }
            }
            page.values[static_cast<std::size_t>(y) * coast_page_nodes + x] =
                static_cast<double>(wet) / (samples * samples) - 0.5;
        }
    });
    return page;
}

class PageCache {
public:
    explicit PageCache(const CoastWaterField& field) : field_(field) {}
    const ProbePage& get(const TileKey& key) {
        const auto id = std::make_tuple(key.face, key.level, key.x, key.y);
        std::lock_guard lock(mutex_);
        const auto found = pages_.find(id);
        if (found != pages_.end()) return *found->second;
        // Built under the lock: a coverage page uses every thread itself.
        auto [it, inserted] = pages_.try_emplace(id, std::make_unique<ProbePage>(make_probe_page(key, field_)));
        return *it->second;
    }
    [[nodiscard]] std::size_t size() const { return pages_.size(); }

private:
    const CoastWaterField& field_;
    std::mutex mutex_;
    std::map<std::tuple<std::uint8_t, std::uint8_t, std::uint32_t, std::uint32_t>, std::unique_ptr<ProbePage>> pages_;
};

struct PageRef {
    const ProbePage* page = nullptr;
    double x = 0.0, y = 0.0;
};

// The page of `level` holding a direction, and the direction's place in it.
PageRef locate(PageCache& cache, const std::array<FaceFrame, 6>& frames, const Vec3d& d, std::uint8_t level) {
    const auto [face, u, v] = face_coordinates(frames, d);
    const double tiles = std::ldexp(1.0, level);
    const double gu = (u + 1.0) / 2.0 * tiles;
    const double gv = (v + 1.0) / 2.0 * tiles;
    const auto last = static_cast<std::uint32_t>(tiles) - 1U;
    const auto x = std::min(static_cast<std::uint32_t>(std::max(gu, 0.0)), last);
    const auto y = std::min(static_cast<std::uint32_t>(std::max(gv, 0.0)), last);
    return {&cache.get({face, level, x, y}),
            std::clamp((gu - x) * coast_page_intervals, 0.0, static_cast<double>(coast_page_intervals)),
            std::clamp((gv - y) * coast_page_intervals, 0.0, static_cast<double>(coast_page_intervals))};
}

struct Camera {
    Vec3d position, forward, right, up;
    double half_tan = std::tan(fov_rad / 2.0);
    // The ray through screen point (sx, sy) in pixels, onto the unit sphere.
    [[nodiscard]] std::optional<Vec3d> hit(double sx, double sy) const {
        const double x = (2.0 * sx / image - 1.0) * half_tan;
        const double y = (2.0 * sy / image - 1.0) * half_tan;
        const Vec3d r = normalized(forward + right * x + up * y);
        const double b = dot(position, r);
        const double c = dot(position, position) - 1.0;
        const double disc = b * b - c;
        if (disc < 0.0) return std::nullopt;
        const double t = -b - std::sqrt(disc);
        if (t <= 0.0) return std::nullopt;
        return normalized(position + r * t);
    }
};

Camera nadir(const Vec3d& focus, double altitude) {
    const Vec3d axis = std::abs(focus.z) > 0.9 ? Vec3d{1.0, 0.0, 0.0} : Vec3d{0.0, 0.0, 1.0};
    const Vec3d right = normalized(cross(axis, focus));
    const Vec3d up = cross(focus, right);
    return {focus * (1.0 + altitude), focus * -1.0, right, up};
}

// A classified image: -1 off the planet, else 0 land or 1 water, with the
// water coverage.
struct Mask {
    std::vector<int> wet;
    std::vector<double> coverage;
};

// Pixels whose every ray (of `rays` x `rays`) hits the planet.
bool all_hit(const Camera& camera, int col, int row) {
    return camera.hit(col + 0.01, row + 0.01) && camera.hit(col + 0.99, row + 0.01) &&
           camera.hit(col + 0.01, row + 0.99) && camera.hit(col + 0.99, row + 0.99);
}

Mask reference_mask(const CoastWaterField& field, const Camera& camera, int rays) {
    Mask m{std::vector<int>(static_cast<std::size_t>(image) * image, -1),
           std::vector<double>(static_cast<std::size_t>(image) * image, 0.0)};
    parallel_rows(image, [&](int row) {
        CellId hint{0};
        for (int col = 0; col < image; ++col) {
            if (!all_hit(camera, col, row)) continue;
            const std::size_t i = static_cast<std::size_t>(row) * image + col;
            int wet = 0;
            for (int sy = 0; sy < rays; ++sy) {
                for (int sx = 0; sx < rays; ++sx) {
                    const auto d = camera.hit(col + (sx + 0.5) / rays, row + (sy + 0.5) / rays);
                    const auto s = field.sample(*d, hint);
                    hint = s.cell;
                    wet += s.signed_water_m > 0.0 ? 1 : 0;
                }
            }
            m.coverage[i] = static_cast<double>(wet) / (rays * rays);
            m.wet[i] = rays == 1 ? wet : (m.coverage[i] > 0.5 ? 1 : 0);
        }
    });
    return m;
}

Mask render(PageCache& cache, const std::array<FaceFrame, 6>& frames, const Camera& camera, std::uint8_t level,
            const Mask& valid) {
    Mask drawn{std::vector<int>(valid.wet.size(), -1), std::vector<double>(valid.wet.size(), 0.0)};
    // Generate the view's pages first, in parallel within each page.
    for (int row = 0; row < image; row += 8)
        for (int col = 0; col < image; col += 8)
            if (const auto d = camera.hit(col + 0.5, row + 0.5)) (void)locate(cache, frames, *d, level);
    parallel_rows(image, [&](int row) {
        for (int col = 0; col < image; ++col) {
            const std::size_t i = static_cast<std::size_t>(row) * image + col;
            if (valid.wet[i] < 0) continue;
            double wet = 0.0;
            for (int sy = 0; sy < antialiasing; ++sy) {
                for (int sx = 0; sx < antialiasing; ++sx) {
                    const auto d = camera.hit(col + (sx + 0.5) / antialiasing, row + (sy + 0.5) / antialiasing);
                    const PageRef ref = locate(cache, frames, *d, level);
                    const double value = ref.page->bilinear(ref.x, ref.y);
                    wet += ref.page->coverage ? std::clamp(value + 0.5, 0.0, 1.0) : (value > 0.0 ? 1.0 : 0.0);
                }
            }
            drawn.coverage[i] = wet / (antialiasing * antialiasing);
            drawn.wet[i] = antialiasing == 1 && !coverage_pages ? (wet > 0.0 ? 1 : 0)
                                                                : (drawn.coverage[i] > 0.5 ? 1 : 0);
        }
    });
    return drawn;
}

struct Result {
    std::size_t pixels = 0, differing = 0, deep = 0, components4 = 0, deep_components4 = 0, largest = 0;
    double coverage_difference_px = 0.0;
};

// `drawn` against `reference`. A differing pixel is deep when the reference
// is on its centre's side over the one-pixel neighbourhood: with one ray a
// pixel and the field given, from the field on a one-pixel circle; else
// from the reference's eight neighbours.
Result evaluate(const Mask& reference, const Mask& drawn, const Camera& camera, const CoastWaterField* field,
                bool single_ray) {
    Result result;
    std::vector<std::uint8_t> differs(reference.wet.size(), 0U); // 1 differs, 2 also deep
    for (std::size_t i = 0; i < reference.wet.size(); ++i) {
        if (reference.wet[i] < 0) continue;
        ++result.pixels;
        result.coverage_difference_px += std::abs(drawn.coverage[i] - reference.coverage[i]);
        if (drawn.wet[i] == reference.wet[i]) continue;
        ++result.differing;
        bool same = true;
        if (single_ray && field != nullptr) {
            const double sx = static_cast<double>(i % image) + 0.5;
            const double sy = static_cast<double>(i / image) + 0.5;
            for (int k = 0; k < 8 && same; ++k) {
                const double a = k * std::numbers::pi / 4.0;
                const auto d = camera.hit(sx + std::cos(a), sy + std::sin(a));
                if (!d) continue;
                same = (field->signed_water_m(*d) > 0.0 ? 1 : 0) == reference.wet[i];
            }
        } else {
            const int px = static_cast<int>(i % image);
            const int py = static_cast<int>(i / image);
            for (int dy = -1; dy <= 1 && same; ++dy) {
                for (int dx = -1; dx <= 1 && same; ++dx) {
                    const int qx = px + dx;
                    const int qy = py + dy;
                    if (qx < 0 || qy < 0 || qx >= image || qy >= image) continue;
                    const std::size_t q = static_cast<std::size_t>(qy) * image + qx;
                    if (reference.wet[q] < 0) continue;
                    same = reference.wet[q] == reference.wet[i];
                }
            }
        }
        differs[i] = same ? 2U : 1U;
        result.deep += same ? 1U : 0U;
    }
    // 8-connected regions of the differences.
    std::vector<int> stack;
    for (std::size_t start = 0; start < differs.size(); ++start) {
        if (differs[start] == 0U || differs[start] == 9U) continue;
        std::size_t area = 0;
        bool deep = differs[start] == 2U;
        stack.assign(1, static_cast<int>(start));
        differs[start] = 9U;
        while (!stack.empty()) {
            const int p = stack.back();
            stack.pop_back();
            ++area;
            const int px = p % image;
            const int py = p / image;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int qx = px + dx;
                    const int qy = py + dy;
                    if (qx < 0 || qy < 0 || qx >= image || qy >= image) continue;
                    const int q = qy * image + qx;
                    if (differs[q] == 1U || differs[q] == 2U) {
                        deep = deep || differs[q] == 2U;
                        differs[q] = 9U;
                        stack.push_back(q);
                    }
                }
            }
        }
        result.largest = std::max(result.largest, area);
        result.components4 += area >= 4U ? 1U : 0U;
        result.deep_components4 += area >= 4U && deep ? 1U : 0U;
    }
    return result;
}

std::size_t water_pixels(const Mask& m) {
    return static_cast<std::size_t>(std::count(m.wet.begin(), m.wet.end(), 1));
}

void print(const char* name, const Result& r) {
    std::cout << ' ' << name << "=" << r.differing << '/' << r.deep << '/' << r.components4 << '/'
              << r.deep_components4 << '/' << r.largest << '/' << r.coverage_difference_px;
}

// Bisect the canonical contour between two directions of opposite class.
Vec3d bisect(const CoastWaterField& field, Vec3d lo, Vec3d hi) {
    const bool wet = field.signed_water_m(lo) > 0.0;
    for (int it = 0; it < 60; ++it) {
        const Vec3d mid = normalized(lo + hi);
        ((field.signed_water_m(mid) > 0.0) == wet ? lo : hi) = mid;
    }
    return normalized(lo + hi);
}

}  // namespace

int main(int argc, char** argv) {
    Scenario scenario;
    scenario.subdivision = argc > 1 ? static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 4U;
    scenario.seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 20260928ULL;
    const int foci = argc > 3 ? std::atoi(argv[3]) : 3;
    antialiasing = argc > 4 ? std::max(1, std::atoi(argv[4])) : 1;
    coverage_pages = argc > 5 && std::string(argv[5]) == "coverage";
    const std::string kind = argc > 6 ? argv[6] : "coast";
    const PlanetRun run(scenario);
    const auto mesh = std::make_shared<const PlanetMesh>(run.state().mesh());
    const auto terrain = run.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    const CoastWaterField field(relief, terrain);
    const auto frames = face_frames();
    threads = std::max(1U, std::thread::hardware_concurrency());
    const double radius_km = run.parameters().radius_m / 1e3;
    finest_rad = relief.cell_spacing_rad() / (relief_features_per_cell * std::ldexp(1.0, relief_octaves - 1));

    std::vector<Vec3d> focus_points;
    if (kind == "coast") {
        // The first sign changes along a fixed sequence of short great-circle
        // steps, each bisected onto the canonical contour.
        const double step = 0.25 * relief.cell_spacing_rad();
        for (std::size_t k = 0; focus_points.size() < static_cast<std::size_t>(foci) && k < 100000U; k += 997U) {
            const double z = 1.0 - 2.0 * (static_cast<double>(k % 4096U) + 0.5) / 4096.0;
            const double phi = 2.399963229728653 * static_cast<double>(k);
            const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
            const Vec3d a{r * std::cos(phi), r * std::sin(phi), z};
            const Vec3d b = normalized(a + normalized(cross(Vec3d{0.0, 0.0, 1.0}, a)) * step);
            if ((field.signed_water_m(a) > 0.0) != (field.signed_water_m(b) > 0.0))
                focus_points.push_back(bisect(field, a, b));
        }
    } else if (kind == "seam") {
        // Sign changes along the u = +1 edge of each face in turn: the coast
        // where it crosses a cube-face seam.
        for (std::uint8_t face = 0; face < 6U && focus_points.size() < static_cast<std::size_t>(foci); ++face) {
            constexpr int steps = 4096;
            Vec3d previous = tile_face_direction(face, 1.0, -1.0);
            for (int k = 1; k <= steps; ++k) {
                const Vec3d d = tile_face_direction(face, 1.0, -1.0 + 2.0 * k / steps);
                if ((field.signed_water_m(previous) > 0.0) != (field.signed_water_m(d) > 0.0)) {
                    focus_points.push_back(bisect(field, previous, d));
                    break;
                }
                previous = d;
            }
        }
    } else {
        // The centres of inland depression cells.
        for (const auto& cell : mesh->cells()) {
            if (focus_points.size() >= static_cast<std::size_t>(foci)) break;
            const auto i = cell.id.to_index();
            if (terrain.land_fraction[i] == 1.0F &&
                terrain.hypsometry_m[i * hypsometry_quantile_count] < terrain.sea_level_m)
                focus_points.push_back(cell.center_unit);
        }
    }

    std::cout.setf(std::ios::fixed);
    std::cout << "L" << scenario.subdivision << " seed " << scenario.seed << ", " << image << "^2 px, FOV 60 deg, "
              << focus_points.size() << ' ' << kind << " foci, " << antialiasing << "x" << antialiasing
              << " rays a pixel, " << (coverage_pages ? "coverage" : "point") << " pages; finest wavelength "
              << finest_rad * radius_km << " km\n";
    std::cout << "each comparison: differing/deep/regions>=4/deep regions>=4/largest/coverage difference (px)\n";
    PageCache cache(field);
    constexpr std::array<double, 7> altitudes_km{12000.0, 4000.0, 1000.0, 250.0, 50.0, 10.0, 2.0};
    for (std::size_t f = 0; f < focus_points.size(); ++f) {
        for (const double altitude_km : altitudes_km) {
            const auto start = std::chrono::steady_clock::now();
            const Camera camera = nadir(focus_points[f], altitude_km / radius_km);
            // The nadir pixel's ground size, and the coarsest page level with
            // at least two nodes a pixel there.
            const double gsd_rad = altitude_km / radius_km * 2.0 * camera.half_tan / image;
            std::uint8_t level = 0;
            while (level < tile_max_level &&
                   std::numbers::pi / 2.0 / (coast_page_intervals * std::ldexp(1.0, level)) > gsd_rad / 2.0)
                ++level;
            // With coverage pages the reference resolves the finest features.
            const int rays = coverage_pages
                ? std::clamp(static_cast<int>(std::ceil(gsd_rad / (finest_rad / 2.0))), antialiasing, max_reference_rays)
                : antialiasing;
            const Mask truth = reference_mask(field, camera, rays);
            const Mask at = render(cache, frames, camera, level, truth);
            const auto finer_level = static_cast<std::uint8_t>(std::min<int>(level + 1, tile_max_level));
            const Mask finer = render(cache, frames, camera, finer_level, truth);
            const Mask coarser = level > 0U ? render(cache, frames, camera, static_cast<std::uint8_t>(level - 1U), truth) : at;
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const bool single = rays == 1;
            std::cout << f << " alt_km=" << altitude_km << " gsd_m=" << gsd_rad * radius_km * 1e3
                      << " level=" << int(level) << " footprint=" << (coverage_pages ? footprint_samples(level) : 1)
                      << " reference_rays=" << rays << " px=" << truth.wet.size() - static_cast<std::size_t>(
                             std::count(truth.wet.begin(), truth.wet.end(), -1))
                      << " water_px=" << water_pixels(truth) << '/' << water_pixels(at);
            print("field_vs_level", evaluate(truth, at, camera, &field, single));
            print("field_vs_level-1", evaluate(truth, coarser, camera, &field, single));
            print("handoff_level-1_to_level", evaluate(at, coarser, camera, nullptr, single));
            print("handoff_level_to_level+1", evaluate(finer, at, camera, nullptr, single));
            std::cout << " pages=" << cache.size() << " s=" << seconds << '\n';
        }
    }
    return 0;
}
