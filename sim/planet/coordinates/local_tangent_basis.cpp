#include "sim/planet/coordinates/local_tangent_basis.hpp"

#include <algorithm>
#include <cmath>

namespace planetsim {

LocalTangentBasis make_local_tangent_basis(const Vec3d& surface_normal) {
    LocalTangentBasis result;
    result.up_unit = normalized(surface_normal);

    const double horizontal_length = std::hypot(result.up_unit.x, result.up_unit.y);
    if (horizontal_length > 1.0e-12) {
        result.east_unit = {
            -result.up_unit.y / horizontal_length,
            result.up_unit.x / horizontal_length,
            0.0,
        };
    } else {
        result.east_unit = {0.0, 1.0, 0.0};
    }
    result.north_unit = normalized(cross(result.up_unit, result.east_unit));
    return result;
}

double latitude_rad(const Vec3d& surface_normal) {
    const Vec3d unit = normalized(surface_normal);
    return std::asin(std::clamp(unit.z, -1.0, 1.0));
}

double longitude_rad(const Vec3d& surface_normal) {
    const Vec3d unit = normalized(surface_normal);
    return std::atan2(unit.y, unit.x);
}

}  // namespace planetsim
