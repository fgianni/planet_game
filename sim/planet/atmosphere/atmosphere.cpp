#include "sim/planet/atmosphere/atmosphere.hpp"

#include "sim/planet/atmosphere/water.hpp"

#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/mesh/planet_mesh.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/terrain/hypsometry.hpp"
#include "sim/planet/terrain/surface_fractions.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace planetsim {

AtmosphereParameters atmosphere_parameters_for(PlanetPreset preset) noexcept {
    AtmosphereParameters parameters;
    switch (preset) {
    case PlanetPreset::dead_rock:
    case PlanetPreset::aqua_planet:
        parameters.layer_count = 0U;
        break;
    case PlanetPreset::earth_like:
        parameters.layer_count = 3U;
        parameters.longwave_optical_depth = earth_like_longwave_optical_depth;
        parameters.dry_optical_depth = earth_like_dry_optical_depth;
        parameters.vapour_absorption_m2_kg = earth_like_vapour_absorption_m2_kg;
        break;
    }
    return parameters;
}

void validate_atmosphere_parameters(const AtmosphereParameters& parameters) {
    if (parameters.layer_count > max_atmosphere_layer_count) {
        throw std::invalid_argument("atmosphere layer count " +
                                    std::to_string(parameters.layer_count) + " exceeds " +
                                    std::to_string(max_atmosphere_layer_count));
    }
    if (!std::isfinite(parameters.reference_pressure_Pa) ||
        parameters.reference_pressure_Pa <= 0.0) {
        throw std::invalid_argument("atmosphere reference pressure must be positive");
    }
    if (!std::isfinite(parameters.critical_lapse_rate_K_m) ||
        parameters.critical_lapse_rate_K_m <= 0.0) {
        throw std::invalid_argument("atmosphere critical lapse rate must be positive");
    }
    if (!std::isfinite(parameters.longwave_optical_depth) ||
        parameters.longwave_optical_depth < 0.0) {
        throw std::invalid_argument("longwave optical depth must be finite and non-negative");
    }
    if (!(parameters.linear_optical_depth_fraction >= 0.0 &&
          parameters.linear_optical_depth_fraction <= 1.0)) {
        throw std::invalid_argument("linear optical depth fraction must lie in [0, 1]");
    }
    if (!(parameters.dry_optical_depth >= 0.0) || !std::isfinite(parameters.dry_optical_depth) ||
        !(parameters.vapour_absorption_m2_kg >= 0.0) ||
        !std::isfinite(parameters.vapour_absorption_m2_kg)) {
        throw std::invalid_argument("dry optical depth and vapour absorption must be finite and "
                                    "non-negative");
    }
}

double surface_gravity_m_s2(const PlanetParameters& parameters) {
    if (!(parameters.mass_kg > 0.0) || !(parameters.radius_m > 0.0)) {
        throw std::invalid_argument("surface gravity needs a positive mass and radius");
    }
    return gravitational_constant_m3_kg_s2 * parameters.mass_kg /
           (parameters.radius_m * parameters.radius_m);
}

double critical_lapse_exponent(const AtmosphereParameters& parameters,
                               double gravity_m_s2) noexcept {
    return dry_air_gas_constant_J_kg_K * parameters.critical_lapse_rate_K_m / gravity_m_s2;
}

double layer_sigma(std::size_t layer, std::size_t layer_count) noexcept {
    return 1.0 - (static_cast<double>(layer) + 0.5) / static_cast<double>(layer_count);
}

void compute_surface_height(const PlanetMesh& mesh, const SlowState& slow,
                            const SurfaceFractions& fractions, Field2D<double>& height_m) {
    const std::size_t cells = mesh.cell_count();
    height_m = Field2D<double>(cells, 0.0);
    for (std::size_t index = 0; index < cells; ++index) {
        const CellId cell{static_cast<std::uint32_t>(index)};
        const HypsometryQuantiles quantiles = cell_hypsometry(slow.hypsometry_m, cell);
        const bool coastal = fractions.ocean_fraction[index] > 0.0F;
        const double land_m = (coastal ? mean_elevation_above_m(quantiles, slow.sea_level_m)
                                       : mean_elevation_m(quantiles)) -
                              slow.sea_level_m;
        height_m[index] = static_cast<double>(fractions.land_fraction[index]) * land_m;
    }
}

