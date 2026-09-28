#include "planet_mesh_node.hpp"

#include "sim/core/serialization/state_snapshot.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace planetsim::godot_bridge {
namespace {

// Everything below is presentation: colour ramps and relief exaggeration.
// No value here feeds back into PlanetSim.

struct ColorStop {
    float value;
    godot::Color color;
};

[[nodiscard]] godot::Color ramp(const ColorStop* stops, std::size_t count, float value) {
    if (value <= stops[0].value) {
        return stops[0].color;
    }
    for (std::size_t index = 1; index < count; ++index) {
        if (value <= stops[index].value) {
            const float t =
                (value - stops[index - 1U].value) / (stops[index].value - stops[index - 1U].value);
            return stops[index - 1U].color.lerp(stops[index].color, t);
        }
    }
    return stops[count - 1U].color;
}

// Height above sea level (m) of dry land.
[[nodiscard]] godot::Color land_color(float height_m) {
    static const ColorStop stops[] = {
        {0.0F, {0.30F, 0.52F, 0.24F}},    {400.0F, {0.52F, 0.62F, 0.32F}},
        {1'200.0F, {0.62F, 0.52F, 0.33F}}, {2'500.0F, {0.52F, 0.43F, 0.37F}},
        {3'800.0F, {0.72F, 0.70F, 0.70F}}, {4'800.0F, {0.97F, 0.97F, 0.99F}},
    };
    return ramp(stops, std::size(stops), height_m);
}

// Depth below sea level (m) of ocean.
[[nodiscard]] godot::Color ocean_color(float depth_m) {
    static const ColorStop stops[] = {
        {0.0F, {0.42F, 0.74F, 0.84F}},    {200.0F, {0.26F, 0.58F, 0.78F}},
        {1'500.0F, {0.12F, 0.36F, 0.66F}}, {4'000.0F, {0.06F, 0.20F, 0.50F}},
        {7'000.0F, {0.02F, 0.07F, 0.26F}},
    };
    return ramp(stops, std::size(stops), depth_m);
}

[[nodiscard]] godot::Color insolation_color(float insolation_W_m2, double incident_W_m2) {
    const float normalized = incident_W_m2 > 0.0
                                 ? std::clamp(insolation_W_m2 / static_cast<float>(incident_W_m2),
                                              0.0F, 1.0F)
                                 : 0.0F;
    const float daylight = std::sqrt(normalized);
    return {0.015F + 0.95F * daylight, 0.025F + 0.68F * daylight, 0.08F + 0.28F * normalized, 1.0F};
}

[[nodiscard]] float daylight_factor(float insolation_W_m2, double incident_W_m2) {
    const float normalized = incident_W_m2 > 0.0
                                 ? std::clamp(insolation_W_m2 / static_cast<float>(incident_W_m2),
                                              0.0F, 1.0F)
                                 : 0.0F;
    return 0.16F + 0.84F * std::sqrt(normalized);
}

[[nodiscard]] godot::Color plate_color(std::uint16_t plate, bool continental) {
    const float hue = std::fmod(static_cast<float>(plate) * 0.618034F, 1.0F);
    return godot::Color::from_hsv(hue, 0.50F, continental ? 0.88F : 0.58F);
}

[[nodiscard]] godot::Color boundary_color(std::uint8_t boundary_class) {
    switch (static_cast<BoundaryClass>(boundary_class)) {
    case BoundaryClass::convergent:
        return {0.90F, 0.15F, 0.12F};
    case BoundaryClass::divergent:
        return {0.15F, 0.35F, 0.95F};
    case BoundaryClass::transform:
        return {0.20F, 0.80F, 0.25F};
    case BoundaryClass::none:
        break;
    }
    return {};
}

[[nodiscard]] godot::Color age_color(float age_myr, bool continental) {
    if (continental) {
        const float t = std::clamp(age_myr / 3'500.0F, 0.0F, 1.0F);
        return godot::Color{0.55F, 0.52F, 0.48F}.lerp(godot::Color{0.85F, 0.80F, 0.70F}, t);
    }
    const float t = std::clamp(age_myr / 180.0F, 0.0F, 1.0F);
    return godot::Color::from_hsv(0.66F * t, 0.75F, 0.85F);
}

[[nodiscard]] godot::Vector3 to_godot(const Vec3d& vector) {
    return {static_cast<godot::real_t>(vector.x), static_cast<godot::real_t>(vector.y),
            static_cast<godot::real_t>(vector.z)};
}

}  // namespace

PlanetMeshNode::PlanetMeshNode() = default;
PlanetMeshNode::~PlanetMeshNode() = default;

void PlanetMeshNode::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("rebuild", "subdivision", "radius_m", "seed", "preset"),
        &PlanetMeshNode::rebuild, DEFVAL(5), DEFVAL(6'371'000.0), DEFVAL(1), DEFVAL("earth_like"));
    godot::ClassDB::bind_method(godot::D_METHOD("set_simulation_tick", "tick"),
                                &PlanetMeshNode::set_simulation_tick);
    godot::ClassDB::bind_method(godot::D_METHOD("advance_simulation_ticks", "ticks"),
                                &PlanetMeshNode::advance_simulation_ticks);
    godot::ClassDB::bind_method(godot::D_METHOD("get_simulation_tick"),
                                &PlanetMeshNode::get_simulation_tick);
    godot::ClassDB::bind_method(godot::D_METHOD("get_simulation_time"),
                                &PlanetMeshNode::get_simulation_time);
    godot::ClassDB::bind_method(godot::D_METHOD("set_view_mode", "mode"),
                                &PlanetMeshNode::set_view_mode);
    godot::ClassDB::bind_method(godot::D_METHOD("get_view_mode"), &PlanetMeshNode::get_view_mode);
    godot::ClassDB::bind_method(godot::D_METHOD("get_view_mode_name"),
                                &PlanetMeshNode::get_view_mode_name);
    godot::ClassDB::bind_method(godot::D_METHOD("set_relief_exaggeration", "exaggeration"),
                                &PlanetMeshNode::set_relief_exaggeration);
    godot::ClassDB::bind_method(godot::D_METHOD("get_relief_exaggeration"),
                                &PlanetMeshNode::get_relief_exaggeration);
    godot::ClassDB::bind_method(godot::D_METHOD("set_day_night_shading", "enabled"),
                                &PlanetMeshNode::set_day_night_shading);
    godot::ClassDB::bind_method(godot::D_METHOD("get_day_night_shading"),
                                &PlanetMeshNode::get_day_night_shading);
    godot::ClassDB::bind_method(godot::D_METHOD("get_seed"), &PlanetMeshNode::get_seed);
    godot::ClassDB::bind_method(godot::D_METHOD("get_subdivision"),
                                &PlanetMeshNode::get_subdivision);
    godot::ClassDB::bind_method(godot::D_METHOD("get_preset"), &PlanetMeshNode::get_preset);
    godot::ClassDB::bind_method(godot::D_METHOD("get_sea_level"), &PlanetMeshNode::get_sea_level);
    godot::ClassDB::bind_method(godot::D_METHOD("get_land_fraction"),
                                &PlanetMeshNode::get_land_fraction);
    godot::ClassDB::bind_method(godot::D_METHOD("get_plate_count"),
                                &PlanetMeshNode::get_plate_count);
}

