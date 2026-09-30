#include "sim/planet/surface/sea_ice.hpp"

#include "sim/planet/surface/cryosphere_constants.hpp"

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
    result.melted_kg_m2 = sink_W_m2 * tile.dt_s / latent_heat_of_fusion_J_kg;
    result.frozen_kg_m2 = released_W_m2 * tile.dt_s / latent_heat_of_fusion_J_kg;
    result.ice_kg_m2 = tile.ice_kg_m2 - result.melted_kg_m2 + result.frozen_kg_m2;
    if (sink_W_m2 > 0.0) {
        result.ice_kg_m2 = result.frozen_kg_m2;   // all the old ice has melted
    }
    result.latent_J_m2 = latent_heat_of_fusion_J_kg * (result.melted_kg_m2 - result.frozen_kg_m2);
    result.albedo = tile.column.albedo;
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
    ice.system.b = tile.open.absorbed_W_m2 + source_W_m2 + conductance * seawater_freezing_point_K;
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
    const double thickness_m = ice_kg_m2 / sea_ice_density_kg_m3;
    return ocean_albedo +
           (sea_ice_albedo - ocean_albedo) * std::min(1.0, thickness_m / sea_ice_albedo_ramp_m);
}

OceanTileSystem prepare_ocean_tile(const ColumnProperties& ocean, ColumnState state,
                                   double ice_kg_m2, double insolation_W_m2,
                                   double grey_emissivity, double dt_s) {
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
    tile.before = state;
    tile.ice_kg_m2 = ice_kg_m2;
    tile.dt_s = dt_s;
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
    const double thickness_m = tile.ice_kg_m2 / sea_ice_density_kg_m3;
    const double latent_rate = sea_ice_density_kg_m3 * latent_heat_of_fusion_J_kg / tile.dt_s;

    // R(h') = ρ L (h' − h) / Δt − F_top(h') + F_o, strictly increasing. As
    // h' → 0 the surface tends to T_f and F_top to its balance there.
    const double balance_at_freezing_W_m2 =
        tile.open.radiative * std::pow(freezing_K, 4) + exchange_W_m2_K * freezing_K -
        tile.open.absorbed_W_m2 - source_W_m2;
    if (-latent_rate * thickness_m - balance_at_freezing_W_m2 + ocean_flux_W_m2 >= 0.0) {
        // No positive thickness balances the step: all the ice melts, and
        // what is left warms (or refreezes) the open water.
        return open_water(tile, source_W_m2, exchange_W_m2_K,
                          latent_heat_of_fusion_J_kg * tile.ice_kg_m2 / tile.dt_s);
    }

    // Start from the previous solve of this tile, or else from the discrete
    // Stefan estimate with the surface temperature at the current thickness
    // (thin ice on a monthly step grows far from it).
    double root = std::max(thickness_m, 1e-3);
    if (tile.thickness_guess_m > 0.0) {
        root = tile.thickness_guess_m;
    } else {
        const double deficit_K = freezing_K - ice_surface(tile, root, source_W_m2,
                                                          exchange_W_m2_K).surface_K;
        if (deficit_K > 0.0) {
            const double growth = sea_ice_conductivity_W_m_K * deficit_K / latent_rate;
            root = 0.5 * (root + std::sqrt(root * root + 4.0 * growth));
        }
    }

    // Safeguarded Newton. With the surface free, A dT + B dh' = 0 at fixed
    // source gives dF_top/dh' = −(4 r T³ + γ) B / A (A = 4 r T³ + γ + k/h',
    // B = k (T_f − T) / h'²); held at T_m, F_top does not depend on h'. A step
    // that leaves the bracket bisects instead. The residual is known to about
    // 1e-10 of its terms, so a step below 1e-13 of the thickness ends the
    // solve, keeping the last evaluated thickness (energy error < 1e-4 J/m²).
    double low = 0.0;          // R(low) < 0 (the h' → 0 limit)
    double high = std::numeric_limits<double>::infinity();   // R(high) > 0
    IceSurface ice;
    double value = 0.0;
    for (int iteration = 0; iteration < 100; ++iteration) {
        ice = ice_surface(tile, root, source_W_m2, exchange_W_m2_K);
        value = latent_rate * (root - thickness_m) - ice.top_loss_W_m2() + ocean_flux_W_m2;
        (value < 0.0 ? low : high) = root;
        double derivative = latent_rate;
        if (!ice.held) {
            const double conductance = sea_ice_conductivity_W_m_K / root;
            const double radiative_slope =
                4.0 * ice.system.radiative * std::pow(ice.surface_K, 3) + exchange_W_m2_K;
            const double b = conductance * (freezing_K - ice.surface_K) / root;
            derivative += radiative_slope * b / (radiative_slope + conductance);
        }
        double next = root - value / derivative;
        if (!(next > low && next < high)) {
            next = std::isfinite(high) ? 0.5 * (low + high) : 2.0 * root;
        }
        if (value == 0.0 || std::abs(next - root) <= 1e-13 * root) {
            break;
        }
        root = next;
    }
    tile.thickness_guess_m = root;

    OceanTileResult result;
    result.radiating_K = ice.surface_K;
    result.ocean_heat_flux_W_m2 = ocean_flux_W_m2;
    result.ice_kg_m2 = sea_ice_density_kg_m3 * root;
    // Gross terms: the top melts, the base grows or melts; their difference
    // is exactly the change of mass.
    const double top_melt_kg_m2 = ice.top_melt_W_m2 * tile.dt_s / latent_heat_of_fusion_J_kg;
    const double base_kg_m2 = (ice.conduction_W_m2 - ocean_flux_W_m2) * tile.dt_s /
                              latent_heat_of_fusion_J_kg;
    const double change_kg_m2 = result.ice_kg_m2 - tile.ice_kg_m2;
    result.frozen_kg_m2 =
        std::max(0.0, change_kg_m2 + top_melt_kg_m2 + std::max(0.0, -base_kg_m2));
    result.melted_kg_m2 = result.frozen_kg_m2 - change_kg_m2;
    result.latent_J_m2 = latent_heat_of_fusion_J_kg * (result.melted_kg_m2 - result.frozen_kg_m2);
    result.albedo = tile.column.albedo;

    ColumnStepResult& column = result.column;
    column.state = {freezing_K, deep_K};
    column.absorbed_W_m2 = tile.open.absorbed_W_m2;
    column.emitted_W_m2 = tile.open.radiative * std::pow(ice.surface_K, 4);
    column.storage_change_J_m2 =
        tile.column.surface_heat_capacity_J_m2_K * (freezing_K - tile.before.surface_K) +
        tile.column.lower_heat_capacity_J_m2_K * (deep_K - tile.before.lower_K);
    column.source_W_m2 = source_W_m2 - exchange_W_m2_K * ice.surface_K;
    column.newton_residual_W_m2 = value;
    // dT_i/ds with the thickness responding too (implicit function theorem on
    // the surface balance and the growth equation):
    //   A dT + B dh' = ds,   (ρL/Δt + B) dh' + (k/h') dT = 0,
    //   A = 4 r T³ + γ + k/h',  B = k (T_f − T) / h'²,
    // so dT/ds = 1 / (A − (k/h') B / (ρL/Δt + B)) > 1 / (4 r T³ + γ) > 0.
    // Holding h' fixed underestimates it by up to a factor of a few for thin
    // ice on a monthly step, which slowed the transport Newton to linear.
    if (ice.held) {
        column.surface_slope_K_m2_W = 0.0;
    } else {
        const double conductance = sea_ice_conductivity_W_m_K / root;
        const double a = ice.system.a + 4.0 * ice.system.radiative * std::pow(ice.surface_K, 3);
        const double b = conductance * (freezing_K - ice.surface_K) / root;
        column.surface_slope_K_m2_W = 1.0 / (a - conductance * b / (latent_rate + b));
    }
    return result;
}

double ocean_tile_source_floor_W_m2(const OceanTileSystem& tile, double lowest_K) {
    if (!(tile.ice_kg_m2 > 0.0)) {
        return -tile.open.surplus_W_m2(lowest_K);
    }
    // Over ice, the surface at the start-of-step thickness.
    const double conductance =
        sea_ice_conductivity_W_m_K / (tile.ice_kg_m2 / sea_ice_density_kg_m3);
    ColumnSystem system;
    system.radiative = tile.open.radiative;
    system.a = conductance;
    system.b = tile.open.absorbed_W_m2 + conductance * seawater_freezing_point_K;
    return -system.surplus_W_m2(lowest_K);
}

}  // namespace planetsim