double tile_mean_surface_temperature_K(const SlowState& slow, const SurfaceFractions& fractions,
                                       std::size_t cell) noexcept {
    const double land = fractions.land_fraction[cell];
    const double ocean = fractions.ocean_fraction[cell];
    const double total = land + ocean;
    if (!(total > 0.0)) {
        return slow.ocean_mixed_layer_temperature_K[cell];
    }
    return (land * static_cast<double>(slow.land_surface_temperature_K[cell]) +
            ocean * slow.ocean_mixed_layer_temperature_K[cell]) /
           total;
}

double hydrostatic_surface_pressure_Pa(const AtmosphereParameters& parameters,
                                       double gravity_m_s2, double height_m,
                                       double surface_temperature_K) noexcept {
    const double lapse = parameters.critical_lapse_rate_K_m;
    const double sea_level_K = surface_temperature_K + lapse * height_m;
    const double exponent = gravity_m_s2 / (dry_air_gas_constant_J_kg_K * lapse);
    return parameters.reference_pressure_Pa *
           std::pow(surface_temperature_K / sea_level_K, exponent);
}

void initialise_atmosphere(const PlanetMesh& mesh, SlowState& slow,
                           const PlanetParameters& planet,
                           const AtmosphereParameters& parameters) {
    validate_atmosphere_parameters(parameters);
    const std::size_t cells = mesh.cell_count();
    const std::size_t layers = parameters.layer_count;
    slow.atmosphere_surface_pressure_Pa = Field2D<double>(cells, 0.0);
    slow.atmosphere_temperature_K = Field3D<double>(layers, cells, 0.0);
    if (layers == 0U) {
        initialise_water(mesh, slow);
        return;
    }
    const double gravity = surface_gravity_m_s2(planet);
    const double kappa = critical_lapse_exponent(parameters, gravity);
    const SurfaceFractions fractions =
        compute_surface_fractions(mesh, slow.hypsometry_m, slow.sea_level_m);
    Field2D<double> height_m;
    compute_surface_height(mesh, slow, fractions, height_m);
    const double skin_ratio = std::pow(0.5, 0.25);
    for (std::size_t cell = 0; cell < cells; ++cell) {
        const double surface_K = tile_mean_surface_temperature_K(slow, fractions, cell);
        if (!std::isfinite(surface_K) || !(surface_K > 0.0)) {
            throw std::invalid_argument(
                "initialise_atmosphere needs initialised surface temperatures");
        }
        slow.atmosphere_surface_pressure_Pa[cell] =
            hydrostatic_surface_pressure_Pa(parameters, gravity, height_m[cell], surface_K);
        for (std::size_t layer = 0; layer < layers; ++layer) {
            const double profile_K = surface_K * std::pow(layer_sigma(layer, layers), kappa);
            slow.atmosphere_temperature_K.layer(layer)[cell] =
                std::max(profile_K, surface_K * skin_ratio);
        }
    }
    // Its water at the declared start (ADR-0021 §4.1).
    initialise_water(mesh, slow);
}

double surface_air_temperature_K(double bottom_layer_K, std::size_t layer_count,
                                 double lapse_exponent) noexcept {
    return bottom_layer_K * std::pow(1.0 / layer_sigma(0U, layer_count), lapse_exponent);
}

double sea_level_pressure_Pa(const AtmosphereParameters& parameters, double gravity_m_s2,
                             double surface_pressure_Pa, double height_m,
                             double surface_air_K) noexcept {
    const double lapse = parameters.critical_lapse_rate_K_m;
    const double exponent = gravity_m_s2 / (dry_air_gas_constant_J_kg_K * lapse);
    return surface_pressure_Pa *
           std::pow((surface_air_K + lapse * height_m) / surface_air_K, exponent);
}

