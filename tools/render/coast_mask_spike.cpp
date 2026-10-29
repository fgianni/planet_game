// Experimental ADR-0030 georeferenced mask; no production renderer changes.
// Build against libPlanetSimPresentation.a and libPlanetSim.a as for the
// other coast probes. Output is a 3x2 face atlas of 8-bit RGB PPM pixels.
#include "sim/planet/run/planet_run.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

int main(int argc, char** argv) {
    using namespace planetsim;
    using namespace planetsim::presentation;
    if (argc != 3) {
        std::cerr << "usage: coast_mask_spike FACE_RESOLUTION output.ppm\n";
        return 2;
    }
    const int side = std::stoi(argv[1]);
    if (side < 16 || side > 2048) return 2;
    Scenario scenario;
    scenario.seed = 20260928U;
    scenario.subdivision = 4;
    const PlanetRun run(scenario);
    auto mesh = std::make_shared<const PlanetMesh>(run.state().mesh());
    const auto terrain = run.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    const int width = side * 3;
    const int height = side * 2;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 3), 0U);
    const auto start = std::chrono::steady_clock::now();
    std::size_t wet = 0;
    std::size_t wrong = 0;
    std::size_t checked = 0;
    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                const double u = -1.0 + 2.0 * (x + 0.5) / side;
                const double v = -1.0 + 2.0 * (y + 0.5) / side;
                const bool water = relief.sample(tile_face_direction(static_cast<std::uint8_t>(face), u, v))
                                       .elevation_m <= terrain.sea_level_m;
                wet += water;
                const auto index = static_cast<std::size_t>(3 * ((face / 3 * side + y) * width +
                                                                   face % 3 * side + x));
                pixels[index] = pixels[index + 1] = pixels[index + 2] = water ? 255U : 0U;
                // Independent offset points within a sparse subset of
                // texels: test topology loss, not just endpoint agreement.
                if (x % 4 == 0 && y % 4 == 0) {
                    const double test_u = -1.0 + 2.0 * (x + 0.8) / side;
                    const double test_v = -1.0 + 2.0 * (y + 0.8) / side;
                    const double h = relief.sample(tile_face_direction(static_cast<std::uint8_t>(face),
                                                                         test_u, test_v)).elevation_m -
                                     terrain.sea_level_m;
                    if (std::abs(h) > 1.0) {
                        wrong += (h <= 0.0) != water;
                        ++checked;
                    }
                }
            }
        }
    }
    std::ofstream file(argv[2], std::ios::binary);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    if (!file) return 1;
    std::cout << side << "x" << side << " per face; " << wet << "/" << 6 * side * side
              << " wet samples; independent offset flips " << wrong << '/' << checked
              << "; " << pixels.size() << " atlas bytes; "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << " s sampling+write\n";
}
