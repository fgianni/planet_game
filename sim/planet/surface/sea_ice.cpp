#include "sim/planet/surface/sea_ice.hpp"

#include "sim/planet/surface/cover_fractions.hpp"

#include "sim/planet/surface/cryosphere_constants.hpp"
#include "sim/planet/atmosphere/saturation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace planetsim {
namespace {

// Open water, with a constant sink (the latent heat of ice melting away in
// this step) and the freeze at T_f.
OceanTileResult open_water(const OceanTileSystem& tile, double source_W_m2,
                           double exchange_W_m2_K, double sink_W_m2) {
    ColumnSystem system = tile.open;
    system.a += exchange_W_m2_K;
    system.b += source_W_m2;
    double surface_K = solve_column_surface(system, sink_W_m2);
    double released_W_m2 = 0.0;   // latent heat released by freezing
    bool held = false;
    if (surface_K < seawater_freezing_point_K) {
        surface_K = seawater_freezing_point_K;
        released_W_m2 = -system.surplus_W_m2(surface_K) + sink_W_m2;
        held = true;
    }
    OceanTileResult result;
    result.column = complete_column_step(tile.column, system, tile.before, surface_K,
                                         sink_W_m2 - released_W_m2,
                                         source_W_m2 - exchange_W_m2_K * surface_K);
    result.column.surface_slope_K_m2_W = held ? 0.0 : system.slope_K_m2_W(surface_K);
    result.radiating_K = surface_K;
    result.emitted_slope = 4.0 * system.radiative * surface_K * surface_K * surface_K *
                           result.column.surface_slope_K_m2_W;
    result.melted_kg_m2 = sink_W_m2 * tile.dt_s / latent_heat_of_fusion_J_kg;
    result.frozen_kg_m2 = released_W_m2 * tile.dt_s / latent_heat_of_fusion_J_kg;
    result.ice_kg_m2 = tile.ice_kg_m2 - result.melted_kg_m2 + result.frozen_kg_m2;
    if (sink_W_m2 > 0.0) {
        result.ice_kg_m2 = result.frozen_kg_m2;   // all the old ice has melted
    }
    result.latent_J_m2 = latent_heat_of_fusion_J_kg * (result.melted_kg_m2 - result.frozen_kg_m2);
    result.albedo = tile.column.albedo;
    result.evaporation_kg_m2 = system.evaporation_kg_m2_s(surface_K).water * tile.dt_s;
    return result;
}

// The ice surface for a trial thickness: its temperature, whether it is held
// at T_m, and the heat the ice loses upward, F_top = conduction − top melt.
struct IceSurface {
    ColumnSystem system;
    double surface_K = 0.0;
    double top_melt_W_m2 = 0.0;
    double conduction_W_m2 = 0.0;
    bool held = false;

    [[nodiscard]] double top_loss_W_m2() const noexcept {
        return conduction_W_m2 - top_melt_W_m2;
    }
};

[[nodiscard]] IceSurface ice_surface(const OceanTileSystem& tile, double thickness_m,
                                     double source_W_m2, double exchange_W_m2_K) {
    IceSurface ice;
    const double conductance = sea_ice_conductivity_W_m_K / thickness_m;
    ice.system.radiative = tile.open.radiative;
    ice.system.a = exchange_W_m2_K + conductance;
    ice.system.b = tile.floe_absorbed_W_m2 + source_W_m2 + conductance * seawater_freezing_point_K;
    if (tile.evaporation.transfer_kg_m2_s > 0.0) {
        ice.system.ice_transfer = tile.evaporation.transfer_kg_m2_s;
        ice.system.air_humidity = tile.evaporation.air_humidity;
        ice.system.pressure_Pa = tile.evaporation.pressure_Pa;
        ice.system.vapour_kg_m2 = tile.evaporation.vapour_kg_m2;
        ice.system.step_s = tile.dt_s;
    }
    ice.surface_K = solve_column_surface(ice.system);
    if (ice.surface_K > melting_point_K) {
        ice.top_melt_W_m2 = ice.system.surplus_W_m2(melting_point_K);
        ice.surface_K = melting_point_K;
        ice.held = true;
    }
    ice.conduction_W_m2 = conductance * (seawater_freezing_point_K - ice.surface_K);
    return ice;
}

}  // namespace

