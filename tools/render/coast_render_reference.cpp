// R1-05's rendered gates (ADR-0030 Amendment 1): the reference a rendered
// frame of godot/tests/capture_coast_sweep.gd is measured against.
//
//   coast_render_reference foci LEVEL SEED N
//     prints N coastal directions on the canonical contour (the first sign
//     changes along a fixed sequence of short great-circle steps, bisected),
//     one "x,y,z" a line, for capture_coast_sweep.gd's --focus.
//   coast_render_reference reference LEVEL SEED FRAME.txt OUT RAYS
//     casts RAYS x RAYS rays a pixel, with the frame's camera, at the unit
//     sphere and classifies each by the canonical water field s; writes
//     OUT.coverage (float32 water coverage a pixel, row-major) and
//     OUT.visible (uint8: 1 where every ray meets the sphere).
//
// The rays are Godot's (capture_coast_sweep.gd checks the formula against
// project_ray_normal). The sphere is the drawn coast's surface: water is
// drawn at sea level, so the coast lies on it whatever the relief.
//
// Build (Release): c++ -std=c++20 -O3 -ffp-contract=off -I . \
//   tools/render/coast_render_reference.cpp build/libPlanetSimPresentation.a \
//   build/libPlanetSim.a -lzstd -pthread -o build/coast_render_reference
#include "sim/planet/run/planet_run.hpp"
#include "sim/presentation/coast_water_field.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace planetsim;
using namespace planetsim::presentation;

struct Frame {
    int width = 0;
    int height = 0;
    Vec3d position, right, up, back;
    double tangent = 0.0;
};

Frame read_frame(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::map<std::string, std::vector<double>> values;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string key;
        words >> key;
        double v = 0.0;
        while (words >> v) values[key].push_back(v);
    }
    const auto vec = [&](const char* key) {
        const auto& v = values.at(key);
        return Vec3d{v.at(0), v.at(1), v.at(2)};
    };
    Frame frame;
    frame.width = static_cast<int>(values.at("width").at(0));
    frame.height = static_cast<int>(values.at("height").at(0));
    frame.position = vec("position");
    frame.right = vec("right");
    frame.up = vec("up");
    frame.back = vec("back");
    frame.tangent = values.at("tangent").at(0);
    return frame;
}

// Godot's perspective ray through a screen point (pixels, y down), onto the
// unit sphere.
bool hit(const Frame& frame, double sx, double sy, Vec3d& on_sphere) {
    const double aspect = static_cast<double>(frame.width) / frame.height;
    const double nx = 2.0 * sx / frame.width - 1.0;
    const double ny = 1.0 - 2.0 * sy / frame.height;
    const Vec3d r = normalized(frame.right * (nx * frame.tangent * aspect) + frame.up * (ny * frame.tangent) -
                               frame.back);
    const double b = dot(frame.position, r);
    const double c = dot(frame.position, frame.position) - 1.0;
    const double disc = b * b - c;
    if (disc < 0.0) return false;
    const double t = -b - std::sqrt(disc);
    if (t <= 0.0) return false;
    on_sphere = normalized(frame.position + r * t);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "usage: coast_render_reference foci LEVEL SEED N | reference LEVEL SEED FRAME.txt OUT RAYS\n";
        return 2;
    }
    const std::string mode = argv[1];
    Scenario scenario;
    scenario.subdivision = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10));
    scenario.seed = std::strtoull(argv[3], nullptr, 10);
    const PlanetRun run(scenario);
    const auto mesh = std::make_shared<const PlanetMesh>(run.state().mesh());
    const auto terrain = run.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    const CoastWaterField field(relief, terrain);

    if (mode == "foci") {
        const int wanted = std::atoi(argv[4]);
        const double step = 0.25 * relief.cell_spacing_rad();
        int found = 0;
        for (std::size_t k = 0; found < wanted && k < 100000U; k += 997U) {
            const double z = 1.0 - 2.0 * (static_cast<double>(k % 4096U) + 0.5) / 4096.0;
            const double phi = 2.399963229728653 * static_cast<double>(k);
            const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
            Vec3d lo{r * std::cos(phi), r * std::sin(phi), z};
            Vec3d hi = normalized(lo + normalized(cross(Vec3d{0.0, 0.0, 1.0}, lo)) * step);
            const bool wet = field.signed_water_m(lo) > 0.0;
            if (wet == (field.signed_water_m(hi) > 0.0)) continue;
            for (int it = 0; it < 60; ++it) {
                const Vec3d mid = normalized(lo + hi);
                ((field.signed_water_m(mid) > 0.0) == wet ? lo : hi) = mid;
            }
            const Vec3d focus = normalized(lo + hi);
            std::cout.precision(17);
            std::cout << focus.x << ',' << focus.y << ',' << focus.z << '\n';
            ++found;
        }
        return 0;
    }
    if (mode != "reference" || argc < 7) return 2;
    const Frame frame = read_frame(argv[4]);
    const std::string out = argv[5];
    const int rays = std::max(1, std::atoi(argv[6]));
    const auto pixels = static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
    std::vector<float> coverage(pixels, 0.0F);
    std::vector<std::uint8_t> visible(pixels, 0U);
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    const unsigned threads = std::max(1U, std::thread::hardware_concurrency());
    for (unsigned w = 0; w < threads; ++w) {
        pool.emplace_back([&] {
            for (int row; (row = next.fetch_add(1)) < frame.height;) {
                CellId hint{0};
                for (int col = 0; col < frame.width; ++col) {
                    int wet = 0;
                    bool all = true;
                    for (int b = 0; b < rays && all; ++b) {
                        for (int a = 0; a < rays; ++a) {
                            Vec3d d;
                            if (!hit(frame, col + (a + 0.5) / rays, row + (b + 0.5) / rays, d)) {
                                all = false;
                                break;
                            }
                            const auto s = field.sample(d, hint);
                            hint = s.cell;
                            wet += s.signed_water_m > 0.0 ? 1 : 0;
                        }
                    }
                    const std::size_t i = static_cast<std::size_t>(row) * frame.width + col;
                    visible[i] = all ? 1U : 0U;
                    coverage[i] = all ? static_cast<float>(wet) / static_cast<float>(rays * rays) : 0.0F;
                }
            }
        });
    }
    for (auto& t : pool) t.join();
    std::ofstream(out + ".coverage", std::ios::binary)
        .write(reinterpret_cast<const char*>(coverage.data()), static_cast<std::streamsize>(pixels * sizeof(float)));
    std::ofstream(out + ".visible", std::ios::binary)
        .write(reinterpret_cast<const char*>(visible.data()), static_cast<std::streamsize>(pixels));
    return 0;
}
