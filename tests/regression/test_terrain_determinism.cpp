#include "sim/core/serialization/snapshot_file.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "tests/test_support.hpp"

#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace {

template <typename T> [[nodiscard]] bool same_bytes(std::span<const T> left, std::span<const T> right) {
    return left.size() == right.size() &&
           (left.empty() || std::memcmp(left.data(), right.data(), left.size_bytes()) == 0);
}

template <typename T>
[[nodiscard]] bool same_field(const planetsim::Field2D<T>& left, const planetsim::Field2D<T>& right) {
    return same_bytes(left.values(), right.values());
}

[[nodiscard]] bool same_bits(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] bool same_hypsometry(const planetsim::Field3D<float>& left,
                                   const planetsim::Field3D<float>& right) {
    if (left.layer_count() != right.layer_count() || left.cell_count() != right.cell_count()) {
        return false;
    }
    for (std::size_t layer = 0; layer < left.layer_count(); ++layer) {
        if (!same_bytes(left.layer(layer), right.layer(layer))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_vec(const planetsim::Vec3d& left, const planetsim::Vec3d& right) {
    return same_bits(left.x, right.x) && same_bits(left.y, right.y) && same_bits(left.z, right.z);
}

[[nodiscard]] bool same_geology(const planetsim::GeologyState& left,
                                const planetsim::GeologyState& right) {
    if (left.plates.size() != right.plates.size() ||
        left.boundaries.size() != right.boundaries.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.plates.size(); ++index) {
        const auto& a = left.plates[index];
        const auto& b = right.plates[index];
        if (a.seed_cell != b.seed_cell || !same_vec(a.rotation_pole_unit, b.rotation_pole_unit) ||
            !same_bits(a.angular_speed_rad_s, b.angular_speed_rad_s) ||
            !same_bits(a.growth_cost_factor, b.growth_cost_factor) ||
            !same_bits(a.continental_bias, b.continental_bias) || !same_bits(a.area_m2, b.area_m2) ||
            a.cell_count != b.cell_count) {
            return false;
        }
    }
    for (std::size_t index = 0; index < left.boundaries.size(); ++index) {
        const auto& a = left.boundaries[index];
        const auto& b = right.boundaries[index];
        if (a.edge != b.edge || a.first_cell != b.first_cell || a.second_cell != b.second_cell ||
            a.first_plate != b.first_plate || a.second_plate != b.second_plate ||
            a.boundary_class != b.boundary_class || !same_bits(a.convergence_m_s, b.convergence_m_s) ||
            !same_bits(a.tangential_m_s, b.tangential_m_s) || a.first_crust != b.first_crust ||
            a.second_crust != b.second_crust) {
            return false;
        }
    }
    return same_field(left.plate_id, right.plate_id) &&
           same_field(left.plate_velocity_east_m_s, right.plate_velocity_east_m_s) &&
           same_field(left.plate_velocity_north_m_s, right.plate_velocity_north_m_s) &&
           same_field(left.crust_type, right.crust_type) &&
           same_field(left.crust_age_s, right.crust_age_s) &&
           same_field(left.nearest_boundary_class, right.nearest_boundary_class) &&
           same_field(left.nearest_boundary_distance_m, right.nearest_boundary_distance_m) &&
           same_field(left.structural_elevation_m, right.structural_elevation_m);
}

[[nodiscard]] bool same_depression(const planetsim::DepressionRecord& left,
                                   const planetsim::DepressionRecord& right) {
    return left.id == right.id &&
           left.minimum_cell == right.minimum_cell &&
           left.spill_cell == right.spill_cell &&
           same_bits(left.spill_level_m, right.spill_level_m) &&
           same_bits(left.raised_area_m2, right.raised_area_m2) &&
           same_bits(left.maximum_fill_depth_m,
                     right.maximum_fill_depth_m) &&
           left.cell_count == right.cell_count;
}

[[nodiscard]] bool same_drainage(const planetsim::DrainageState& left,
                                 const planetsim::DrainageState& right) {
    if (left.surface.depressions.size() !=
        right.surface.depressions.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < left.surface.depressions.size(); ++index) {
        if (!same_depression(left.surface.depressions[index],
                             right.surface.depressions[index])) {
            return false;
        }
    }
    const auto& a = left.diagnostics;
    const auto& b = right.diagnostics;
    return same_field(left.surface.land_fraction,
                      right.surface.land_fraction) &&
           same_field(left.surface.outlet, right.surface.outlet) &&
           same_field(left.surface.drainage_elevation_m,
                      right.surface.drainage_elevation_m) &&
           same_field(left.surface.filled_elevation_m,
                      right.surface.filled_elevation_m) &&
           same_field(left.surface.depression_id,
                      right.surface.depression_id) &&
           left.surface.terminal_sink == right.surface.terminal_sink &&
           left.surface.outlet_count == right.surface.outlet_count &&
           same_bits(left.surface.maximum_fill_depth_m,
                     right.surface.maximum_fill_depth_m) &&
           same_field(left.downstream, right.downstream) &&
           same_field(left.basin_id, right.basin_id) &&
           same_field(left.catchment_area_m2,
                      right.catchment_area_m2) &&
           a.basin_count == b.basin_count && a.land_basin_count == b.land_basin_count &&
           a.invalid_downstream_count == b.invalid_downstream_count &&
           a.cycle_count == b.cycle_count &&
           a.unreachable_cell_count == b.unreachable_cell_count &&
           same_bits(a.total_routed_land_area_m2,
                     b.total_routed_land_area_m2) &&
           same_bits(a.terminal_catchment_area_m2,
                     b.terminal_catchment_area_m2) &&
           same_bits(a.catchment_closure_relative_error,
                     b.catchment_closure_relative_error) &&
           same_bits(a.largest_catchment_area_m2,
                     b.largest_catchment_area_m2) &&
           same_bits(a.maximum_catchment_storage_error_m2,
                     b.maximum_catchment_storage_error_m2);
}

}  // namespace

int main() {
    planetsim::test::Context test;
    constexpr std::uint64_t seed = 0x7E44'A1Eu;
    const planetsim::GeologyParameters parameters;

    for (const std::uint32_t level : {4U, 5U}) {
        const auto mesh = std::make_shared<const planetsim::PlanetMesh>(
            planetsim::make_icosphere(level, 6'371'000.0));

        // A1: identical for 1, 2, 8 and 16 workers, and on repetition.
        planetsim::PlanetState reference(mesh);
        const auto reference_generation =
            planetsim::generate_terrain(reference, seed, parameters, 1U);
        for (const std::size_t workers : {1U, 2U, 8U, 16U}) {
            planetsim::PlanetState candidate(mesh);
            const auto generation = planetsim::generate_terrain(candidate, seed, parameters, workers);
            PLANETSIM_EXPECT(test, same_hypsometry(reference.slow().hypsometry_m,
                                                   candidate.slow().hypsometry_m));
            PLANETSIM_EXPECT(test,
                             same_bits(reference.slow().sea_level_m, candidate.slow().sea_level_m));
            PLANETSIM_EXPECT(test, same_geology(reference_generation.geology, generation.geology));
            PLANETSIM_EXPECT(test, same_bits(reference_generation.sea_level.achieved_land_fraction,
                                             generation.sea_level.achieved_land_fraction));
            PLANETSIM_EXPECT(test, same_drainage(
                                       reference_generation.drainage, generation.drainage));
        }

        // A2: a different seed gives a different planet.
        planetsim::PlanetState other(mesh);
        static_cast<void>(planetsim::generate_terrain(other, seed + 1U, parameters, 4U));
        PLANETSIM_EXPECT(test, !same_hypsometry(reference.slow().hypsometry_m,
                                                other.slow().hypsometry_m));

        // A12: snapshot round trip is bit-identical.
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("planetsim_terrain_L" + std::to_string(level) + ".psnap");
        planetsim::write_snapshot(path, reference, 0);
        planetsim::PlanetState loaded(mesh);
        static_cast<void>(planetsim::read_snapshot(path, loaded));
        PLANETSIM_EXPECT(test, same_hypsometry(reference.slow().hypsometry_m,
                                               loaded.slow().hypsometry_m));
        PLANETSIM_EXPECT(test, same_bits(reference.slow().sea_level_m, loaded.slow().sea_level_m));
        const planetsim::DrainageState regenerated = planetsim::generate_drainage(
            *mesh, loaded.slow().hypsometry_m, loaded.slow().sea_level_m, 16U);
        PLANETSIM_EXPECT(
            test, same_drainage(reference_generation.drainage, regenerated));
        std::filesystem::remove(path);
    }

    return test.result();
}
