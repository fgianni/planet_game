#pragma once

#include "sim/planet/geology/geology_state.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace planetsim {

inline constexpr std::array<double, 9> terrain_elevation_percentiles{0.0,  1.0,  5.0,  25.0, 50.0,
                                                                     75.0, 95.0, 99.0, 100.0};

// Generation diagnostics (task M2-02 A11) plus the bathymetry measures of A10.
// Elevations are relative to the reference radius; ocean depths are below
// sea level and count only world-ocean area.
struct TerrainDiagnostics {
    std::uint32_t plate_count = 0;
    std::vector<double> plate_area_fractions;
    std::array<double, 4> boundary_length_m{};   // indexed by BoundaryClass
    std::array<std::uint32_t, 4> boundary_edge_count{};
    double continental_area_fraction = 0.0;
    double oceanic_age_min_myr = 0.0;
    double oceanic_age_max_myr = 0.0;
    double continental_age_min_myr = 0.0;
    double continental_age_max_myr = 0.0;
    // Area-weighted percentiles of the cell mean elevation.
    std::array<double, terrain_elevation_percentiles.size()> mean_elevation_percentiles_m{};
    double sea_level_m = 0.0;
    double land_area_fraction = 0.0;
    std::uint32_t inland_depression_count = 0;   // below-sea components other than the ocean
    double ocean_area_fraction = 0.0;            // of the sphere
    double ocean_shallower_than_200m_fraction = 0.0;  // of the ocean area
    double ocean_deeper_than_4000m_fraction = 0.0;    // of the ocean area
    double ocean_elevation_mean_m = 0.0;
    double ocean_elevation_std_m = 0.0;
};

[[nodiscard]] TerrainDiagnostics compute_terrain_diagnostics(const PlanetMesh& mesh,
                                                             const GeologyState& geology,
                                                             const Field3D<float>& hypsometry_m,
                                                             double sea_level_m,
                                                             std::size_t worker_count = 1U);

}  // namespace planetsim