void PlanetMeshNode::rebuild(std::int64_t subdivision, double radius_m, std::int64_t seed,
                             const godot::String& preset) {
    try {
        constexpr std::int64_t maximum_preview_subdivision = 7;
        if (subdivision < 0 || subdivision > maximum_preview_subdivision) {
            godot::UtilityFunctions::push_error(
                "PlanetMeshNode subdivision must be between 0 and 7");
            return;
        }
        const std::string preset_name{preset.utf8().get_data()};
        const auto planet_preset = parse_planet_preset(preset_name);
        if (!planet_preset) {
            godot::UtilityFunctions::push_error("PlanetMeshNode: unknown preset ", preset);
            return;
        }

        auto next_parameters = PlanetParameters::earth_development();
        next_parameters.mesh_subdivision = static_cast<std::uint32_t>(subdivision);
        next_parameters.radius_m = radius_m;
        next_parameters.validate();
        auto next_mesh = std::make_shared<const PlanetMesh>(
            make_icosphere(next_parameters.mesh_subdivision, next_parameters.radius_m));
        auto next_state = std::make_unique<PlanetState>(next_mesh);
        const std::size_t workers = std::max<std::size_t>(1U, std::thread::hardware_concurrency());
        const auto next_seed = static_cast<std::uint64_t>(seed);
        const TerrainGeneration generation = generate_terrain(
            *next_state, next_seed, geology_parameters_for(*planet_preset), workers);
        TerrainSnapshot next_terrain =
            make_terrain_snapshot(*next_state, generation.geology, workers);
        update_solar_forcing(*next_state, next_parameters, 0);

        std::vector<std::array<std::uint32_t, 3>> next_corner_cells(next_mesh->corner_count());
        std::vector<std::uint8_t> corner_fill(next_mesh->corner_count(), 0U);
        for (const auto& cell : next_mesh->cells()) {
            for (const CornerIndex corner : next_mesh->cell_corners(cell.id)) {
                if (corner_fill[corner] >= 3U) {
                    throw std::logic_error("a dual-mesh corner has more than three cells");
                }
                next_corner_cells[corner][corner_fill[corner]++] = cell.id.value();
            }
        }

        parameters_ = next_parameters;
        mesh_ = std::move(next_mesh);
        state_ = std::move(next_state);
        terrain_ = std::move(next_terrain);
        corner_cells_ = std::move(next_corner_cells);
        seed_ = next_seed;
        preset_ = preset;
        clock_.reset();
        build_geometry();
        refresh();
    } catch (const std::exception& exception) {
        godot::UtilityFunctions::push_error(exception.what());
    }
}

