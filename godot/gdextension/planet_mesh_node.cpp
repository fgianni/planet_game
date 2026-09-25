#include "planet_mesh_node.hpp"

#include "sim/core/serialization/state_snapshot.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/planet_state.hpp"

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
#include <stdexcept>
#include <utility>

namespace planetsim::godot_bridge {
namespace {

[[nodiscard]] godot::Color insolation_color(float insolation_W_m2,
                                            double incident_solar_flux_W_m2) {
    const float normalized_scalar =
        incident_solar_flux_W_m2 > 0.0
            ? std::clamp(insolation_W_m2 / static_cast<float>(incident_solar_flux_W_m2), 0.0F, 1.0F)
            : 0.0F;
    const float daylight = std::sqrt(normalized_scalar);
    return {
        0.015F + 0.95F * daylight,
        0.025F + 0.68F * daylight,
        0.08F + 0.28F * normalized_scalar,
        1.0F,
    };
}

}  // namespace

PlanetMeshNode::PlanetMeshNode() = default;
PlanetMeshNode::~PlanetMeshNode() = default;

void PlanetMeshNode::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("rebuild", "subdivision", "radius_m"),
                                &PlanetMeshNode::rebuild, DEFVAL(4), DEFVAL(6'371'000.0));
    godot::ClassDB::bind_method(godot::D_METHOD("set_simulation_time", "simulation_time_s"),
                                &PlanetMeshNode::set_simulation_time);
    godot::ClassDB::bind_method(godot::D_METHOD("advance_simulation", "timestep_s"),
                                &PlanetMeshNode::advance_simulation);
    godot::ClassDB::bind_method(godot::D_METHOD("get_simulation_time"),
                                &PlanetMeshNode::get_simulation_time);
}

void PlanetMeshNode::rebuild(std::int64_t subdivision, double radius_m) {
    try {
        constexpr std::int64_t maximum_preview_subdivision = 7;
        if (subdivision < 0 || subdivision > maximum_preview_subdivision) {
            godot::UtilityFunctions::push_error(
                "PlanetMeshNode subdivision must be between 0 and 7");
            return;
        }

        auto next_parameters = PlanetParameters::earth_development();
        next_parameters.mesh_subdivision = static_cast<std::uint32_t>(subdivision);
        next_parameters.radius_m = radius_m;
        next_parameters.validate();
        auto next_mesh = std::make_shared<const PlanetMesh>(
            make_icosphere(next_parameters.mesh_subdivision, next_parameters.radius_m));
        auto next_state = std::make_unique<PlanetState>(next_mesh);
        update_solar_forcing(*next_state, next_parameters, 0.0);

        parameters_ = next_parameters;
        mesh_ = std::move(next_mesh);
        state_ = std::move(next_state);
        clock_.reset();
        render_snapshot(make_state_snapshot(*state_, clock_));
    } catch (const std::exception& exception) {
        godot::UtilityFunctions::push_error(exception.what());
    }
}

void PlanetMeshNode::set_simulation_time(double simulation_time_s) {
    try {
        if (!std::isfinite(simulation_time_s) || simulation_time_s < 0.0) {
            throw std::invalid_argument("simulation time must be finite and non-negative");
        }
        if (!state_) {
            throw std::logic_error("PlanetMeshNode must be rebuilt before setting simulation time");
        }
        clock_.reset();
        if (simulation_time_s > 0.0) {
            clock_.advance(simulation_time_s);
        }
        update_solar_forcing(*state_, parameters_, clock_.time_s());
        render_snapshot(make_state_snapshot(*state_, clock_));
    } catch (const std::exception& exception) {
        godot::UtilityFunctions::push_error(exception.what());
    }
}

void PlanetMeshNode::advance_simulation(double timestep_s) {
    try {
        if (!state_) {
            throw std::logic_error("PlanetMeshNode must be rebuilt before advancing simulation");
        }
        if (timestep_s == 0.0) {
            return;
        }
        clock_.advance(timestep_s);
        update_solar_forcing(*state_, parameters_, clock_.time_s());
        render_snapshot(make_state_snapshot(*state_, clock_));
    } catch (const std::exception& exception) {
        godot::UtilityFunctions::push_error(exception.what());
    }
}

double PlanetMeshNode::get_simulation_time() const noexcept { return clock_.time_s(); }

void PlanetMeshNode::render_snapshot(const StateSnapshot& snapshot) {
    try {
        if (!mesh_ || snapshot.top_of_atmosphere_insolation_W_m2.size() != mesh_->cell_count()) {
            throw std::logic_error("snapshot insolation size does not match the presentation mesh");
        }

        const std::size_t packed_size = mesh_->cell_count() * 3U;
        if (packed_size > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
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

        for (std::size_t cell_index = 0; cell_index < mesh_->cell_count(); ++cell_index) {
            const auto& cell = mesh_->cells()[cell_index];
            const auto color =
                insolation_color(snapshot.top_of_atmosphere_insolation_W_m2[cell_index],
                                 snapshot.incident_solar_flux_W_m2);
            for (std::size_t corner = 0; corner < cell.vertex_indices.size(); ++corner) {
                const auto& point = mesh_->vertices_unit()[cell.vertex_indices[corner]];
                const godot::Vector3 position{
                    static_cast<godot::real_t>(point.x),
                    static_cast<godot::real_t>(point.y),
                    static_cast<godot::real_t>(point.z),
                };
                const auto packed_index = static_cast<std::int64_t>(cell_index * 3U + corner);
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
        material->set_shading_mode(godot::BaseMaterial3D::SHADING_MODE_UNSHADED);
        material->set_flag(godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        rendered_mesh->surface_set_material(0, material);
        set_mesh(rendered_mesh);
    } catch (const std::exception& exception) {
        godot::UtilityFunctions::push_error(exception.what());
    }
}

}  // namespace planetsim::godot_bridge
