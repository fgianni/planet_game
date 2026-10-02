#pragma once

#include "sim/core/math/vec3d.hpp"
#include "sim/planet/dynamics/shallow_water.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
#include "sim/planet/operators/c_grid.hpp"

namespace planetsim {

// Williamson et al. (1992) shallow-water test cases (ADR-0011 V3, task
// M6-02), on a mesh of Williamson's radius a = 6.37122e6 m.
inline constexpr double williamson_radius_m = 6.37122e6;
inline constexpr double williamson_gravity_m_s2 = 9.80616;
inline constexpr double williamson_rotation_rad_s = 7.292e-5;
inline constexpr double williamson_day_s = 86'400.0;

struct WilliamsonCase {
    ShallowWaterParameters parameters;
    Field2D<double> bottom_height_m;
    ShallowWaterState initial;
};

// Test 2: steady zonal geostrophic flow about the axis ω̂, which is also the
// rotation axis (Williamson's α tilts both), u = u₀ ω̂ × x with
// u₀ = 2πa / 12 days and g h = g h₀ − (a Ω u₀ + u₀²/2)(ω̂ · x)², g h₀ =
// 2.94e4 m²/s². The exact solution is the initial state at every time.
[[nodiscard]] WilliamsonCase williamson_case_2(const PlanetMesh& mesh, const CGridGeometry& grid,
                                               const Vec3d& axis);

// Test 5: zonal flow (u₀ = 20 m/s, h₀ = 5960 m) about +Z impinging on a
// conical mountain 2000 m high, radius π/9, centred at 30° N, 90° W.
[[nodiscard]] WilliamsonCase williamson_case_5(const PlanetMesh& mesh, const CGridGeometry& grid);

// Williamson's normalised errors of h against an exact field at the cells:
// sqrt(Σ A (h − h_T)²) / sqrt(Σ A h_T²) and max |h − h_T| / max |h_T|.
struct ThicknessErrors {
    double l2 = 0.0;
    double linf = 0.0;
};

[[nodiscard]] ThicknessErrors thickness_errors(const PlanetMesh& mesh,
                                               const Field2D<double>& thickness,
                                               const Field2D<double>& exact);

}  // namespace planetsim