void PlanetMeshNode::set_simulation_tick(std::int64_t tick) {
    try {
        if (!state_) {
            throw std::logic_error("PlanetMeshNode must be rebuilt before setting simulation tick");
        }
        clock_.set_tick(tick);
        refresh();
    } catch (const std::exception& exception) {
        godot::UtilityFunctions::push_error(exception.what());
    }
}

void PlanetMeshNode::advance_simulation_ticks(std::int64_t ticks) {
    try {
        if (!state_) {
            throw std::logic_error("PlanetMeshNode must be rebuilt before advancing simulation");
        }
        if (ticks == 0) {
            return;
        }
        clock_.advance_ticks(ticks);
        refresh();
    } catch (const std::exception& exception) {
        godot::UtilityFunctions::push_error(exception.what());
    }
}

std::int64_t PlanetMeshNode::get_simulation_tick() const noexcept { return clock_.tick(); }
double PlanetMeshNode::get_simulation_time() const noexcept { return clock_.time_s(); }

void PlanetMeshNode::set_view_mode(std::int64_t mode) {
    if (mode < 0 || mode >= view_mode_count) {
        godot::UtilityFunctions::push_error("PlanetMeshNode: view mode must be 0..3");
        return;
    }
    view_mode_ = mode;
    if (state_) {
        refresh();
    }
}

std::int64_t PlanetMeshNode::get_view_mode() const noexcept { return view_mode_; }

godot::String PlanetMeshNode::get_view_mode_name() const {
    switch (view_mode_) {
    case view_terrain:
        return "terrain";
    case view_plates:
        return "plates and boundaries";
    case view_crust_age:
        return "crust age";
    case view_insolation:
        return "top-of-atmosphere insolation";
    default:
        return "unknown";
    }
}

void PlanetMeshNode::set_relief_exaggeration(double exaggeration) {
    relief_exaggeration_ = std::clamp(exaggeration, 0.0, 200.0);
    if (state_) {
        build_geometry();
        refresh();
    }
}

double PlanetMeshNode::get_relief_exaggeration() const noexcept { return relief_exaggeration_; }

void PlanetMeshNode::set_day_night_shading(bool enabled) {
    day_night_shading_ = enabled;
    if (state_) {
        refresh();
    }
}

