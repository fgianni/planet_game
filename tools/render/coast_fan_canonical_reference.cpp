// Probe-only, pixel-centred canonical sea-level reference for the fixed
// nadir fan camera. Not a mesh or a production renderer. A unit-sphere ray
// is the default; optional `raised` intersects the sampled relief surface
// along the ray. This is still a fixed-pose diagnostic, not a certified
// visibility solver for grazing rays or multiple terrain occluders.
// Build with C++20, -O3, -ffp-contract=off, -I ., the two PlanetSim static
// libraries, -lzstd and -pthread (as coast_cell_fan_spike.cpp).
// Usage: coast_fan_canonical_reference 4|6 seed prefix altitude_radii [fov_deg [raised]]
// Writes prefix-altitude-fov[-raised]-canonical.ppm (512x512).
#include "sim/planet/run/planet_run.hpp"
#include "sim/presentation/terrain_relief.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    using namespace planetsim;
    using namespace planetsim::presentation;
    if (argc < 5 || argc > 7) return 2;
    const auto level = static_cast<std::uint32_t>(std::stoul(argv[1]));
    if (level != 4 && level != 6) return 2;
    const std::string prefix = argv[3];
    const double altitude = std::stod(argv[4]);
    const double fov = argc >= 6 ? std::stod(argv[5]) : 60.0;
    const bool raised = argc == 7 && std::string(argv[6]) == "raised";
    if (argc == 7 && !raised) return 2;
    if (!std::isfinite(altitude) || altitude <= 0 || !std::isfinite(fov) ||
        fov <= 0 || fov >= 180) return 2;
    std::ifstream focus_file(prefix + "-focus.txt");
    Vec3d focus{};
    if (!(focus_file >> focus.x >> focus.y >> focus.z))
        throw std::runtime_error("missing focus");
    focus = normalized(focus);
    Scenario scenario;
    scenario.seed = std::stoull(argv[2]);
    scenario.subdivision = level;
    const PlanetRun planet(scenario);
    const auto mesh = std::make_shared<const PlanetMesh>(planet.state().mesh());
    const auto terrain = planet.terrain_snapshot();
    const TerrainRelief relief(mesh, terrain, scenario.seed);
    if (terrain.hypsometry_m.empty()) throw std::runtime_error("missing hypsometry");

    const Vec3d up = std::abs(focus.y) < 0.9 ? Vec3d{0.0, 1.0, 0.0} : Vec3d{0.0, 0.0, -1.0};
    const Vec3d north = normalized(up - focus * dot(up, focus));
    const Vec3d east = cross(north, focus);
    constexpr int size = 512;
    const double focal = size / (2.0 * std::tan(fov * std::numbers::pi / 360.0));
    const Vec3d camera = focus * (1.0 + altitude);
    const double radius_squared = dot(camera, camera) - 1.0;
    std::string fov_label = argc >= 6 ? argv[5] : "60";
    std::replace(fov_label.begin(), fov_label.end(), '.', '_');
    const std::string output_name = prefix + '-' + argv[4] + '-' + fov_label +
                                    (raised ? "-raised" : "") + "-canonical.ppm";
    std::ofstream output(output_name, std::ios::binary);
    if (!output) throw std::runtime_error("cannot write canonical reference");
    output << "P6\n" << size << ' ' << size << "\n255\n";
    std::size_t wet_count = 0, dry_count = 0;
    std::size_t shifted_hits = 0;
    double max_ray_shift_m = 0.0;
    const double max_height = std::max(0.0, static_cast<double>(*std::max_element(
        terrain.hypsometry_m.begin(), terrain.hypsometry_m.end())) - terrain.sea_level_m) /
        mesh->radius_m();
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const double right = (x + 0.5 - size / 2.0) / focal;
            const double above = (size / 2.0 - y - 0.5) / focal;
            const Vec3d ray = normalized(focus * -1.0 + east * right + north * above);
            const double along = dot(camera, ray);
            const double discriminant = along * along - radius_squared;
            std::array<unsigned char, 3> color{0, 0, 0};
            if (discriminant >= 0) {
                const double near = -along - std::sqrt(discriminant);
                if (near > 0) {
                    double hit = near;
                    if (raised && max_height > 0.0) {
                        // The maximum sampled hypsometric height bounds the
                        // relief. March from its outer sphere to the sea
                        // sphere, then bisect the first sampled sign change.
                        const double outer = -along - std::sqrt(
                            discriminant + 2.0 * max_height + max_height * max_height);
                        const auto surface_difference = [&](double distance) {
                            const Vec3d position = camera + ray * distance;
                            const Vec3d direction = normalized(position);
                            const double height = std::max(0.0,
                                relief.sample(direction).elevation_m - terrain.sea_level_m) /
                                mesh->radius_m();
                            return length(position) - 1.0 - height;
                        };
                        double previous = outer;
                        for (int step = 1; step <= 16; ++step) {
                            const double next = outer + (near - outer) * step / 16.0;
                            if (surface_difference(next) <= 0) {
                                double left = previous, right = next;
                                for (int iteration = 0; iteration < 20; ++iteration) {
                                    const double middle = (left + right) * 0.5;
                                    if (surface_difference(middle) > 0) left = middle;
                                    else right = middle;
                                }
                                hit = right;
                                break;
                            }
                            previous = next;
                        }
                    }
                    if (near - hit > 1e-10) {
                        ++shifted_hits;
                        max_ray_shift_m = std::max(max_ray_shift_m,
                                                    (near - hit) * mesh->radius_m());
                    }
                    const Vec3d direction = normalized(camera + ray * hit);
                    const bool wet = relief.sample(direction).elevation_m <= terrain.sea_level_m;
                    color = wet ? std::array<unsigned char, 3>{6, 63, 242} :
                                  std::array<unsigned char, 3>{242, 76, 6};
                    if (wet) ++wet_count;
                    else ++dry_count;
                }
            }
            output.write(reinterpret_cast<const char*>(color.data()), color.size());
        }
    if (!output) throw std::runtime_error("incomplete canonical reference");
    std::cout << output_name << " wet/dry px " << wet_count << '/' << dry_count
              << (raised ? "; raised-hit pixels " + std::to_string(shifted_hits) +
                               ", max earlier ray hit m " + std::to_string(max_ray_shift_m) +
                               "; grazing/multiple-hit unverified\n" :
                           "; unit-sphere ray, no raised-terrain parallax\n");
}