double sea_ice_covered_albedo(double ocean_albedo, double ice_kg_m2) noexcept {
    return ocean_albedo +
           (sea_ice_albedo - ocean_albedo) * sea_ice_cover_fraction(ice_kg_m2);
}

OceanTileSystem prepare_ocean_tile(const ColumnProperties& ocean, ColumnState state,
                                   double ice_kg_m2, double insolation_W_m2,
                                   double grey_emissivity, double dt_s,
                                   const EvaporationForcing& evaporation) {
    if (!std::isfinite(ice_kg_m2) || ice_kg_m2 < 0.0) {
        throw std::invalid_argument("sea ice must be finite and non-negative");
    }
    OceanTileSystem tile;
    tile.column = ocean;
    // Without ice the tile is exactly the ADR-0007 column (ADR-0008 V6).
    if (ice_kg_m2 > 0.0) {
        tile.column.albedo = sea_ice_covered_albedo(ocean.albedo, ice_kg_m2);
    }
    tile.open = column_system(tile.column, state, insolation_W_m2, grey_emissivity, dt_s);
    if (evaporation.transfer_kg_m2_s > 0.0) {
        tile.open.water_transfer = evaporation.transfer_kg_m2_s;
        tile.open.air_humidity = evaporation.air_humidity;
        tile.open.pressure_Pa = evaporation.pressure_Pa;
        tile.open.vapour_kg_m2 = evaporation.vapour_kg_m2;
        tile.open.step_s = dt_s;
    }
    tile.evaporation = evaporation;
    tile.before = state;
    tile.ice_kg_m2 = ice_kg_m2;
    tile.dt_s = dt_s;
    // ADR-0008 §10: the cover that also sets the albedo; floes and leads
    // absorb with their own albedos, which average to the tile's.
    tile.cover = sea_ice_cover_fraction(ice_kg_m2);
    tile.floe_absorbed_W_m2 = (1.0 - sea_ice_albedo) * insolation_W_m2;
    tile.lead_absorbed_W_m2 = (1.0 - ocean.albedo) * insolation_W_m2;
    return tile;
}

