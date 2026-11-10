// Headless, fixed-nadir ADR-0030 Amendment 1 diagnostic. No Godot geometry,
// shader, AA, raised-terrain occlusion or production page residency.
// Build: c++ -std=c++20 -O3 -ffp-contract=off -I . <this file>
//        build/libPlanetSimPresentation.a build/libPlanetSim.a -lzstd -pthread -o probe
// Usage: probe level seed focus.txt altitude_radii page_level output_prefix [fov_deg]
// Writes three raw 512x512 byte masks (visible, canonical, page) and metadata.
#include "sim/planet/run/planet_run.hpp"
#include "sim/presentation/coast_page.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace planetsim;
using namespace planetsim::presentation;

struct FacePoint { std::uint8_t face; double u, v; };

// Inverse of tile_face_direction's equal-angle frames; ties are owned by
// the first xyz axis. UV is clamped only for roundoff at a face edge.
FacePoint face_point_on(std::uint8_t face, const Vec3d& d) {
    const double factor = 4.0 / std::numbers::pi;
    switch (face) {
    case 0: return {face, factor * std::atan(-d.z / d.x), factor * std::atan(d.y / d.x)};
    case 1: return {face, factor * std::atan(d.z / -d.x), factor * std::atan(d.y / -d.x)};
    case 2: return {face, factor * std::atan(d.x / d.y), factor * std::atan(-d.z / d.y)};
    case 3: return {face, factor * std::atan(d.x / -d.y), factor * std::atan(d.z / -d.y)};
    case 4: return {face, factor * std::atan(d.x / d.z), factor * std::atan(d.y / d.z)};
    default: return {face, factor * std::atan(-d.x / -d.z), factor * std::atan(d.y / -d.z)};
    }
}

FacePoint face_point(const Vec3d& d) {
    const double ax = std::abs(d.x), ay = std::abs(d.y), az = std::abs(d.z);
    if (ax >= ay && ax >= az) return face_point_on(d.x >= 0 ? 0 : 1, d);
    if (ay >= az) return face_point_on(d.y >= 0 ? 2 : 3, d);
    return face_point_on(d.z >= 0 ? 4 : 5, d);
}

