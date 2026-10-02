#include "sim/planet/dynamics/williamson_cases.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace planetsim {
namespace {

// Zonal flow u₀ ω̂ × x in geostrophic balance with f = 2Ω (ω̂ · x): the
// thickness that balances it is h₀ − (a Ω u₀ + u₀²/2)(ω̂ · x)² / g − b.
WilliamsonCase zonal_case(const PlanetMesh& mesh, const CGridGeometry& grid, const Vec3d& axis,
                          double u0, double surface_height_m, Field2D<double> bottom) {
    WilliamsonCase result;
    result.parameters.gravity_m_s2 = williamson_gravity_m_s2;
    result.parameters.rotation_rate_rad_s = williamson_rotation_rad_s;
    result.parameters.rotation_axis = normalized(axis);
    const Vec3d w = result.parameters.rotation_axis;
    const double a = mesh.radius_m();
    const double coefficient =
        (a * williamson_rotation_rad_s * u0 + 0.5 * u0 * u0) / williamson_gravity_m_s2;

    result.initial.thickness_m = Field2D<double>(mesh.cell_count());
    for (const auto& cell : mesh.cells()) {
        const double s = dot(w, cell.center_unit);
        result.initial.thickness_m[cell.id] =
            surface_height_m - coefficient * s * s - bottom[cell.id];
    }
    result.initial.normal_velocity_m_s = EdgeField<double>(mesh.edge_count());
    for (std::size_t index = 0; index < mesh.edge_count(); ++index) {
        const auto& edge = grid.edges()[index];
        result.initial.normal_velocity_m_s.values()[index] =
            u0 * dot(cross(w, edge.midpoint_unit), edge.normal_unit);
    }
    result.bottom_height_m = std::move(bottom);
    return result;
}

}  // namespace

WilliamsonCase williamson_case_2(const PlanetMesh& mesh, const CGridGeometry& grid,
                                 const Vec3d& axis) {
    const double u0 = 2.0 * std::numbers::pi * mesh.radius_m() / (12.0 * williamson_day_s);
    return zonal_case(mesh, grid, axis, u0, 2.94e4 / williamson_gravity_m_s2,
                      Field2D<double>(mesh.cell_count(), 0.0));
}

WilliamsonCase williamson_case_5(const PlanetMesh& mesh, const CGridGeometry& grid) {
    constexpr double pi = std::numbers::pi;
    const double mountain_radius = pi / 9.0;
    const double centre_longitude = 1.5 * pi;
    const double centre_latitude = pi / 6.0;
    Field2D<double> bottom(mesh.cell_count());
    for (const auto& cell : mesh.cells()) {
        const Vec3d& x = cell.center_unit;
        const double latitude = std::asin(std::clamp(x.z, -1.0, 1.0));
        const double longitude = std::atan2(x.y, x.x);
        const double d_longitude = std::remainder(longitude - centre_longitude, 2.0 * pi);
        const double d_latitude = latitude - centre_latitude;
        const double r = std::min(mountain_radius, std::hypot(d_longitude, d_latitude));
        bottom[cell.id] = 2000.0 * (1.0 - r / mountain_radius);
    }
    return zonal_case(mesh, grid, Vec3d{0.0, 0.0, 1.0}, 20.0, 5960.0, std::move(bottom));
}

ThicknessErrors thickness_errors(const PlanetMesh& mesh, const Field2D<double>& thickness,
                                 const Field2D<double>& exact) {
    double error_squared = 0.0;
    double exact_squared = 0.0;
    double max_error = 0.0;
    double max_exact = 0.0;
    for (const auto& cell : mesh.cells()) {
        const double error = thickness[cell.id] - exact[cell.id];
        error_squared += cell.area_m2 * error * error;
        exact_squared += cell.area_m2 * exact[cell.id] * exact[cell.id];
        max_error = std::max(max_error, std::abs(error));
        max_exact = std::max(max_exact, std::abs(exact[cell.id]));
    }
    return {std::sqrt(error_squared / exact_squared), max_error / max_exact};
}

}  // namespace planetsim
