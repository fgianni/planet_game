#include "sim/planet/atmosphere/water.hpp"

#include "sim/planet/atmosphere/atmosphere.hpp"
#include "sim/planet/atmosphere/saturation.hpp"

#include <stdexcept>

namespace planetsim {

void initialise_water(const PlanetMesh& mesh, SlowState& slow) {
    const std::size_t cells = mesh.cell_count();
    const std::size_t layers = slow.atmosphere_layer_count();
    if (slow.atmosphere_surface_pressure_Pa.size() != cells && layers > 0U) {
        throw std::invalid_argument("initialise_water needs an initialised atmosphere");
    }
    slow.atmosphere_specific_humidity_kg_kg = Field3D<double>(layers, cells, 0.0);
    slow.land_surface_water_kg_m2 =
        Field2D<double>(cells, layers > 0U ? 0.5 * bucket_capacity_kg_m2 : 0.0);
    for (std::size_t layer = 0; layer < layers; ++layer) {
        const double sigma = layer_sigma(layer, layers);
        const auto temperature = slow.atmosphere_temperature_K.layer(layer);
        auto humidity = slow.atmosphere_specific_humidity_kg_kg.layer(layer);
        for (std::size_t cell = 0; cell < cells; ++cell) {
            humidity[cell] = initial_relative_humidity *
                             saturation_specific_humidity(
                                 temperature[cell],
                                 sigma * slow.atmosphere_surface_pressure_Pa[cell]);
        }
    }
}

}  // namespace planetsim
