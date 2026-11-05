// Feasibility of a per-pixel coast from georeferenced pages of the canonical
// relief (R1-05; probe only, no tile or shader change): what one canonical
// sample costs, how steep the relief is, and so how much of the planet a
// page can certify as all-land or all-water from a coarse sampling.
//
// With L a bound on |grad h| (m per radian), a sample with |h| > L d has no
// sea-level crossing within angular distance d. A page texel of size D is
// certified when its samples' |h| exceed L D / sqrt(2); only uncertified
// texels need finer pages. L here is the measured maximum over dense
// finite differences, times a safety factor: an estimate, not a proof.
//
// Build (Release): c++ -std=c++20 -O3 -ffp-contract=off -I . \
//   tools/render/coast_page_probe.cpp build/libPlanetSimPresentation.a \
//   build/libPlanetSim.a -lzstd -pthread -o build/coast_page_probe
// Run: ./build/coast_page_probe [4|6] [samples]
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numbers>
#include <thread>
#include <vector>

namespace {

// A deterministic, area-uniform direction (Fibonacci sphere).
planetsim::Vec3d fibonacci(std::size_t i, std::size_t n) {
    const double golden = std::numbers::pi * (3.0 - std::sqrt(5.0));
    const double z = 1.0 - 2.0 * (static_cast<double>(i) + 0.5) / static_cast<double>(n);
    const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
    const double phi = golden * static_cast<double>(i);
    return {r * std::cos(phi), r * std::sin(phi), z};
}

}  // namespace

int main(int argc, char** argv) {
    using namespace planetsim;
    Scenario scenario;
    scenario.seed = 20260928U;
    scenario.subdivision = argc > 1 ? static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 4U;
    const std::size_t n = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 400'000U;
    const PlanetRun run(scenario);
    auto mesh = std::make_shared<const PlanetMesh>(make_icosphere(scenario.subdivision, run.parameters().radius_m));
    const auto terrain = run.terrain_snapshot();
    const presentation::TerrainRelief relief(mesh, terrain, scenario.seed);
    const double sea = terrain.sea_level_m;
    const double spacing = relief.cell_spacing_rad();
    const double radius_m = run.parameters().radius_m;
    // The finest octave's wavelength, in radians: the scale of the smallest
    // coast features.
    const double finest = spacing / (presentation::relief_features_per_cell * std::ldexp(1.0, presentation::relief_octaves - 1));
    const unsigned threads = std::max(1U, std::thread::hardware_concurrency());

    // 1. Cost: one thread, then all threads, with warm rank tables.
    const auto timed = [&](unsigned workers, std::size_t count) {
        std::vector<double> sink(workers, 0.0);
        const auto start = std::chrono::steady_clock::now();
        std::vector<std::thread> pool;
        for (unsigned w = 0; w < workers; ++w) {
            pool.emplace_back([&, w] {
                CellId hint{0};
                double s = 0.0;
                for (std::size_t i = w; i < count; i += workers) {
                    const auto sample = relief.sample(fibonacci(i, count), hint);
                    hint = sample.cell;
                    s += sample.elevation_m;
                }
                sink[w] = s;
            });
        }
        for (auto& t : pool) t.join();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    timed(threads, n);   // warm the rank tables
    const double serial_s = timed(1U, n / 4U);
    const double parallel_s = timed(threads, n);

    // 2. Steepness: |grad h| by central differences at a tenth of the finest
    // wavelength, at every point.
    const double step = 0.1 * finest;
    std::vector<double> h(n);
    std::vector<double> gradient(n);
    {
        std::atomic<std::size_t> next{0};
        std::vector<std::thread> pool;
        for (unsigned w = 0; w < threads; ++w) {
            pool.emplace_back([&] {
                CellId hint{0};
                for (std::size_t i; (i = next.fetch_add(1)) < n;) {
                    const Vec3d d = fibonacci(i, n);
                    const Vec3d axis = std::abs(d.z) > 0.9 ? Vec3d{1.0, 0.0, 0.0} : Vec3d{0.0, 0.0, 1.0};
                    const Vec3d e1 = normalized(cross(d, axis));
                    const Vec3d e2 = cross(d, e1);
                    const auto at = [&](const Vec3d& offset) {
                        const auto sample = relief.sample(normalized(d + offset), hint);
                        hint = sample.cell;
                        return sample.elevation_m;
                    };
                    h[i] = at({0.0, 0.0, 0.0}) - sea;
                    const double gx = (at(e1 * step) - at(e1 * -step)) / (2.0 * step);
                    const double gy = (at(e2 * step) - at(e2 * -step)) / (2.0 * step);
                    gradient[i] = std::sqrt(gx * gx + gy * gy);
                }
            });
        }
        for (auto& t : pool) t.join();
    }
    std::vector<double> sorted = gradient;
    std::sort(sorted.begin(), sorted.end());
    const auto quantile = [&](double q) { return sorted[static_cast<std::size_t>(q * static_cast<double>(n - 1U))]; };
    const double maximum = sorted.back();
    std::size_t water = 0;
    for (double v : h) water += v <= 0.0 ? 1U : 0U;

    std::cout.setf(std::ios::fixed);
    std::cout.precision(3);
    std::cout << "level=" << scenario.subdivision << " cells=" << mesh->cell_count()
              << " cell_spacing_km=" << spacing * radius_m / 1e3 << " finest_wavelength_km=" << finest * radius_m / 1e3
              << " water_share=" << static_cast<double>(water) / static_cast<double>(n) << '\n';
    std::cout.precision(2);
    std::cout << "sample_us serial=" << 1e6 * serial_s / static_cast<double>(n / 4U)
              << " parallel=" << 1e6 * parallel_s / static_cast<double>(n) << " threads=" << threads << '\n';
    std::cout << "grad_m_per_km p50=" << quantile(0.5) / radius_m * 1e3 << " p99=" << quantile(0.99) / radius_m * 1e3
              << " p99.99=" << quantile(0.9999) / radius_m * 1e3 << " max=" << maximum / radius_m * 1e3 << '\n';

    // 3. Certification: the share of the planet whose texels of size D cannot
    // be certified from one sample with L = factor x the measured maximum.
    for (const double factor : {1.0, 2.0}) {
        const double lipschitz = factor * maximum;
        std::cout << "L=" << factor << "x_max uncertified_share:";
        for (const double texel_km : {100.0, 30.0, 10.0, 3.0, 1.0, 0.3, 0.1}) {
            const double texel = texel_km * 1e3 / radius_m;
            std::size_t open = 0;
            for (double v : h) open += std::abs(v) <= lipschitz * texel / std::sqrt(2.0) ? 1U : 0U;
            std::cout << ' ' << texel_km << "km=" << static_cast<double>(open) / static_cast<double>(n);
        }
        std::cout << '\n';
    }
    return 0;
}
