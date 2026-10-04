#include "sim/planet/dynamics/zonal_coupling.hpp"

#include "sim/planet/coordinates/local_tangent_basis.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace planetsim {

ZonalCirculationParameters zonal_circulation_parameters(const PlanetParameters& planet,
                                                        const AtmosphereParameters& atmosphere,
                                                        std::size_t bands) {
    if (atmosphere.layer_count == 0U) {
        throw std::invalid_argument("the zonal circulation needs an atmosphere");
    }
    ZonalCirculationParameters parameters;
    parameters.bands = bands;
    parameters.layer_count = atmosphere.layer_count;
    parameters.radius_m = planet.radius_m;
    parameters.gravity_m_s2 = surface_gravity_m_s2(planet);
    parameters.rotation_rate_rad_s =
        2.0 * std::numbers::pi_v<double> / planet.sidereal_rotation_period_s;
    parameters.gas_constant_J_kg_K = dry_air_gas_constant_J_kg_K;
    parameters.heat_capacity_J_kg_K = dry_air_heat_capacity_J_kg_K;
    parameters.critical_lapse_rate_K_m =
        atmosphere.convection ? atmosphere.critical_lapse_rate_K_m : 0.0;
    parameters.eddy_generation_m4_s2_K2 = earth_like_eddy_generation_m4_s2_K2;
    return parameters;
}

std::vector<std::size_t> zonal_band_of_cells(const PlanetMesh& mesh, std::size_t bands) {
    std::vector<std::size_t> band(mesh.cell_count());
    const double width = 180.0 / static_cast<double>(bands);
    for (const auto& cell : mesh.cells()) {
        const double latitude = latitude_rad(cell.center_unit) * 180.0 / std::numbers::pi_v<double>;
        const double index = std::floor((latitude + 90.0) / width);
        band[cell.id.to_index()] =
            static_cast<std::size_t>(std::clamp(index, 0.0, static_cast<double>(bands - 1U)));
    }
    return band;
}

ZonalForcing zonal_forcing_from_state(const PlanetMesh& mesh, const SlowState& slow,
                                      const AtmosphereHeating& heating,
                                      const Field2D<double>& dynamics_height_m,
                                      const SurfaceFractions& fractions, std::size_t bands,
                                      const ZonalDragCoefficients& drag) {
    const std::size_t cells = mesh.cell_count();
    const std::size_t n = slow.atmosphere_layer_count();
    if (n == 0U || heating.rate_K_s.layer_count() != n || heating.rate_K_s.cell_count() != cells ||
        heating.derivative_s.cell_count() != cells || dynamics_height_m.size() != cells ||
        fractions.land_fraction.size() != cells) {
        throw std::invalid_argument("zonal forcing inputs do not match the mesh and atmosphere");
    }
    const auto band_of = zonal_band_of_cells(mesh, bands);
    std::vector<double> area(bands, 0.0);
    std::vector<double> mass(bands, 0.0);
    ZonalForcing forcing;
    forcing.surface_pressure_Pa.assign(bands, 0.0);
    forcing.surface_height_m.assign(bands, 0.0);
    forcing.drag_coefficient.assign(bands, 0.0);
    forcing.temperature_K.assign(n * bands, 0.0);
    forcing.heating_K_s.assign(n * bands, 0.0);
    forcing.heating_derivative_s.assign(n * bands, 0.0);
    forcing.rayleigh_friction_s.assign(n * bands, 0.0);
    for (std::size_t i = 0; i < cells; ++i) {
        const std::size_t j = band_of[i];
        const double a = mesh.cells()[i].area_m2;
        const double ps = slow.atmosphere_surface_pressure_Pa[i];
        const double land = std::clamp(static_cast<double>(fractions.land_fraction[i]), 0.0, 1.0);
        area[j] += a;
        mass[j] += a * ps;
        forcing.surface_pressure_Pa[j] += a * ps;
        forcing.surface_height_m[j] += a * dynamics_height_m[i];
        forcing.drag_coefficient[j] += a * (land * drag.land + (1.0 - land) * drag.ocean);
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t c = k * bands + j;
            forcing.temperature_K[c] += a * ps * slow.atmosphere_temperature_K.layer(k)[i];
            forcing.heating_K_s[c] += a * ps * heating.rate_K_s.layer(k)[i];
            forcing.heating_derivative_s[c] += a * ps * heating.derivative_s.layer(k)[i];
        }
    }
    for (std::size_t j = 0; j < bands; ++j) {
        if (!(area[j] > 0.0)) {
            throw std::runtime_error("a latitude band holds no cell centre: too many bands");
        }
        forcing.surface_pressure_Pa[j] /= area[j];
        forcing.surface_height_m[j] /= area[j];
        forcing.drag_coefficient[j] /= area[j];
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t c = k * bands + j;
            forcing.temperature_K[c] /= mass[j];
            forcing.heating_K_s[c] /= mass[j];
            forcing.heating_derivative_s[c] /= mass[j];
        }
    }
    return forcing;
}

}  // namespace planetsim
