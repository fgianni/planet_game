#include "planet_mesh_node.hpp"

#include "sim/core/serialization/state_snapshot.hpp"
#include "sim/planet/mesh/icosphere.hpp"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>

namespace planetsim::godot_bridge {
namespace {

[[nodiscard]] StateSnapshot make_demo_snapshot(const PlanetMesh& mesh) {
    StateSnapshot snapshot;
    snapshot.surface_scalar.reserve(mesh.cell_count());
    for (const auto& cell : mesh.cells()) {
        snapshot.surface_scalar.push_back(static_cast<float>(cell.center_unit.z));
    }
    return snapshot;
}

[[nodiscard]] godot::Color scalar_color(float scalar) {
    const float normalized_scalar = std::clamp(0.5F * (scalar + 1.0F), 0.0F, 1.0F);
    return {
        0.08F + 0.82F * normalized_scalar,
        0.25F + 0.55F * (1.0F - std::abs(2.0F * normalized_scalar - 1.0F)),
        0.92F - 0.78F * normalized_scalar,
        1.0F,
    };
}

}  // namespace

void PlanetMeshNode::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("rebuild", "subdivision", "radius_m"),
        &PlanetMeshNode::rebuild,
        DEFVAL(4),
        DEFVAL(6'371'000.0));
}

void PlanetMeshNode::rebuild(std::int64_t subdivision, double radius_m) {
    try {
        constexpr std::int64_t maximum_preview_subdivision = 7;
        if (subdivision < 0 || subdivision > maximum_preview_subdivision) {
            godot::UtilityFunctions::push_error(
                "PlanetMeshNode subdivision must be between 0 and 7");
            return;
        }

        const auto mesh =
            make_icosphere(static_cast<std::uint32_t>(subdivision), radius_m);
        const auto snapshot = make_demo_snapshot(mesh);
        const std::size_t packed_size = mesh.cell_count() * 3U;
        if (packed_size >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            godot::UtilityFunctions::push_error("PlanetMeshNode preview is too large");
            return;
        }

        godot::PackedVector3Array vertices;
        godot::PackedVector3Array normals;
        godot::PackedColorArray colors;
        const auto godot_size = static_cast<std::int64_t>(packed_size);
        vertices.resize(godot_size);
        normals.resize(godot_size);
        colors.resize(godot_size);

        for (std::size_t cell_index = 0; cell_index < mesh.cell_count(); ++cell_index) {
            const auto& cell = mesh.cells()[cell_index];
            const auto color = scalar_color(snapshot.surface_scalar[cell_index]);
            for (std::size_t corner = 0; corner < cell.vertex_indices.size(); ++corner) {
                const auto& point = mesh.vertices_unit()[cell.vertex_indices[corner]];
                const godot::Vector3 position{
                    static_cast<godot::real_t>(point.x),
                    static_cast<godot::real_t>(point.y),
                    static_cast<godot::real_t>(point.z),
                };
                const auto packed_index =
                    static_cast<std::int64_t>(cell_index * 3U + corner);
                vertices.set(packed_index, position);
                normals.set(packed_index, position);
                colors.set(packed_index, color);
            }
        }

        godot::Array arrays;
        arrays.resize(godot::Mesh::ARRAY_MAX);
        arrays[godot::Mesh::ARRAY_VERTEX] = vertices;
        arrays[godot::Mesh::ARRAY_NORMAL] = normals;
        arrays[godot::Mesh::ARRAY_COLOR] = colors;

        godot::Ref<godot::ArrayMesh> rendered_mesh;
        rendered_mesh.instantiate();
        rendered_mesh->add_surface_from_arrays(godot::Mesh::PRIMITIVE_TRIANGLES, arrays);

        godot::Ref<godot::StandardMaterial3D> material;
        material.instantiate();
        material->set_cull_mode(godot::BaseMaterial3D::CULL_DISABLED);
        material->set_flag(
            godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        rendered_mesh->surface_set_material(0, material);
        set_mesh(rendered_mesh);
    } catch (const std::exception& exception) {
        godot::UtilityFunctions::push_error(exception.what());
    }
}

}  // namespace planetsim::godot_bridge
