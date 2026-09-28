#pragma once

#include "sim/core/random/noise.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace planetsim {

inline constexpr double seconds_per_julian_year = 365.25 * 86'400.0;
inline constexpr double seconds_per_million_years = 1.0e6 * seconds_per_julian_year;

// Parameters of the plate-scale geological generator (task M2-02 §4).
//
// Every value below is a starting value chosen to give plausible plate-scale
// structure, not calibrated physics. Distances are along-mesh (Dijkstra) path
// lengths, which exceed the geodesic by up to about 17 % on the cell mesh.
// Noise amplitudes multiply fbm(), whose sampled standard deviation is about
// 0.15, not its [-1, 1] bound.
struct GeologyParameters {
    // Plates (§4.3, §4.4).
    std::uint32_t plate_count = 12;                  // allowed 2--40
    // Minimum angular spacing between plate seeds, as a fraction of the
    // characteristic spacing sqrt(4 pi / plate_count) rad (amends §4.3).
    double plate_seed_min_spacing_factor = 0.5;
    double plate_growth_factor_min = 0.6;
    double plate_growth_factor_max = 1.4;
    double plate_growth_noise_amplitude = 0.5;       // a in (1 + a fbm)
    FbmParameters plate_growth_noise{3.0, 4U, 2.0, 0.5};
    double plate_speed_min_m_s = 0.01 / seconds_per_julian_year;   // 1 cm/yr
    double plate_speed_max_m_s = 0.10 / seconds_per_julian_year;   // 10 cm/yr

    // Continental crust (§4.6).
    double continental_area_fraction = 0.40;
    FbmParameters continental_noise{1.5, 4U, 2.0, 0.5};
    double continental_plate_bias_amplitude = 0.25;  // bias uniform in [-A, A]

    // Crust age and ocean depth (§4.7). Parsons & Sclater (1977) below the
    // transition age; above it an exponential approach to the abyssal limit
    // that matches the square-root branch in value and slope.
    double oceanic_age_cap_s = 180.0 * seconds_per_million_years;
    double continental_age_min_s = 500.0 * seconds_per_million_years;
    double continental_age_max_s = 3'500.0 * seconds_per_million_years;
    double ridge_depth_m = 2'500.0;
    double subsidence_m_per_sqrt_myr = 350.0;
    double age_depth_transition_s = 70.0 * seconds_per_million_years;
    double abyssal_depth_limit_m = 6'400.0;

    // Structural elevation (§4.8). Profiles are Gaussians in along-mesh
    // distance with the given half-width at half-maximum; cells adjacent to
    // a boundary are at distance zero.
    double continental_base_elevation_m = 300.0;
    double shelf_edge_elevation_m = -130.0;
    double passive_margin_width_m = 150'000.0;       // continental side
    double continental_slope_width_m = 100'000.0;    // oceanic side
    double collision_mountain_height_m = 3'500.0;
    double collision_mountain_half_width_m = 250'000.0;
    double trench_depth_m = 2'500.0;
    double trench_half_width_m = 60'000.0;
    double cordillera_height_m = 2'500.0;
    double cordillera_half_width_m = 200'000.0;
    double island_arc_height_m = 1'500.0;
    double island_arc_half_width_m = 100'000.0;
    double rift_depth_m = 800.0;
    double rift_half_width_m = 80'000.0;
    double transform_fault_amplitude_m = 200.0;
    double transform_fault_half_width_m = 50'000.0;
    FbmParameters transform_fault_noise{24.0, 3U, 2.0, 0.5};
    // A boundary's structures scale with its relevant relative speed divided
    // by the median for its class, clamped to at most this factor.
    double boundary_speed_scale_max = 2.0;

    // Roughness and diffusive erosion (§4.9).
    double continental_roughness_amplitude_m = 1'500.0;
    double oceanic_roughness_amplitude_m = 500.0;
    FbmParameters roughness_noise{6.0, 6U, 2.0, 0.5};
    std::uint32_t erosion_step_count = 4;
    double erosion_diffusivity_m2_s = 1.0;
    double erosion_step_s = 2.5e9;  // sub-stepped where the explicit step is unstable

    // Sub-cell hypsometry (§4.10).
    double continental_subcell_roughness_amplitude_m = 1'000.0;
    double oceanic_subcell_roughness_amplitude_m = 300.0;
    FbmParameters subcell_roughness_noise{48.0, 4U, 2.0, 0.5};

    // Sea level (§4.11).
    double target_land_fraction = 0.29;
};

enum class PlanetPreset : std::uint8_t { earth_like, aqua_planet, dead_rock };

[[nodiscard]] GeologyParameters geology_parameters_for(PlanetPreset preset);
[[nodiscard]] std::optional<PlanetPreset> parse_planet_preset(std::string_view name) noexcept;
[[nodiscard]] std::string_view planet_preset_name(PlanetPreset preset) noexcept;

// Throws std::invalid_argument when a parameter is outside its valid range.
void validate_geology_parameters(const GeologyParameters& parameters);

}  // namespace planetsim