OceanTileResult solve_ocean_tile(const OceanTileSystem& tile, double source_W_m2,
                                 double exchange_W_m2_K) {
    if (!(tile.ice_kg_m2 > 0.0)) {
        return open_water(tile, source_W_m2, exchange_W_m2_K, 0.0);
    }

    // Under ice: the mixed layer at T_f, the deep layer exchanging with it.
    const double freezing_K = seawater_freezing_point_K;
    const double deep_K = tile.open.lower_K(tile.before.lower_K, freezing_K);
    const double surface_rate = tile.column.surface_heat_capacity_J_m2_K / tile.dt_s;
    const double ocean_flux_W_m2 = tile.open.exchange * (deep_K - freezing_K) +
                                   surface_rate * (tile.before.surface_K - freezing_K);
    const double latent_rate = latent_heat_of_fusion_J_kg / tile.dt_s;   // per kg/m²
    const double cover = tile.cover;
    const double floe_m = sea_ice_albedo_ramp_m;
    const double radiative = tile.open.radiative;
    const double freezing_emission_W_m2 = radiative * std::pow(freezing_K, 4);
    // Net heat into the leads at T_f, per unit lead area, after their
    // evaporation (ADR-0021 §4.3).
    const double lead_evaporation_kg_m2_s = tile.open.evaporation_kg_m2_s(freezing_K).water;
    const double lead_latent_W_m2 = latent_heat_vaporisation_J_kg * lead_evaporation_kg_m2_s;
    const double lead_W_m2 = tile.lead_absorbed_W_m2 + source_W_m2 -
                             exchange_W_m2_K * freezing_K - freezing_emission_W_m2 -
                             lead_latent_W_m2;

    // R(m') = L (m' − m)/Δt − c₀ F_top(h_f') + (1 − c₀) Q_L + F_o with
    // h_f' = max(h_r, m'/ρ): increasing in m' (ADR-0008 §10).
    const auto thickness_of = [&](double mass) {
        return std::max(floe_m, mass / sea_ice_density_kg_m3);
    };
    const auto residual = [&](double mass, IceSurface& ice) {
        ice = ice_surface(tile, thickness_of(mass), source_W_m2, exchange_W_m2_K);
        return latent_rate * (mass - tile.ice_kg_m2) - cover * ice.top_loss_W_m2() +
               (1.0 - cover) * lead_W_m2 + ocean_flux_W_m2;
    };

    IceSurface ice;
    double value = residual(0.0, ice);
    double root = 0.0;
    const bool melts_away = value >= 0.0;
    if (!melts_away) {
        // Safeguarded Newton on the mass. Below ρ h_r the floe thickness is
        // fixed and R is linear (one step); above it dF_top/dh' =
        // −(4 r T³ + γ) B / A with A = 4 r T³ + γ + k/h', B = k (T_f − T)/h'²
        // while the surface is free, and 0 while it is held at T_m. A step
        // that leaves the bracket bisects instead; a step below 1e-13 of the
        // mass ends the solve, keeping the last evaluated mass.
        double low = 0.0;                                          // R(low) < 0
        double high = std::numeric_limits<double>::infinity();     // R(high) > 0
        root = std::max(tile.ice_kg_m2, tile.thickness_guess_m * sea_ice_density_kg_m3);
        for (int iteration = 0; iteration < 100; ++iteration) {
            value = residual(root, ice);
            (value < 0.0 ? low : high) = root;
            double derivative = latent_rate;
            const double thickness = thickness_of(root);
            if (!ice.held && root > sea_ice_density_kg_m3 * floe_m) {
                const double conductance = sea_ice_conductivity_W_m_K / thickness;
                const double radiative_slope =
                    4.0 * radiative * std::pow(ice.surface_K, 3) + exchange_W_m2_K;
                const double b = conductance * (freezing_K - ice.surface_K) / thickness;
                derivative += cover * radiative_slope * b / (radiative_slope + conductance) /
                              sea_ice_density_kg_m3;
            }
            double next = root - value / derivative;
            if (!(next > low && next < high)) {
                next = std::isfinite(high) ? 0.5 * (low + high) : 2.0 * root + 1.0;
            }
            if (value == 0.0 || std::abs(next - root) <= 1e-13 * root) {
                break;
            }
            root = next;
        }
        tile.thickness_guess_m = root / sea_ice_density_kg_m3;
    }

    OceanTileResult result;
    const double surface_K = cover * ice.surface_K + (1.0 - cover) * freezing_K;
    result.radiating_K = surface_K;
    result.ocean_heat_flux_W_m2 = ocean_flux_W_m2;
    result.ice_kg_m2 = root;
    // Heat left over once all the ice has melted warms the mixed layer.
    const double leftover_J_m2 = melts_away ? value * tile.dt_s : 0.0;
    if (melts_away) {
        result.melted_kg_m2 = tile.ice_kg_m2;
    } else {
        // Gross terms: the floes' top melts, their base grows or melts, the
        // leads freeze or melt; their sum is exactly the change of mass.
        const double top_melt_kg_m2 = cover * ice.top_melt_W_m2 / latent_rate;
        const double base_kg_m2 = (cover * ice.conduction_W_m2 - ocean_flux_W_m2) / latent_rate;
        const double lead_kg_m2 = -(1.0 - cover) * lead_W_m2 / latent_rate;
        result.frozen_kg_m2 = std::max(0.0, base_kg_m2) + std::max(0.0, lead_kg_m2);
        result.melted_kg_m2 =
            top_melt_kg_m2 + std::max(0.0, -base_kg_m2) + std::max(0.0, -lead_kg_m2);
        // The Newton root carries the residual's rounding; the gross terms
        // are adjusted to the stored change.
        const double change_kg_m2 = result.ice_kg_m2 - tile.ice_kg_m2;
        const double gross_change = result.frozen_kg_m2 - result.melted_kg_m2;
        if (change_kg_m2 >= gross_change) {
            result.frozen_kg_m2 += change_kg_m2 - gross_change;
        } else {
            result.melted_kg_m2 += gross_change - change_kg_m2;
        }
    }
    result.latent_J_m2 = latent_heat_of_fusion_J_kg * (result.melted_kg_m2 - result.frozen_kg_m2);
    result.albedo = tile.column.albedo;
    // Evaporation: the leads' from the water, the floes' sublimated from the
    // ice (ADR-0021 §4.3).
    result.evaporation_kg_m2 = (1.0 - cover) * lead_evaporation_kg_m2_s * tile.dt_s;
    const double floe_kg_m2 = cover * ice.system.evaporation_kg_m2_s(ice.surface_K).ice * tile.dt_s;
    if (!melts_away) {
        result.sublimation_kg_m2 = std::min(result.ice_kg_m2, floe_kg_m2);
        result.ice_kg_m2 -= result.sublimation_kg_m2;
    } else {
        // The floes' latent heat is in the balance that melted them; with no
        // ice left, their vapour comes from the water.
        result.evaporation_kg_m2 += floe_kg_m2;
    }

    ColumnStepResult& column = result.column;
    const double mixed_K = freezing_K + leftover_J_m2 / tile.column.surface_heat_capacity_J_m2_K;
    column.state = {mixed_K, deep_K};
    column.absorbed_W_m2 = tile.open.absorbed_W_m2;
    column.emitted_W_m2 = cover * radiative * std::pow(ice.surface_K, 4) +
                          (1.0 - cover) * freezing_emission_W_m2;
    column.storage_change_J_m2 =
        tile.column.surface_heat_capacity_J_m2_K * (mixed_K - tile.before.surface_K) +
        tile.column.lower_heat_capacity_J_m2_K * (deep_K - tile.before.lower_K);
    column.source_W_m2 = source_W_m2 - exchange_W_m2_K * surface_K;
    column.newton_residual_W_m2 = melts_away ? 0.0 : value;
    column.evaporation_W_m2 =
        (1.0 - cover) * lead_latent_W_m2 +
        latent_heat_sublimation_J_kg *
            (melts_away ? floe_kg_m2 : result.sublimation_kg_m2) / tile.dt_s;
    // dT_i/ds of the floes, with the thickness responding where it is free
    // to (implicit function theorem on the surface balance and the growth
    // equation): A dT + B dh' = ds, (ρL/Δt + B) dh' + (k/h') dT = 0 gives
    // dT/ds = 1 / (A − (k/h') B / (ρL/Δt + B)), A = 4 r T³ + γ + k/h',
    // B = k (T_f − T)/h'². At the fixed floe thickness, or after complete
    // melt, dT/ds = 1 / A. The leads stay at T_f.
    double floe_slope = 0.0;
    if (!ice.held) {
        const double thickness = thickness_of(root);
        const double conductance = sea_ice_conductivity_W_m_K / thickness;
        const double a = ice.system.a + 4.0 * radiative * std::pow(ice.surface_K, 3) +
                         ice.system.latent_slope_W_m2_K(ice.surface_K);
        floe_slope = 1.0 / a;
        if (!melts_away && root > sea_ice_density_kg_m3 * floe_m) {
            const double b = conductance * (freezing_K - ice.surface_K) / thickness;
            const double rho_latent = sea_ice_density_kg_m3 * latent_rate / cover;
            floe_slope = 1.0 / (a - conductance * b / (rho_latent + b));
        }
    }
    column.surface_slope_K_m2_W = cover * floe_slope;
    result.emitted_slope =
        cover * 4.0 * radiative * std::pow(ice.surface_K, 3) * floe_slope;
    return result;
}

double ocean_tile_source_floor_W_m2(const OceanTileSystem& tile, double lowest_K) {
    if (!(tile.ice_kg_m2 > 0.0)) {
        return -tile.open.surplus_W_m2(lowest_K);
    }
    // Over ice, the floes' surface at the start-of-step thickness (the
    // leads stay at T_f).
    const double conductance =
        sea_ice_conductivity_W_m_K /
        std::max(sea_ice_albedo_ramp_m, tile.ice_kg_m2 / sea_ice_density_kg_m3);
    ColumnSystem system;
    system.radiative = tile.open.radiative;
    system.a = conductance;
    system.b = tile.floe_absorbed_W_m2 + conductance * seawater_freezing_point_K;
    return -system.surplus_W_m2(lowest_K);
}

}  // namespace planetsim
