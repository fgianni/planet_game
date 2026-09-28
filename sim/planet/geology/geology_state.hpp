#pragma once

#include "sim/core/fields/field.hpp"
#include "sim/core/math/vec3d.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace planetsim {

class PlateId {
  public:
    using value_type = std::uint16_t;

    constexpr PlateId() noexcept = default;
    explicit constexpr PlateId(value_type value) noexcept : value_(value) {}

    [[nodiscard]] static constexpr PlateId invalid() noexcept {
        return PlateId{std::numeric_limits<value_type>::max()};
    }

    [[nodiscard]] constexpr value_type value() const noexcept { return value_; }
    [[nodiscard]] constexpr std::size_t to_index() const noexcept {
        return static_cast<std::size_t>(value_);
    }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return *this != invalid(); }

    friend constexpr bool operator==(PlateId, PlateId) noexcept = default;
    friend constexpr auto operator<=>(PlateId, PlateId) noexcept = default;

  private:
    value_type value_ = std::numeric_limits<value_type>::max();
};

enum class CrustType : std::uint8_t { oceanic = 0, continental = 1 };

enum class BoundaryClass : std::uint8_t { none = 0, convergent = 1, divergent = 2, transform = 3 };

struct TectonicPlate {
    CellId seed_cell;
    Vec3d rotation_pole_unit;
    double angular_speed_rad_s = 0.0;   // about rotation_pole_unit, right-handed
    double growth_cost_factor = 1.0;    // multiplies the growth edge cost
    double continental_bias = 0.0;      // added to the continental propensity
    double area_m2 = 0.0;
    std::uint32_t cell_count = 0;
};

// One mesh edge between two different plates. Motion is evaluated at the edge
// midpoint m, with the edge normal n tangent at m and pointing from
// first_cell to second_cell. relative_velocity is the second plate's surface
// velocity minus the first plate's. convergence_m_s = -relative_velocity . n
// is positive when the plates close; tangential_m_s = relative_velocity .
// (m x n).
struct PlateBoundaryEdge {
    EdgeId edge;
    CellId first_cell;
    CellId second_cell;
    PlateId first_plate;
    PlateId second_plate;
    BoundaryClass boundary_class = BoundaryClass::none;
    double convergence_m_s = 0.0;
    double tangential_m_s = 0.0;
    CrustType first_crust = CrustType::oceanic;
    CrustType second_crust = CrustType::oceanic;
};

// Plate and crust structure produced by the geological generator. It lives in
// memory only: it is neither registered nor snapshotted (task M2-02 §4.14).
struct GeologyState {
    std::vector<TectonicPlate> plates;
    std::vector<PlateBoundaryEdge> boundaries;

    Field2D<PlateId> plate_id;
    Field2D<float> plate_velocity_east_m_s;
    Field2D<float> plate_velocity_north_m_s;
    Field2D<CrustType> crust_type;
    Field2D<float> crust_age_s;
    Field2D<BoundaryClass> nearest_boundary_class;
    Field2D<float> nearest_boundary_distance_m;
    Field2D<float> structural_elevation_m;  // cell-centre elevation after erosion
};

}  // namespace planetsim
