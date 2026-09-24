#pragma once

#include <godot_cpp/classes/mesh_instance3d.hpp>

#include <cstdint>

namespace planetsim::godot_bridge {

class PlanetMeshNode : public godot::MeshInstance3D {
    GDCLASS(PlanetMeshNode, godot::MeshInstance3D)

protected:
    static void _bind_methods();

public:
    void rebuild(std::int64_t subdivision = 4, double radius_m = 6'371'000.0);
};

}  // namespace planetsim::godot_bridge