bool PlanetMeshNode::get_day_night_shading() const noexcept { return day_night_shading_; }
std::int64_t PlanetMeshNode::get_seed() const noexcept { return static_cast<std::int64_t>(seed_); }
std::int64_t PlanetMeshNode::get_subdivision() const noexcept {
    return static_cast<std::int64_t>(parameters_.mesh_subdivision);
}
godot::String PlanetMeshNode::get_preset() const { return preset_; }
double PlanetMeshNode::get_sea_level() const noexcept { return terrain_.sea_level_m; }
double PlanetMeshNode::get_land_fraction() const noexcept { return terrain_.land_area_fraction; }
std::int64_t PlanetMeshNode::get_plate_count() const noexcept { return terrain_.plate_count; }

// Relief: each cell is a fan of triangles from its centre to its corners. A
// vertex stands at sea level over ocean and at its height above sea level
// over land, multiplied by relief_exaggeration_. Corner values are the mean
// of the three cells sharing the corner.
void PlanetMeshNode::build_geometry() {
    const std::size_t packed_size = mesh_->directed_edge_count() * 3U;
    if (packed_size > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::length_error("PlanetMeshNode preview is too large");
    }
    const double sea_level_m = terrain_.sea_level_m;
    const double scale = relief_exaggeration_ / mesh_->radius_m();
    const auto height_above_sea = [&](double elevation_m, double land_fraction) {
        return land_fraction >= 0.5 ? std::max(0.0, elevation_m - sea_level_m) : 0.0;
    };
    std::vector<godot::Vector3> corner_position(mesh_->corner_count());
    for (std::size_t corner = 0; corner < corner_cells_.size(); ++corner) {
        double elevation = 0.0;
        double land = 0.0;
        for (const std::uint32_t cell : corner_cells_[corner]) {
            elevation += terrain_.mean_elevation_m[cell];
            land += terrain_.land_fraction[cell];
        }
        const double radius = 1.0 + scale * height_above_sea(elevation / 3.0, land / 3.0);
        corner_position[corner] = to_godot(mesh_->corners_unit()[corner] * radius);
    }

    vertices_.resize(static_cast<std::int64_t>(packed_size));
    normals_.resize(static_cast<std::int64_t>(packed_size));
    std::int64_t packed = 0;
    for (const auto& cell : mesh_->cells()) {
        const std::size_t index = cell.id.to_index();
        const double radius = 1.0 + scale * height_above_sea(terrain_.mean_elevation_m[index],
                                                             terrain_.land_fraction[index]);
        const godot::Vector3 center = to_godot(cell.center_unit * radius);
        const auto corners = mesh_->cell_corners(cell.id);
        for (std::size_t k = 0; k < corners.size(); ++k) {
            const godot::Vector3 first = corner_position[corners[k]];
            const godot::Vector3 second = corner_position[corners[(k + 1U) % corners.size()]];
            godot::Vector3 normal = (first - center).cross(second - center).normalized();
            if (normal.dot(center) < 0.0F) {
                normal = -normal;
            }
            for (const godot::Vector3& point : {center, first, second}) {
                vertices_.set(packed, point);
                normals_.set(packed, normal);
                ++packed;
            }
        }
    }
}

void PlanetMeshNode::refresh() {
    update_solar_forcing(*state_, parameters_, clock_.tick());
    render_snapshot(make_state_snapshot(*state_, clock_));
}

