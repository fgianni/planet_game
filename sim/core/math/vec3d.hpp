#pragma once

#include <cmath>
#include <stdexcept>

namespace planetsim {

struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    [[nodiscard]] constexpr Vec3d operator+(const Vec3d& other) const noexcept {
        return {x + other.x, y + other.y, z + other.z};
    }

    [[nodiscard]] constexpr Vec3d operator-(const Vec3d& other) const noexcept {
        return {x - other.x, y - other.y, z - other.z};
    }

    [[nodiscard]] constexpr Vec3d operator*(double scale) const noexcept {
        return {x * scale, y * scale, z * scale};
    }

    [[nodiscard]] constexpr Vec3d operator/(double scale) const noexcept {
        return {x / scale, y / scale, z / scale};
    }
};

[[nodiscard]] constexpr double dot(const Vec3d& left, const Vec3d& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] constexpr Vec3d cross(const Vec3d& left, const Vec3d& right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] inline double length(const Vec3d& vector) noexcept {
    return std::sqrt(dot(vector, vector));
}

[[nodiscard]] inline Vec3d normalized(const Vec3d& vector) {
    const double magnitude = length(vector);
    if (!(magnitude > 0.0) || !std::isfinite(magnitude)) {
        throw std::invalid_argument("cannot normalize a zero or non-finite vector");
    }
    return vector / magnitude;
}

[[nodiscard]] inline bool is_finite(const Vec3d& vector) noexcept {
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

}  // namespace planetsim
