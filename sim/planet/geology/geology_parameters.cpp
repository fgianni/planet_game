#include "sim/planet/geology/geology_parameters.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace planetsim {
namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(std::string("invalid geology parameters: ") + message);
    }
}

[[nodiscard]] bool positive(double value) noexcept { return std::isfinite(value) && value > 0.0; }
[[nodiscard]] bool non_negative(double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

void require_fbm(const FbmParameters& noise, const char* message) {
    require(noise.octaves > 0U && positive(noise.base_frequency) && positive(noise.lacunarity) &&
                positive(noise.gain),
            message);
}

}  // namespace

GeologyParameters geology_parameters_for(PlanetPreset preset) {
    GeologyParameters parameters;
    switch (preset) {
    case PlanetPreset::earth_like:
        break;
    case PlanetPreset::aqua_planet:
        parameters.target_land_fraction = 0.0;
        break;
    case PlanetPreset::dead_rock:
        parameters.target_land_fraction = 1.0;
        break;
    }
    return parameters;
}

std::optional<PlanetPreset> parse_planet_preset(std::string_view name) noexcept {
    if (name == "earth_like") {
        return PlanetPreset::earth_like;
    }
    if (name == "aqua_planet") {
        return PlanetPreset::aqua_planet;
    }
    if (name == "dead_rock") {
        return PlanetPreset::dead_rock;
    }
    return std::nullopt;
}

std::string_view planet_preset_name(PlanetPreset preset) noexcept {
    switch (preset) {
    case PlanetPreset::earth_like:
        return "earth_like";
    case PlanetPreset::aqua_planet:
        return "aqua_planet";
    case PlanetPreset::dead_rock:
        return "dead_rock";
    }
    return "unknown";
}

void validate_geology_parameters(const GeologyParameters& p) {
    require(p.plate_count >= 2U && p.plate_count <= 40U, "plate_count must be in [2, 40]");
    require(non_negative(p.plate_seed_min_spacing_factor) && p.plate_seed_min_spacing_factor < 1.0,
            "plate seed spacing factor must be in [0, 1)");
    require(positive(p.plate_growth_factor_min) &&
                p.plate_growth_factor_max >= p.plate_growth_factor_min &&
                std::isfinite(p.plate_growth_factor_max),
            "plate growth factors must be positive and ordered");
    require(non_negative(p.plate_growth_noise_amplitude) && p.plate_growth_noise_amplitude < 1.0,
            "plate growth noise amplitude must be in [0, 1) so edge costs stay positive");
    require_fbm(p.plate_growth_noise, "plate growth noise");
    require(positive(p.plate_speed_min_m_s) && p.plate_speed_max_m_s >= p.plate_speed_min_m_s &&
                std::isfinite(p.plate_speed_max_m_s),
            "plate speeds must be positive and ordered");
    require(p.continental_area_fraction >= 0.0 && p.continental_area_fraction <= 1.0,
            "continental area fraction must be in [0, 1]");
    require_fbm(p.continental_noise, "continental noise");
    require(non_negative(p.continental_plate_bias_amplitude), "continental bias amplitude");
    require(positive(p.oceanic_age_cap_s), "oceanic age cap");
    require(positive(p.continental_age_min_s) && p.continental_age_max_s >= p.continental_age_min_s &&
                std::isfinite(p.continental_age_max_s),
            "continental ages must be positive and ordered");
    require(non_negative(p.ridge_depth_m) && non_negative(p.subsidence_m_per_sqrt_myr) &&
                positive(p.age_depth_transition_s),
            "age-depth relation");
    const double transition_myr = p.age_depth_transition_s / seconds_per_million_years;
    require(p.abyssal_depth_limit_m >
                p.ridge_depth_m + p.subsidence_m_per_sqrt_myr * std::sqrt(transition_myr),
            "abyssal depth limit must exceed the depth at the transition age");
    require(std::isfinite(p.continental_base_elevation_m) &&
                std::isfinite(p.shelf_edge_elevation_m),
            "continental elevations");
    for (const double width :
         {p.passive_margin_width_m, p.continental_slope_width_m, p.collision_mountain_half_width_m,
          p.trench_half_width_m, p.cordillera_half_width_m, p.island_arc_half_width_m,
          p.rift_half_width_m, p.transform_fault_half_width_m}) {
        require(positive(width), "structure widths must be positive");
    }
    for (const double amplitude :
         {p.collision_mountain_height_m, p.trench_depth_m, p.cordillera_height_m,
          p.island_arc_height_m, p.rift_depth_m, p.transform_fault_amplitude_m,
          p.continental_roughness_amplitude_m, p.oceanic_roughness_amplitude_m,
          p.continental_subcell_roughness_amplitude_m, p.oceanic_subcell_roughness_amplitude_m}) {
        require(non_negative(amplitude), "structure amplitudes must be non-negative");
    }
    require_fbm(p.transform_fault_noise, "transform fault noise");
    require(positive(p.boundary_speed_scale_max), "boundary speed scale maximum");
    require_fbm(p.roughness_noise, "roughness noise");
    require(non_negative(p.erosion_diffusivity_m2_s) && non_negative(p.erosion_step_s),
            "erosion diffusivity and step");
    require_fbm(p.subcell_roughness_noise, "sub-cell roughness noise");
    require(p.target_land_fraction >= 0.0 && p.target_land_fraction <= 1.0,
            "target land fraction must be in [0, 1]");
}

}  // namespace planetsim
