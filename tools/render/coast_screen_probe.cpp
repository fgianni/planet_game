// ADR-0030 Amendment 1, task 2 (probe only; no tile, shader or state
// change): the coast drawn from canonical pages against the canonical field
// itself, in screen space.
//
// A nadir pinhole camera over a coastal focus casts one ray per pixel centre
// onto the unit sphere (a flat footprint: raised geometry is a later gate).
// The reference classifies each pixel by the canonical field s at its hit
// point; the candidate reads the bilinear page value there, from the
// coarsest page level with at least two samples per pixel, and again one
// level coarser (the shortfall while children load).
//
// A differing pixel is "deep" when s has its reference sign at the pixel
// centre and at eight points on a circle of one pixel's radius around it:
// the canonical coast is then more than about a pixel away, which the
// screen-space gate forbids. Differences are also grouped into 8-connected
// components; one of at least four pixels is a candidate island or inlet
// that appears or disappears with the representation.
//
// With an antialiasing factor A > 1 (the fourth argument), each pixel casts
// A x A rays: the reference and the candidate are coverages, a pixel is
// water at coverage above 0.5, and a differing pixel is deep when the
// reference coverage of all eight neighbours is on the centre's side of 0.5.
// The sum of |coverage difference| over the image is reported too, in
// pixels.
//
// Build (Release): c++ -std=c++20 -O3 -ffp-contract=off -I . \
//   tools/render/coast_screen_probe.cpp build/libPlanetSimPresentation.a \
//   build/libPlanetSim.a -lzstd -pthread -o build/coast_screen_probe
// Run: ./build/coast_screen_probe [level] [seed] [foci] [antialiasing]
#include "sim/planet/run/planet_run.hpp"
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
#include <thread>
#include <tuple>
#include <vector>

