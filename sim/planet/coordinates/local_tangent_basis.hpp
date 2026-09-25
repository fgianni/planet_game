#pragma once

#include "sim/core/math/vec3d.hpp"

namespace planetsim {

struct LocalTangentBasis {
    Vec3d east_unit;
    Vec3d north_unit;
    Vec3d up_unit;
};

[[nodiscard]] LocalTangentBasis make_local_tangent_basis(const Vec3d& surface_normal);
[[nodiscard]] double latitude_rad(const Vec3d& surface_normal);
[[nodiscard]] double longitude_rad(const Vec3d& surface_normal);

}  // namespace planetsim