void PlanetMeshNode::render_snapshot(const StateSnapshot& snapshot) {
    try {
        const std::size_t cell_count = mesh_->cell_count();
        if (snapshot.top_of_atmosphere_insolation_W_m2.size() != cell_count ||
            terrain_.mean_elevation_m.size() != cell_count) {
            throw std::logic_error("snapshot sizes do not match the presentation mesh");
        }
        const auto& insolation = snapshot.top_of_atmosphere_insolation_W_m2;
        const double incident = snapshot.incident_solar_flux_W_m2;
        const double sea_level_m = terrain_.sea_level_m;

        // Terrain colour from interpolated elevation and land fraction.
        const auto terrain_color = [&](double elevation_m, double land_fraction) {
            return land_fraction >= 0.5
                       ? land_color(static_cast<float>(elevation_m - sea_level_m))
                       : ocean_color(static_cast<float>(sea_level_m - elevation_m));
        };
        // Per-cell colour for the categorical views.
        const auto cell_color = [&](std::size_t index) {
            const bool continental = terrain_.crust_type[index] != 0U;
            switch (view_mode_) {
            case view_plates:
                return terrain_.boundary_class[index] != 0U
                           ? boundary_color(terrain_.boundary_class[index])
                           : plate_color(terrain_.plate_id[index], continental);
            case view_crust_age:
                return age_color(terrain_.crust_age_myr[index], continental);
            case view_insolation:
                return insolation_color(insolation[index], incident);
            default:
                return terrain_color(terrain_.mean_elevation_m[index], terrain_.land_fraction[index]);
            }
        };
        const bool shade = day_night_shading_ && view_mode_ != view_insolation;

        std::vector<godot::Color> corner_color(mesh_->corner_count());
        std::vector<float> corner_light(mesh_->corner_count(), 1.0F);
        for (std::size_t corner = 0; corner < corner_cells_.size(); ++corner) {
            double elevation = 0.0;
            double land = 0.0;
            float light = 0.0F;
            for (const std::uint32_t cell : corner_cells_[corner]) {
                elevation += terrain_.mean_elevation_m[cell];
                land += terrain_.land_fraction[cell];
                light += daylight_factor(insolation[cell], incident);
            }
            corner_color[corner] = terrain_color(elevation / 3.0, land / 3.0);
            corner_light[corner] = light / 3.0F;
        }

        godot::PackedColorArray colors;
        colors.resize(vertices_.size());
        std::int64_t packed = 0;
        for (const auto& cell : mesh_->cells()) {
            const std::size_t index = cell.id.to_index();
            const godot::Color center_color = cell_color(index);
            const float center_light = daylight_factor(insolation[index], incident);
            const auto corners = mesh_->cell_corners(cell.id);
            for (std::size_t k = 0; k < corners.size(); ++k) {
                const CornerIndex first = corners[k];
                const CornerIndex second = corners[(k + 1U) % corners.size()];
                const bool smooth = view_mode_ == view_terrain;
                godot::Color triangle[3] = {center_color,
                                            smooth ? corner_color[first] : center_color,
                                            smooth ? corner_color[second] : center_color};
                const float light[3] = {center_light, corner_light[first], corner_light[second]};
                for (int vertex = 0; vertex < 3; ++vertex) {
                    godot::Color color = triangle[vertex];
                    if (shade) {
                        color = godot::Color(color.r * light[vertex], color.g * light[vertex],
                                             color.b * light[vertex], 1.0F);
                    }
                    colors.set(packed++, color);
                }
            }
        }

        godot::Array arrays;
        arrays.resize(godot::Mesh::ARRAY_MAX);
        arrays[godot::Mesh::ARRAY_VERTEX] = vertices_;
        arrays[godot::Mesh::ARRAY_NORMAL] = normals_;
        arrays[godot::Mesh::ARRAY_COLOR] = colors;

        if (rendered_mesh_.is_null()) {
            rendered_mesh_.instantiate();
            godot::Ref<godot::StandardMaterial3D> material;
            material.instantiate();
            material->set_cull_mode(godot::BaseMaterial3D::CULL_DISABLED);
            material->set_flag(godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
            // The ramps above are authored in sRGB.
            material->set_flag(godot::BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
            material->set_roughness(0.9F);
            set_material_override(material);
            set_mesh(rendered_mesh_);
        }
        rendered_mesh_->clear_surfaces();
        rendered_mesh_->add_surface_from_arrays(godot::Mesh::PRIMITIVE_TRIANGLES, arrays);
    } catch (const std::exception& exception) {
        godot::UtilityFunctions::push_error(exception.what());
    }
}

}  // namespace planetsim::godot_bridge
