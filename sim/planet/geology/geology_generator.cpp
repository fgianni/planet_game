#include "sim/planet/geology/geology_generator.hpp"

#include "sim/planet/geology/crust.hpp"
#include "sim/planet/geology/plates.hpp"
#include "sim/planet/geology/structural_elevation.hpp"

namespace planetsim {

GeologyState generate_geology(const PlanetMesh& mesh, std::uint64_t world_seed,
                              const GeologyParameters& parameters, std::size_t worker_count) {
    validate_geology_parameters(parameters);
    GeologyState geology;
    generate_plates(mesh, world_seed, parameters, geology, worker_count);
    geology.boundaries = classify_plate_boundaries(mesh, geology);
    assign_continental_crust(mesh, world_seed, parameters, geology, worker_count);
    static_cast<void>(assign_crust_age(mesh, world_seed, parameters, geology));
    compute_structural_elevation(mesh, world_seed, parameters, geology, worker_count);
    return geology;
}

}  // namespace planetsim