namespace {

using namespace planetsim;
using namespace planetsim::presentation;

constexpr int image = 512;
constexpr double fov_rad = 60.0 * std::numbers::pi / 180.0;

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

class PageCache {
public:
    explicit PageCache(const CoastWaterField& field) : field_(field) {}
    const CoastPage& get(const TileKey& key) {
        const auto id = std::make_tuple(key.face, key.level, key.x, key.y);
        {
            std::lock_guard lock(mutex_);
            const auto found = pages_.find(id);
            if (found != pages_.end()) return *found->second;
        }
        auto page = std::make_unique<CoastPage>(make_coast_page(key, field_));
        std::lock_guard lock(mutex_);
        auto [it, inserted] = pages_.try_emplace(id, std::move(page));
        return *it->second;
    }
    [[nodiscard]] std::size_t size() const { return pages_.size(); }

private:
    const CoastWaterField& field_;
    std::mutex mutex_;
    std::map<std::tuple<std::uint8_t, std::uint8_t, std::uint32_t, std::uint32_t>, std::unique_ptr<CoastPage>> pages_;
};

// The bilinear page value at a direction, from the page of `level` holding it.
double page_value(PageCache& cache, const std::array<FaceFrame, 6>& frames, const Vec3d& d, std::uint8_t level) {
    const auto [face, u, v] = face_coordinates(frames, d);
    const double tiles = std::ldexp(1.0, level);
    const double gu = (u + 1.0) / 2.0 * tiles;
    const double gv = (v + 1.0) / 2.0 * tiles;
    const auto last = static_cast<std::uint32_t>(tiles) - 1U;
    const auto x = std::min(static_cast<std::uint32_t>(std::max(gu, 0.0)), last);
    const auto y = std::min(static_cast<std::uint32_t>(std::max(gv, 0.0)), last);
    // Neighbouring rays read the same page: each thread remembers its last.
    thread_local const CoastPage* cached_page = nullptr;
    thread_local const PageCache* last_cache = nullptr;
    const TileKey key{face, level, x, y};
    if (cached_page == nullptr || last_cache != &cache || !(cached_page->key == key)) {
        cached_page = &cache.get(key);
        last_cache = &cache;
    }
    const CoastPage& page = *cached_page;
    const double fx = std::clamp((gu - x) * coast_page_intervals, 0.0, static_cast<double>(coast_page_intervals));
    const double fy = std::clamp((gv - y) * coast_page_intervals, 0.0, static_cast<double>(coast_page_intervals));
    return page.bilinear_signed_water_m(fx, fy);
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

struct Result {
    std::size_t pixels = 0, differing = 0, deep = 0, components4 = 0, deep_components4 = 0, largest = 0;
    double coverage_difference_px = 0.0;
};

int antialiasing = 1;

// A classified image: -1 off the planet, else 0 land or 1 water; with A x A
// rays, the water coverage too.
struct Mask {
    std::vector<int> wet;
    std::vector<double> coverage;
};

Mask render(PageCache& cache, const std::array<FaceFrame, 6>& frames, std::uint8_t level, const Mask& valid,
            const std::vector<Vec3d>& hits, unsigned threads) {
    const auto per = static_cast<std::size_t>(antialiasing * antialiasing);
    Mask drawn{std::vector<int>(valid.wet.size(), -1), std::vector<double>(valid.wet.size(), 0.0)};
    std::atomic<int> next_row{0};
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < threads; ++w) {
        pool.emplace_back([&] {
            for (int row; (row = next_row.fetch_add(1)) < image;) {
                for (int col = 0; col < image; ++col) {
                    const std::size_t i = static_cast<std::size_t>(row) * image + col;
                    if (valid.wet[i] < 0) continue;
                    double wet = 0.0;
                    for (std::size_t k = 0; k < per; ++k)
                        wet += page_value(cache, frames, hits[i * per + k], level) > 0.0 ? 1.0 : 0.0;
                    drawn.coverage[i] = wet / static_cast<double>(per);
                    drawn.wet[i] = per == 1U ? (wet > 0.0 ? 1 : 0) : (drawn.coverage[i] > 0.5 ? 1 : 0);
                }
            }
        });
    }
    for (auto& t : pool) t.join();
    return drawn;
}

// `drawn` against `reference`. A differing pixel is deep when the reference
// is on its centre's side over the one-pixel neighbourhood: with one ray a
// pixel and the field given, from the field on a one-pixel circle; else
// from the reference's eight neighbours.
Result evaluate(const Mask& reference, const Mask& drawn, const Camera& camera, const CoastWaterField* field) {
    const auto per = static_cast<std::size_t>(antialiasing * antialiasing);
    Result result;
    std::vector<std::uint8_t> differs(reference.wet.size(), 0U); // 1 differs, 2 also deep
    for (std::size_t i = 0; i < reference.wet.size(); ++i) {
        if (reference.wet[i] < 0) continue;
        ++result.pixels;
        result.coverage_difference_px += std::abs(drawn.coverage[i] - reference.coverage[i]);
        if (drawn.wet[i] == reference.wet[i]) continue;
        ++result.differing;
        bool same = true;
        if (per == 1U && field != nullptr) {
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
    // 8-connected components of the differences; one of four pixels or more
    // with a deep pixel is a candidate island or inlet.
    std::vector<int> stack;
    for (std::size_t start = 0; start < differs.size(); ++start) {
        if (differs[start] == 0U || differs[start] == 9U) continue;
        std::size_t area = 0;
        bool deep = false;
        stack.assign(1, static_cast<int>(start));
        deep = differs[start] == 2U;
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

std::size_t truth_pixels(const Mask& m) {
    return static_cast<std::size_t>(std::count_if(m.wet.begin(), m.wet.end(), [](int v) { return v >= 0; }));
}

void print(const char* name, const Result& r) {
    std::cout << ' ' << name << "=" << r.differing << '/' << r.deep << '/' << r.components4 << '/'
              << r.deep_components4 << '/' << r.largest << '/' << r.coverage_difference_px;
}

}  // namespace

int main(int argc, char** argv) {
    Scenario scenario;
    scenario.subdivision = argc > 1 ? static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 4U;
    scenario.seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 20260928ULL;
    const int foci = argc > 3 ? std::atoi(argv[3]) : 3;
    antialiasing = argc > 4 ? std::max(1, std::atoi(argv[4])) : 1;
    const auto per = static_cast<std::size_t>(antialiasing * antialiasing);
    const PlanetRun run(scenario);
    const auto mesh = std::make_shared<const PlanetMesh>(run.state().mesh());
    const auto terrain = run.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    const CoastWaterField field(relief, terrain);
    const auto frames = face_frames();
    const unsigned threads = std::max(1U, std::thread::hardware_concurrency());
    const double radius_km = run.parameters().radius_m / 1e3;

    // Coastal foci: the first sign changes along a fixed sequence of short
    // great-circle steps, each bisected onto the canonical contour.
    std::vector<Vec3d> focus_points;
    const double step = 0.25 * relief.cell_spacing_rad();
    for (std::size_t k = 0; focus_points.size() < static_cast<std::size_t>(foci) && k < 100000U; k += 997U) {
        const double z = 1.0 - 2.0 * (static_cast<double>(k % 4096U) + 0.5) / 4096.0;
        const double phi = 2.399963229728653 * static_cast<double>(k);
        const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
        const Vec3d a{r * std::cos(phi), r * std::sin(phi), z};
        const Vec3d east = normalized(cross(Vec3d{0.0, 0.0, 1.0}, a));
        const Vec3d b = normalized(a + east * step);
        const bool wa = field.signed_water_m(a) > 0.0;
        if (wa == (field.signed_water_m(b) > 0.0)) continue;
        Vec3d lo = a, hi = b;
        for (int it = 0; it < 60; ++it) {
            const Vec3d mid = normalized(lo + hi);
            ((field.signed_water_m(mid) > 0.0) == wa ? lo : hi) = mid;
        }
        focus_points.push_back(normalized(lo + hi));
    }

    std::cout.setf(std::ios::fixed);
    std::cout << "L" << scenario.subdivision << " seed " << scenario.seed << ", " << image << "^2 px, FOV 60 deg, "
              << focus_points.size() << " coastal foci, " << antialiasing << "x" << antialiasing << " rays a pixel\n";
    std::cout << "each comparison: differing/deep/components>=4/deep components>=4/largest/coverage difference (px)\n";
    PageCache cache(field);
    constexpr std::array<double, 7> altitudes_km{12000.0, 4000.0, 1000.0, 250.0, 50.0, 10.0, 2.0};
    for (std::size_t f = 0; f < focus_points.size(); ++f) {
        for (const double altitude_km : altitudes_km) {
            const auto start = std::chrono::steady_clock::now();
            const Camera camera = nadir(focus_points[f], altitude_km / radius_km);
            // The nadir pixel's ground size, and the coarsest page level with
            // at least two samples a pixel there.
            const double gsd_rad = altitude_km / radius_km * 2.0 * camera.half_tan / image;
            std::uint8_t level = 0;
            while (level < tile_max_level &&
                   std::numbers::pi / 2.0 / (coast_page_intervals * std::ldexp(1.0, level)) > gsd_rad / 2.0)
                ++level;
            Mask truth{std::vector<int>(static_cast<std::size_t>(image) * image, -1),
                       std::vector<double>(static_cast<std::size_t>(image) * image, 0.0)};
            auto& reference = truth.wet;
            auto& reference_coverage = truth.coverage;
            std::vector<Vec3d> hits(reference.size() * per);
            std::atomic<int> next_row{0};
            std::vector<std::thread> pool;
            for (unsigned w = 0; w < threads; ++w) {
                pool.emplace_back([&] {
                    for (int row; (row = next_row.fetch_add(1)) < image;) {
                        CellId hint{0};
                        for (int col = 0; col < image; ++col) {
                            const std::size_t i = static_cast<std::size_t>(row) * image + col;
                            // The pixel's A x A sub-sample centres; a pixel
                            // counts only if every ray hits the planet.
                            double wet = 0.0;
                            bool all = true;
                            for (int sy = 0; sy < antialiasing && all; ++sy) {
                                for (int sx = 0; sx < antialiasing; ++sx) {
                                    const auto d = camera.hit(col + (sx + 0.5) / antialiasing,
                                                              row + (sy + 0.5) / antialiasing);
                                    if (!d) {
                                        all = false;
                                        break;
                                    }
                                    hits[i * per + static_cast<std::size_t>(sy * antialiasing + sx)] = *d;
                                    const auto s = field.sample(*d, hint);
                                    hint = s.cell;
                                    wet += s.signed_water_m > 0.0 ? 1.0 : 0.0;
                                }
                            }
                            if (!all) continue;
                            reference_coverage[i] = wet / static_cast<double>(per);
                            reference[i] = per == 1U ? (wet > 0.0 ? 1 : 0) : (reference_coverage[i] > 0.5 ? 1 : 0);
                        }
                    }
                });
            }
            for (auto& t : pool) t.join();
            const Mask at = render(cache, frames, level, truth, hits, threads);
            const auto finer_level = static_cast<std::uint8_t>(std::min<int>(level + 1, tile_max_level));
            const Mask finer = render(cache, frames, finer_level, truth, hits, threads);
            const Mask coarser = level > 0U ? render(cache, frames, static_cast<std::uint8_t>(level - 1U), truth, hits, threads) : at;
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::cout << f << " alt_km=" << altitude_km << " gsd_m=" << gsd_rad * radius_km * 1e3
                      << " level=" << int(level) << " px=" << truth_pixels(truth);
            print("field_vs_level", evaluate(truth, at, camera, &field));
            print("field_vs_level-1", evaluate(truth, coarser, camera, &field));
            print("handoff_level-1_to_level", evaluate(at, coarser, camera, nullptr));
            print("handoff_level_to_level+1", evaluate(finer, at, camera, nullptr));
            std::cout << " pages=" << cache.size() << " s=" << seconds << '\n';
        }
    }
    return 0;
}