void layer_heights_m(std::span<const double> layer_temperature_K, double surface_air_K,
                     double surface_height_m, double gravity_m_s2, std::span<double> height_m) {
    const std::size_t layers = layer_temperature_K.size();
    if (height_m.size() != layers) {
        throw std::invalid_argument("layer_heights_m needs one height per layer");
    }
    const double scale = dry_air_gas_constant_J_kg_K / gravity_m_s2;
    double previous_sigma = 1.0;
    double previous_K = surface_air_K;
    double z = surface_height_m;
    for (std::size_t layer = 0; layer < layers; ++layer) {
        const double sigma = layer_sigma(layer, layers);
        const double mean_K = 0.5 * (previous_K + layer_temperature_K[layer]);
        z += scale * mean_K * std::log(previous_sigma / sigma);
        height_m[layer] = z;
        previous_sigma = sigma;
        previous_K = layer_temperature_K[layer];
    }
}

namespace {

struct AtmospherePartial {
    double area_m2 = 0.0;
    double mass_kg = 0.0;
    double surface_pressure_Pa_m2 = 0.0;
    double sea_level_pressure_Pa_m2 = 0.0;
    double surface_air_K_m2 = 0.0;
    double min_surface_Pa = std::numeric_limits<double>::infinity();
    double max_surface_Pa = -std::numeric_limits<double>::infinity();
    double min_sea_level_Pa = std::numeric_limits<double>::infinity();
    double max_sea_level_Pa = -std::numeric_limits<double>::infinity();
    std::vector<double> layer_K_m2;
    std::vector<double> layer_Pa_m2;
    std::vector<double> layer_height_m3;
};

}  // namespace

AtmosphereDiagnostics diagnose_atmosphere(const PlanetMesh& mesh, const SlowState& slow,
                                          const PlanetParameters& planet,
                                          const AtmosphereParameters& parameters,
                                          std::size_t worker_count) {
    validate_atmosphere_parameters(parameters);
    const std::size_t layers = slow.atmosphere_layer_count();
    if (layers != parameters.layer_count) {
        throw std::invalid_argument("the state's atmosphere has " + std::to_string(layers) +
                                    " layers, the parameters " +
                                    std::to_string(parameters.layer_count));
    }
    AtmosphereDiagnostics diagnostics;
    diagnostics.layer_count = static_cast<std::uint32_t>(layers);
    if (layers == 0U) {
        return diagnostics;
    }
    const double gravity = surface_gravity_m_s2(planet);
    const double kappa = critical_lapse_exponent(parameters, gravity);
    const SurfaceFractions fractions =
        compute_surface_fractions(mesh, slow.hypsometry_m, slow.sea_level_m, worker_count);
    Field2D<double> height_m;
    compute_surface_height(mesh, slow, fractions, height_m);

    AtmospherePartial identity;
    identity.layer_K_m2.assign(layers, 0.0);
    identity.layer_Pa_m2.assign(layers, 0.0);
    identity.layer_height_m3.assign(layers, 0.0);
    const AtmospherePartial total = reduce_deterministic_blocks<AtmospherePartial>(
        mesh.blocks(), worker_count, identity,
        [&](std::size_t, const CellBlock& block) {
            AtmospherePartial partial = identity;
            std::vector<double> column(layers);
            std::vector<double> heights(layers);
            for (std::size_t cell = block.begin; cell < block.end; ++cell) {
                const double area = mesh.cells()[cell].area_m2;
                const double surface_Pa = slow.atmosphere_surface_pressure_Pa[cell];
                for (std::size_t layer = 0; layer < layers; ++layer) {
                    column[layer] = slow.atmosphere_temperature_K.layer(layer)[cell];
                }
                const double air_K = surface_air_temperature_K(column[0], layers, kappa);
                const double sea_level_Pa =
                    sea_level_pressure_Pa(parameters, gravity, surface_Pa, height_m[cell], air_K);
                layer_heights_m(column, air_K, height_m[cell], gravity, heights);
                partial.area_m2 += area;
                partial.mass_kg += area * surface_Pa / gravity;
                partial.surface_pressure_Pa_m2 += area * surface_Pa;
                partial.sea_level_pressure_Pa_m2 += area * sea_level_Pa;
                partial.surface_air_K_m2 += area * air_K;
                partial.min_surface_Pa = std::min(partial.min_surface_Pa, surface_Pa);
                partial.max_surface_Pa = std::max(partial.max_surface_Pa, surface_Pa);
                partial.min_sea_level_Pa = std::min(partial.min_sea_level_Pa, sea_level_Pa);
                partial.max_sea_level_Pa = std::max(partial.max_sea_level_Pa, sea_level_Pa);
                for (std::size_t layer = 0; layer < layers; ++layer) {
                    partial.layer_K_m2[layer] += area * column[layer];
                    partial.layer_Pa_m2[layer] += area * surface_Pa * layer_sigma(layer, layers);
                    partial.layer_height_m3[layer] += area * heights[layer];
                }
            }
            return partial;
        },
        [](AtmospherePartial sum, const AtmospherePartial& part) {
            sum.area_m2 += part.area_m2;
            sum.mass_kg += part.mass_kg;
            sum.surface_pressure_Pa_m2 += part.surface_pressure_Pa_m2;
            sum.sea_level_pressure_Pa_m2 += part.sea_level_pressure_Pa_m2;
            sum.surface_air_K_m2 += part.surface_air_K_m2;
            sum.min_surface_Pa = std::min(sum.min_surface_Pa, part.min_surface_Pa);
            sum.max_surface_Pa = std::max(sum.max_surface_Pa, part.max_surface_Pa);
            sum.min_sea_level_Pa = std::min(sum.min_sea_level_Pa, part.min_sea_level_Pa);
            sum.max_sea_level_Pa = std::max(sum.max_sea_level_Pa, part.max_sea_level_Pa);
            for (std::size_t layer = 0; layer < sum.layer_K_m2.size(); ++layer) {
                sum.layer_K_m2[layer] += part.layer_K_m2[layer];
                sum.layer_Pa_m2[layer] += part.layer_Pa_m2[layer];
                sum.layer_height_m3[layer] += part.layer_height_m3[layer];
            }
            return sum;
        });

    const double area = total.area_m2;
    diagnostics.mass_kg = total.mass_kg;
    diagnostics.mean_surface_pressure_Pa = total.surface_pressure_Pa_m2 / area;
    diagnostics.min_surface_pressure_Pa = total.min_surface_Pa;
    diagnostics.max_surface_pressure_Pa = total.max_surface_Pa;
    diagnostics.mean_sea_level_pressure_Pa = total.sea_level_pressure_Pa_m2 / area;
    diagnostics.min_sea_level_pressure_Pa = total.min_sea_level_Pa;
    diagnostics.max_sea_level_pressure_Pa = total.max_sea_level_Pa;
    diagnostics.mean_surface_air_temperature_K = total.surface_air_K_m2 / area;
    for (std::size_t layer = 0; layer < layers; ++layer) {
        diagnostics.mean_layer_temperature_K.push_back(total.layer_K_m2[layer] / area);
        diagnostics.mean_layer_pressure_Pa.push_back(total.layer_Pa_m2[layer] / area);
        diagnostics.mean_layer_height_m.push_back(total.layer_height_m3[layer] / area);
    }
    return diagnostics;
}

