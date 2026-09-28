#pragma once

#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/terrain/terrain_snapshot.hpp"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace planetsim {

class PlanetMesh;
class PlanetState;
struct StateSnapshot;

}  // namespace planetsim

namespace planetsim::godot_bridge {

// Presentation of a PlanetSim planet. PlanetSim owns every value shown here;
// this node only reads StateSnapshot (per tick) and TerrainSnapshot (once
// per generated planet) and turns them into colours and exaggerated relief.
class PlanetMeshNode : public godot::MeshInstance3D {
    GDCLASS(PlanetMeshNode, godot::MeshInstance3D)

  protected:
    static void _bind_methods();

  public:
    enum ViewMode : std::int64_t {
        view_terrain = 0,
        view_plates = 1,
        view_crust_age = 2,
        view_insolation = 3,
        view_mode_count = 4,
    };

    PlanetMeshNode();
    ~PlanetMeshNode() override;

    void rebuild(std::int64_t subdivision = 5, double radius_m = 6'371'000.0,
                 std::int64_t seed = 1, const godot::String& preset = "earth_like");
    void set_simulation_tick(std::int64_t tick);
    void advance_simulation_ticks(std::int64_t ticks);
    [[nodiscard]] std::int64_t get_simulation_tick() const noexcept;
    [[nodiscard]] double get_simulation_time() const noexcept;

    void set_view_mode(std::int64_t mode);
    [[nodiscard]] std::int64_t get_view_mode() const noexcept;
    [[nodiscard]] godot::String get_view_mode_name() const;
    void set_relief_exaggeration(double exaggeration);
    [[nodiscard]] double get_relief_exaggeration() const noexcept;
    void set_day_night_shading(bool enabled);
    [[nodiscard]] bool get_day_night_shading() const noexcept;

    [[nodiscard]] std::int64_t get_seed() const noexcept;
    [[nodiscard]] std::int64_t get_subdivision() const noexcept;
    [[nodiscard]] godot::String get_preset() const;
    [[nodiscard]] double get_sea_level() const noexcept;
    [[nodiscard]] double get_land_fraction() const noexcept;
    [[nodiscard]] std::int64_t get_plate_count() const noexcept;

  private:
    void build_geometry();
    void refresh();
    void render_snapshot(const StateSnapshot& snapshot);

    PlanetParameters parameters_ = PlanetParameters::earth_development();
    SimulationClock clock_;
    std::shared_ptr<const PlanetMesh> mesh_;
    std::unique_ptr<PlanetState> state_;
    TerrainSnapshot terrain_;
    std::vector<std::array<std::uint32_t, 3>> corner_cells_;
    std::uint64_t seed_ = 1;
    godot::String preset_ = "earth_like";

    std::int64_t view_mode_ = view_terrain;
    double relief_exaggeration_ = 25.0;
    bool day_night_shading_ = true;

    godot::PackedVector3Array vertices_;
    godot::PackedVector3Array normals_;
    godot::Ref<godot::ArrayMesh> rendered_mesh_;
};

}  // namespace planetsim::godot_bridge
