#include "sim/planet/surface/surface_materials.hpp"

#include "sim/planet/orbit/substep_forcing.hpp"
#include "sim/planet/planet_parameters.hpp"

#include <cmath>
#include <numbers>

namespace planetsim {

MaterialProperties material_properties(SurfaceMaterial material) noexcept {
    switch (material) {
    case SurfaceMaterial::rock:
        return {2.5, 2.2e6, 0.25, 0.95};
    case SurfaceMaterial::dry_soil:
        return {0.3, 1.3e6, 0.30, 0.95};
    case SurfaceMaterial::wet_soil:
        return {1.5, 2.9e6, 0.15, 0.97};
    case SurfaceMaterial::ocean:
        return {0.0, 0.0, 0.06, 0.97};
    }
    return {};
}

ColumnProperties column_properties(SurfaceMaterial material,
                                   const PlanetParameters& parameters) {
    const MaterialProperties properties = material_properties(material);
    if (material == SurfaceMaterial::ocean) {
        return {ocean_mixed_layer_heat_capacity_J_m2_K, ocean_deep_heat_capacity_J_m2_K,
                ocean_deep_exchange_W_m2_K, properties.albedo, properties.emissivity};
    }
    const double diffusivity_m2_s =
        properties.conductivity_W_m_K / properties.volumetric_heat_capacity_J_m3_K;
    const double two_pi = 2.0 * std::numbers::pi_v<double>;
    const double day_depth_m =
        std::sqrt(2.0 * diffusivity_m2_s / (two_pi / synodic_day_s(parameters)));
    const double year_depth_m =
        std::sqrt(2.0 * diffusivity_m2_s / (two_pi / parameters.orbital_period_s));
    return {properties.volumetric_heat_capacity_J_m3_K * day_depth_m,
            properties.volumetric_heat_capacity_J_m3_K * year_depth_m,
            properties.conductivity_W_m_K / (0.5 * (day_depth_m + year_depth_m)),
            properties.albedo, properties.emissivity};
}

}  // namespace planetsim