void save(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot open screen mask");
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("incomplete screen mask");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 7 && argc != 8) return 2;
    const auto level = static_cast<std::uint32_t>(std::stoul(argv[1]));
    if (level != 4 && level != 6) return 2;
    const auto seed = std::stoull(argv[2]);
    std::ifstream focus_file(argv[3]);
    Vec3d focus;
    if (!(focus_file >> focus.x >> focus.y >> focus.z))
        throw std::runtime_error("missing focus");
    focus = normalized(focus);
    const double altitude = std::stod(argv[4]);
    const auto requested_page_level = std::stoul(argv[5]);
    const std::string prefix = argv[6];
    const double fov = argc == 8 ? std::stod(argv[7]) : 75.0;
    if (!(altitude > 0.0) || !std::isfinite(altitude) || requested_page_level > tile_max_level ||
        !(fov > 0.0 && fov < 180.0) || !std::isfinite(fov)) return 2;
    const auto page_level = static_cast<std::uint8_t>(requested_page_level);
    for (std::uint8_t face = 0; face < 6; ++face)
        for (const double u : {-0.8, 0.0, 0.8})
            for (const double v : {-0.8, 0.0, 0.8}) {
                const FacePoint inverse = face_point(tile_face_direction(face, u, v));
                if (inverse.face != face || std::abs(inverse.u - u) > 1e-12 ||
                    std::abs(inverse.v - v) > 1e-12)
                    throw std::runtime_error("cube-face inverse failed");
            }
    Scenario scenario;
    scenario.seed = seed;
    scenario.subdivision = level;
    const PlanetRun run(scenario);
    const auto mesh = std::make_shared<const PlanetMesh>(run.state().mesh());
    const auto terrain = run.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, seed);
    const CoastWaterField field(relief, terrain);
    const Vec3d up = std::abs(focus.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{0, 0, -1};
    const Vec3d north = normalized(up - focus * dot(up, focus));
    const Vec3d east = cross(north, focus);
    constexpr int size = 512;
    const double focal = size / (2.0 * std::tan(fov * std::numbers::pi / 360.0));
    const Vec3d camera = focus * (1.0 + altitude);
    const double radius_squared = dot(camera, camera) - 1.0;
    std::vector<std::uint8_t> visible(size * size, 0), canonical(size * size, 0),
                              page_mask(size * size, 0);
    std::map<std::array<std::uint32_t, 4>, CoastPage> pages;
    double min_density = 1e300, max_density = 0.0;
    double generation_ms = 0.0;
    std::size_t insufficient = 0, visible_pixels = 0, flips = 0;
    CellId hint{0};
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        const double right = (x + 0.5 - size / 2.0) / focal;
        const double above = (size / 2.0 - y - 0.5) / focal;
        const Vec3d ray = normalized(focus * -1.0 + east * right + north * above);
        const double along = dot(camera, ray);
        const double discriminant = along * along - radius_squared;
        if (discriminant < 0.0) continue;
        const double near = -along - std::sqrt(discriminant);
        if (near <= 0.0) continue;
        const Vec3d direction = normalized(camera + ray * near);
        const FacePoint face = face_point(direction);
        const double tiles = static_cast<double>(1U << page_level);
        const double gx = std::clamp((face.u + 1.0) * 0.5 * tiles, 0.0, tiles);
        const double gy = std::clamp((face.v + 1.0) * 0.5 * tiles, 0.0, tiles);
        const auto tx = std::min(static_cast<std::uint32_t>(gx), (1U << page_level) - 1U);
        const auto ty = std::min(static_cast<std::uint32_t>(gy), (1U << page_level) - 1U);
        const TileKey key{face.face, page_level, tx, ty};
        const auto id = std::array<std::uint32_t, 4>{key.face, key.level, key.x, key.y};
        auto iter = pages.find(id);
        if (iter == pages.end()) {
            const auto start = std::chrono::steady_clock::now();
            CoastPage generated = make_coast_page(key, field);
            generation_ms += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            iter = pages.emplace(id, std::move(generated)).first;
        }
        const double px = std::clamp((gx - tx) * coast_page_intervals, 0.0,
                                     static_cast<double>(coast_page_intervals));
        const double py = std::clamp((gy - ty) * coast_page_intervals, 0.0,
                                     static_cast<double>(coast_page_intervals));
        const CoastWaterSample exact = field.sample(direction, hint);
        hint = exact.cell;
        const bool wet = exact.signed_water_m > 0.0;
        const bool drawn = iter->second.bilinear_signed_water_m(px, py) > 0.0;
        const std::size_t index = static_cast<std::size_t>(y * size + x);
        visible[index] = 1;
        canonical[index] = static_cast<std::uint8_t>(wet);
        page_mask[index] = static_cast<std::uint8_t>(drawn);
        ++visible_pixels;
        flips += wet != drawn;
        // Minimum x/y screen derivative in page samples per pixel. Continue
        // the current face's coordinates across an edge for this derivative.
        double density = 1e300;
        for (const auto [dr, da] : {std::array<double, 2>{1.0 / focal, 0.0},
                                    std::array<double, 2>{0.0, 1.0 / focal}}) {
            const Vec3d next_ray = normalized(focus * -1.0 + east * (right + dr) +
                                              north * (above + da));
            const double n_along = dot(camera, next_ray);
            const double n_disc = n_along * n_along - radius_squared;
            if (n_disc < 0.0) continue;
            const Vec3d neighbour = normalized(camera + next_ray *
                                               (-n_along - std::sqrt(n_disc)));
            const FacePoint next = face_point_on(face.face, neighbour);
            density = std::min(density, std::hypot(next.u - face.u, next.v - face.v) *
                                        coast_page_intervals * tiles * 0.5);
        }
        if (density == 1e300) continue;
        min_density = std::min(min_density, density);
        max_density = std::max(max_density, density);
        insufficient += density < 2.0;
    }
    save(prefix + "-visible.raw", visible);
    save(prefix + "-canonical.raw", canonical);
    save(prefix + "-page.raw", page_mask);
    std::ofstream info(prefix + "-info.txt");
    info << "size " << size << "\nlevel " << level << "\nseed " << seed
         << "\naltitude " << altitude << "\nfov " << fov
         << "\npage_level " << static_cast<int>(page_level)
         << "\nvisible " << visible_pixels << "\nflips " << flips
         << "\npages " << pages.size() << "\npage_bytes "
         << pages.size() * coast_page_nodes * coast_page_nodes * 2ULL
         << "\ngeneration_ms " << generation_ms
         << "\ndensity_min " << min_density << "\ndensity_max " << max_density
         << "\ndensity_below_2 " << insufficient << '\n';
    if (!info) throw std::runtime_error("incomplete screen metadata");
    std::cout << prefix << ": visible " << visible_pixels << ", flips " << flips
              << ", pages " << pages.size() << ", bytes "
              << pages.size() * coast_page_nodes * coast_page_nodes * 2ULL
              << ", cold page generation " << generation_ms << " ms"
              << ", density below 2 samples/pixel " << insufficient << '\n';
}