void layer_dry_static_energy(double surface_geopotential_m2_s2,
                             std::span<const double> temperature_K, std::span<double> energy_J_kg,
                             double gas_constant_J_kg_K, double heat_capacity_J_kg_K) {
    const std::size_t n = temperature_K.size();
    if (energy_J_kg.size() != n || n == 0U) {
        throw std::invalid_argument("layer dry static energy: invalid column");
    }
    const double kappa = gas_constant_J_kg_K / heat_capacity_J_kg_K;
    const double cp = heat_capacity_J_kg_K;
    const double n_d = static_cast<double>(n);
    // The Exner function's reference pressure cancels in θ Δπ.
    double phi = surface_geopotential_m2_s2;
    double pi_bottom = 1.0;
    for (std::size_t k = 0; k < n; ++k) {
        const double sigma_bottom = 1.0 - static_cast<double>(k) / n_d;
        const double sigma_top = 1.0 - static_cast<double>(k + 1U) / n_d;
        const double pi_top = k + 1U == n ? 0.0 : std::pow(sigma_top, kappa);
        const double pi_mean = (sigma_bottom * pi_bottom - sigma_top * pi_top) /
                               ((1.0 + kappa) * (sigma_bottom - sigma_top));
        const double theta = temperature_K[k] / pi_mean;
        energy_J_kg[k] = cp * temperature_K[k] + phi + cp * theta * (pi_bottom - pi_mean);
        phi += cp * theta * (pi_bottom - pi_top);
        pi_bottom = pi_top;
    }
}

}  // namespace planetsim
