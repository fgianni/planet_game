#pragma once

#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/planet/planet_parameters.hpp"

#include <godot_cpp/classes/mesh_instance3d.hpp>

#include <cstdint>
#include <memory>

namespace planetsim {

class PlanetMesh;
class PlanetState;
struct StateSnapshot;

}  // namespace planetsim

namespace planetsim::godot_bridge {

class PlanetMeshNode : public godot::MeshInstance3D {
    GDCLASS(PlanetMeshNode, godot::MeshInstance3D)

  protected:
    static void _bind_methods();

  public:
    PlanetMeshNode();
    ~PlanetMeshNode() override;

    void rebuild(std::int64_t subdivision = 4, double radius_m = 6'371'000.0);
    void set_simulation_tick(std::int64_t tick);
    void advance_simulation_ticks(std::int64_t ticks);
    [[nodiscard]] std::int64_t get_simulation_tick() const noexcept;
    [[nodiscard]] double get_simulation_time() const noexcept;

  private:
    void render_snapshot(const StateSnapshot& snapshot);

    PlanetParameters parameters_ = PlanetParameters::earth_development();
    SimulationClock clock_;
    std::shared_ptr<const PlanetMesh> mesh_;
    std::unique_ptr<PlanetState> state_;
};

}  // namespace planetsim::godot_bridge
