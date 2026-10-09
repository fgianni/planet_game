#include "planet_mesh_node.hpp"

#include "sim/core/serialization/state_snapshot.hpp"
#include "sim/planet/geology/geology_parameters.hpp"
#include "sim/planet/mesh/icosphere.hpp"
#include "sim/planet/orbit/solar_forcing.hpp"
#include "sim/planet/run/planet_run.hpp"
#include "sim/planet/planet_state.hpp"
#include "sim/planet/terrain/terrain_generator.hpp"
#include "sim/presentation/presentation_record.hpp"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
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

constexpr std::int32_t channel_texture_width = 2048;

[[nodiscard]] std::size_t channel_index(presentation::ChannelId id) {
    return static_cast<std::size_t>(id) - 1U;
}

[[nodiscard]] godot::Vector2 texel_of(std::size_t texel) {
    const auto width = static_cast<std::size_t>(channel_texture_width);
    return {static_cast<godot::real_t>(texel % width), static_cast<godot::real_t>(texel / width)};
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

[[nodiscard]] float drainage_weight(float catchment_area_m2, double largest_catchment_area_m2) {
    if (!(catchment_area_m2 > 0.0F) || !(largest_catchment_area_m2 > 0.0)) {
        return 0.0F;
    }
    const double fraction =
        std::clamp(static_cast<double>(catchment_area_m2) / largest_catchment_area_m2, 0.0, 1.0);
    return static_cast<float>(std::log1p(99.0 * fraction) / std::log(100.0));
}

[[nodiscard]] godot::Color drainage_color(float land_fraction, std::uint32_t downstream,
                                           std::uint32_t depression_id, float weight) {
    if (!(land_fraction > 0.0F)) {
        return {0.015F, 0.065F, 0.14F};
    }
    if (depression_id != no_depression) {
        return godot::Color{0.42F, 0.12F, 0.52F}.lerp({0.92F, 0.36F, 0.88F}, weight);
    }
    const godot::Color accumulated =
        godot::Color{0.13F, 0.16F, 0.12F}.lerp({0.08F, 0.82F, 0.95F}, weight);
    return downstream == no_downstream ? accumulated.lerp({0.72F, 1.0F, 1.0F}, 0.55F)
                                       : accumulated;
}

[[nodiscard]] godot::Vector3 to_godot(const Vec3d& vector) {
    return {static_cast<godot::real_t>(vector.x), static_cast<godot::real_t>(vector.y),
            static_cast<godot::real_t>(vector.z)};
}

}  // namespace

PlanetMeshNode::PlanetMeshNode() = default;
PlanetMeshNode::~PlanetMeshNode() { stop_live_run(); }

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
    godot::ClassDB::bind_method(godot::D_METHOD("set_style", "style_name"),
                                &PlanetMeshNode::set_style);
    godot::ClassDB::bind_method(godot::D_METHOD("next_style"), &PlanetMeshNode::next_style);
    godot::ClassDB::bind_method(godot::D_METHOD("get_style"), &PlanetMeshNode::get_style);
    godot::ClassDB::bind_method(godot::D_METHOD("get_available_styles"),
                                &PlanetMeshNode::get_available_styles);
    godot::ClassDB::bind_method(godot::D_METHOD("validate_style_manifest", "path"),
                                &PlanetMeshNode::validate_style_manifest);
    godot::ClassDB::bind_method(godot::D_METHOD("load_presentation_record", "path"),
                                &PlanetMeshNode::load_presentation_record);
    godot::ClassDB::bind_method(godot::D_METHOD("clear_presentation_record"),
                                &PlanetMeshNode::clear_presentation_record);
    godot::ClassDB::bind_method(godot::D_METHOD("has_presentation_record"),
                                &PlanetMeshNode::has_presentation_record);
    godot::ClassDB::bind_method(godot::D_METHOD("get_presentation_frame_count"),
                                &PlanetMeshNode::get_presentation_frame_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_presentation_frame"),
                                &PlanetMeshNode::get_presentation_frame);
    godot::ClassDB::bind_method(godot::D_METHOD("set_presentation_frame", "frame"),
                                &PlanetMeshNode::set_presentation_frame);
    godot::ClassDB::bind_method(godot::D_METHOD("advance_presentation_frame"),
                                &PlanetMeshNode::advance_presentation_frame);
    godot::ClassDB::bind_method(
        godot::D_METHOD("advance_presentation", "wall_seconds", "simulated_years_per_wall_second"),
        &PlanetMeshNode::advance_presentation);
    godot::ClassDB::bind_method(godot::D_METHOD("get_geometry_revision"),
                                &PlanetMeshNode::get_geometry_revision);
    godot::ClassDB::bind_method(
        godot::D_METHOD("start_live_run", "workers", "water_cycle"),
        &PlanetMeshNode::start_live_run, DEFVAL(0), DEFVAL(false));
    godot::ClassDB::bind_method(godot::D_METHOD("stop_live_run"),
                                &PlanetMeshNode::stop_live_run);
    godot::ClassDB::bind_method(godot::D_METHOD("has_live_run"),
                                &PlanetMeshNode::has_live_run);
    godot::ClassDB::bind_method(godot::D_METHOD("request_live_steps", "steps"),
                                &PlanetMeshNode::request_live_steps, DEFVAL(1));
    godot::ClassDB::bind_method(godot::D_METHOD("poll_live_frame"),
                                &PlanetMeshNode::poll_live_frame);
    godot::ClassDB::bind_method(godot::D_METHOD("is_live_run_busy"),
                                &PlanetMeshNode::is_live_run_busy);
    godot::ClassDB::bind_method(godot::D_METHOD("get_live_state_hash"),
                                &PlanetMeshNode::get_live_state_hash);
    godot::ClassDB::bind_method(godot::D_METHOD("find_cell", "direction"),
                                &PlanetMeshNode::find_cell);
    godot::ClassDB::bind_method(godot::D_METHOD("has_channel", "channel_name"),
                                &PlanetMeshNode::has_channel);
    godot::ClassDB::bind_method(godot::D_METHOD("get_channel_value", "cell", "channel_name"),
                                &PlanetMeshNode::get_channel_value);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_vector_channel_value", "cell", "channel_name"),
        &PlanetMeshNode::get_vector_channel_value);
    godot::ClassDB::bind_method(godot::D_METHOD("get_wind_streak_count"),
                                &PlanetMeshNode::get_wind_streak_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_seed"), &PlanetMeshNode::get_seed);
    godot::ClassDB::bind_method(godot::D_METHOD("get_subdivision"),
                                &PlanetMeshNode::get_subdivision);
    godot::ClassDB::bind_method(godot::D_METHOD("get_preset"), &PlanetMeshNode::get_preset);
    godot::ClassDB::bind_method(godot::D_METHOD("get_sea_level"), &PlanetMeshNode::get_sea_level);
    godot::ClassDB::bind_method(godot::D_METHOD("get_land_fraction"),
                                &PlanetMeshNode::get_land_fraction);
    godot::ClassDB::bind_method(godot::D_METHOD("get_plate_count"),
                                &PlanetMeshNode::get_plate_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_drainage_outlet_count"),
                                &PlanetMeshNode::get_drainage_outlet_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_drainage_basin_count"),
                                &PlanetMeshNode::get_drainage_basin_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_drainage_depression_count"),
                                &PlanetMeshNode::get_drainage_depression_count);
}

void PlanetMeshNode::rebuild(std::int64_t subdivision, double radius_m, std::int64_t seed,
                             const godot::String& preset) {
    stop_live_run();
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
        if (*planet_preset == PlanetPreset::dead_rock) {
            next_parameters.axial_tilt_rad = 0.0;
        }
        next_parameters.validate();
        auto next_mesh = std::make_shared<const PlanetMesh>(
            make_icosphere(next_parameters.mesh_subdivision, next_parameters.radius_m));
        auto next_state = std::make_unique<PlanetState>(next_mesh);
        const std::size_t workers = std::max<std::size_t>(1U, std::thread::hardware_concurrency());
        const auto next_seed = static_cast<std::uint64_t>(seed);
        const TerrainGeneration generation = generate_terrain(
            *next_state, next_seed, geology_parameters_for(*planet_preset), workers);
        TerrainSnapshot next_terrain =
            make_terrain_snapshot(*next_state, generation.geology, generation.drainage, workers);
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
        const std::size_t texels = mesh_->cell_count() + mesh_->corner_count();
        texture_width_ = channel_texture_width;
        texture_height_ =
            static_cast<std::int32_t>((texels + static_cast<std::size_t>(texture_width_) - 1U) /
                                      static_cast<std::size_t>(texture_width_));
        for (auto& texture : channel_textures_)
            texture.unref();
        surface_class_ice_texture_.unref();
        for (auto& values : uploaded_channels_)
            values.clear();
        // A new resolution changes every channel's cell count. In particular,
        // do not carry the smoothed snow/ice arrays into the first new frame.
        displayed_frame_ = {};
        target_frame_ = {};
        if (styles_[0].is_null())
            load_styles();
        build_preview_reference();
        build_geometry();
        build_colors();
        upload_mesh();
        if (!presentation_frames_.empty()) {
            presentation_reference_ =
                presentation::make_presentation_reference(presentation_frames_);
            presentation_frame_ = std::min(presentation_frame_, presentation_frames_.size() - 1U);
            set_visual_frame(
                presentation::make_visual_frame(presentation_frames_[presentation_frame_], terrain_,
                                                presentation_reference_),
                true);
        } else {
            refresh();
        }
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
        godot::UtilityFunctions::push_error("PlanetMeshNode: view mode must be 0..6");
        return;
    }
    view_mode_ = mode;
    if (state_) {
        build_colors();
        upload_mesh();
        update_shader_flags();
    }
}

std::int64_t PlanetMeshNode::get_view_mode() const noexcept { return view_mode_; }

godot::String PlanetMeshNode::get_view_mode_name() const {
    switch (view_mode_) {
    case view_style:
        return godot::String("natural (") + get_style() + ")";
    case view_terrain:
        return "terrain (climate-lab overlay)";
    case view_plates:
        return "plates and boundaries";
    case view_crust_age:
        return "crust age";
    case view_insolation:
        return "daylight / top-of-atmosphere insolation";
    case view_drainage:
        return "drainage and catchment area";
    case view_temperature_anomaly:
        return "temperature anomaly (climate-lab overlay)";
    case view_wind:
        return "surface wind particles (climate-lab overlay)";
    case view_precipitation:
        return "precipitation intensity (climate-lab overlay)";
    default:
        return "unknown";
    }
}

void PlanetMeshNode::set_relief_exaggeration(double exaggeration) {
    relief_exaggeration_ = std::clamp(exaggeration, 0.0, 200.0);
    if (state_) {
        build_geometry();
        upload_mesh();
    }
}

double PlanetMeshNode::get_relief_exaggeration() const noexcept { return relief_exaggeration_; }

void PlanetMeshNode::set_day_night_shading(bool enabled) {
    day_night_shading_ = enabled;
    update_shader_flags();
}

bool PlanetMeshNode::get_day_night_shading() const noexcept { return day_night_shading_; }

void PlanetMeshNode::set_style(const godot::String& style_name) {
    if (styles_[0].is_null())
        load_styles();
    for (std::size_t index = 0; index < styles_.size(); ++index) {
        if (styles_[index].is_valid() && styles_[index]->get_style_name() == style_name) {
            active_style_ = index;
            apply_render_material();
            return;
        }
    }
    godot::UtilityFunctions::push_error("PlanetMeshNode: unknown style ", style_name);
}

void PlanetMeshNode::next_style() {
    if (styles_[0].is_null())
        load_styles();
    active_style_ = (active_style_ + 1U) % styles_.size();
    apply_render_material();
}

godot::String PlanetMeshNode::get_style() const {
    return styles_[active_style_].is_valid() ? styles_[active_style_]->get_style_name()
                                             : "unloaded";
}

godot::PackedStringArray PlanetMeshNode::get_available_styles() const {
    godot::PackedStringArray result;
    for (const auto& style : styles_) {
        if (style.is_valid())
            result.push_back(style->get_style_name());
    }
    return result;
}

godot::String PlanetMeshNode::validate_style_manifest(const godot::String& path) const {
    const godot::Ref<godot::Resource> resource = godot::ResourceLoader::get_singleton()->load(path);
    const godot::Ref<PlanetStyle> style = resource;
    return validate_style(style);
}

void PlanetMeshNode::load_presentation_record(const godot::String& path) {
    try {
        if (!mesh_)
            throw std::logic_error("generate the planet before loading a presentation record");
        stop_live_run();
        const std::filesystem::path record_path{std::string(path.utf8().get_data())};
        auto frames = presentation::read_presentation_record(record_path);
        if (frames.empty())
            throw std::runtime_error("presentation record contains no frames");
        const std::size_t cells = mesh_->cell_count();
        for (const StateSnapshot& frame : frames) {
            if (frame.top_of_atmosphere_insolation_W_m2.size() != cells) {
                throw std::runtime_error(
                    "presentation record cell count does not match the planet");
            }
        }
        auto reference = presentation::make_presentation_reference(frames);
        presentation_frames_ = std::move(frames);
        presentation_reference_ = std::move(reference);
        presentation_frame_ = 0U;
        set_visual_frame(presentation::make_visual_frame(presentation_frames_.front(), terrain_,
                                                         presentation_reference_),
                         true);
    } catch (const std::exception& exception) {
        godot::UtilityFunctions::push_error("PlanetMeshNode: ", exception.what());
    }
}

void PlanetMeshNode::clear_presentation_record() {
    presentation_frames_.clear();
    presentation_frame_ = 0U;
    if (state_) {
        build_preview_reference();
        refresh();
    }
}

bool PlanetMeshNode::has_presentation_record() const noexcept {
    return !presentation_frames_.empty();
}
std::int64_t PlanetMeshNode::get_presentation_frame_count() const noexcept {
    return static_cast<std::int64_t>(presentation_frames_.size());
}
std::int64_t PlanetMeshNode::get_presentation_frame() const noexcept {
    return static_cast<std::int64_t>(presentation_frame_);
}

void PlanetMeshNode::set_presentation_frame(std::int64_t frame) {
    try {
        if (presentation_frames_.empty())
            throw std::logic_error("no presentation record is loaded");
        if (frame < 0 || static_cast<std::size_t>(frame) >= presentation_frames_.size()) {
            throw std::out_of_range("presentation frame is out of range");
        }
        presentation_frame_ = static_cast<std::size_t>(frame);
        set_visual_frame(presentation::make_visual_frame(presentation_frames_[presentation_frame_],
                                                         terrain_, presentation_reference_),
                         false);
    } catch (const std::exception& exception) {
        godot::UtilityFunctions::push_error("PlanetMeshNode: ", exception.what());
    }
}

void PlanetMeshNode::advance_presentation_frame() {
    if (!presentation_frames_.empty()) {
        set_presentation_frame(
            static_cast<std::int64_t>((presentation_frame_ + 1U) % presentation_frames_.size()));
    }
}

void PlanetMeshNode::advance_presentation(double wall_seconds,
                                          double simulated_years_per_wall_second) {
    if (!(wall_seconds > 0.0))
        return;
    bool changed = false;
    for (const auto id : {presentation::ChannelId::snow_cover, presentation::ChannelId::sea_ice}) {
        auto& displayed = displayed_frame_.channels[channel_index(id)].values;
        const auto& target = target_frame_.channels[channel_index(id)].values;
        if (displayed.size() != target.size() || target.empty())
            continue;
        const double channel_scale = id == presentation::ChannelId::snow_cover ? 0.20 : 0.12;
        const double tau = std::max(0.08, channel_scale * simulated_years_per_wall_second);
        const float alpha = static_cast<float>(1.0 - std::exp(-wall_seconds / tau));
        for (std::size_t cell = 0; cell < displayed.size(); ++cell) {
            displayed[cell] += (target[cell] - displayed[cell]) * alpha;
        }
        changed = true;
    }
    if (changed)
        upload_changed_channels();
}

std::int64_t PlanetMeshNode::get_geometry_revision() const noexcept { return geometry_revision_; }

void PlanetMeshNode::start_live_run(std::int64_t workers, bool water_cycle) {
    try {
        if (!mesh_) {
            throw std::logic_error("generate the planet before starting a live run");
        }
        stop_live_run();
        const auto parsed_preset = parse_planet_preset(std::string(preset_.utf8().get_data()));
        if (!parsed_preset) {
            throw std::logic_error("the generated planet has an unknown preset");
        }
        if (parameters_.radius_m != PlanetParameters::earth_development().radius_m) {
            throw std::logic_error(
                "live runs require the scenario radius; custom preview radii are presentation-only");
        }
        Scenario scenario;
        scenario.preset = *parsed_preset;
        scenario.seed = seed_;
        scenario.subdivision = parameters_.mesh_subdivision;
        scenario.water_cycle = water_cycle;
        build_preview_reference();
        const std::size_t worker_count = workers > 0
            ? static_cast<std::size_t>(workers)
            : std::max<std::size_t>(1U, std::thread::hardware_concurrency());
        auto run = std::make_unique<PlanetRun>(scenario, worker_count);
        live_state_hash_.store(run->state_hash(), std::memory_order_release);
        presentation_frames_.clear();
        presentation_frame_ = 0U;
        SimulationClock initial_clock;
        StateSnapshot initial = make_state_snapshot(run->state(), initial_clock);
        live_run_ = std::move(run);
        apply_live_snapshot(std::move(initial), true);
        live_thread_ = std::jthread(
            [this](std::stop_token stop_token) { live_run_loop(stop_token); });
    } catch (const std::exception& exception) {
        stop_live_run();
        godot::UtilityFunctions::push_error("PlanetMeshNode live run: ", exception.what());
    }
}

void PlanetMeshNode::stop_live_run() {
    if (live_thread_.joinable()) {
        live_thread_.request_stop();
        live_condition_.notify_all();
        live_thread_.join();
    }
    {
        std::lock_guard lock(live_mutex_);
        live_pending_steps_ = 0;
        live_snapshot_.reset();
        live_error_.clear();
    }
    live_run_.reset();
    live_state_hash_.store(0U, std::memory_order_release);
    live_busy_.store(false, std::memory_order_release);
}

bool PlanetMeshNode::has_live_run() const noexcept { return live_run_ != nullptr; }

void PlanetMeshNode::request_live_steps(std::int64_t steps) {
    if (!live_run_ || steps <= 0)
        return;
    {
        std::lock_guard lock(live_mutex_);
        constexpr std::int64_t maximum_backlog = 2;
        live_pending_steps_ =
            std::min(maximum_backlog,
                     live_pending_steps_ + std::min(steps, maximum_backlog));
    }
    live_condition_.notify_one();
}

bool PlanetMeshNode::poll_live_frame() {
    std::optional<StateSnapshot> snapshot;
    std::string error;
    {
        std::lock_guard lock(live_mutex_);
        snapshot = std::move(live_snapshot_);
        live_snapshot_.reset();
        error = std::move(live_error_);
        live_error_.clear();
    }
    if (!error.empty()) {
        godot::UtilityFunctions::push_error("PlanetMeshNode live run: ", error.c_str());
    }
    if (!snapshot)
        return false;
    apply_live_snapshot(std::move(*snapshot));
    return true;
}

bool PlanetMeshNode::is_live_run_busy() const noexcept {
    if (live_busy_.load(std::memory_order_acquire))
        return true;
    std::lock_guard lock(live_mutex_);
    return live_pending_steps_ > 0;
}

godot::String PlanetMeshNode::get_live_state_hash() const {
    if (!live_run_)
        return {};
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16)
           << live_state_hash_.load(std::memory_order_acquire);
    return godot::String(output.str().c_str());
}

void PlanetMeshNode::live_run_loop(std::stop_token stop_token) {
    for (;;) {
        {
            std::unique_lock lock(live_mutex_);
            live_condition_.wait(lock, [&] {
                return stop_token.stop_requested() || live_pending_steps_ > 0;
            });
            if (stop_token.stop_requested())
                return;
            --live_pending_steps_;
            live_busy_.store(true, std::memory_order_release);
        }
        try {
            StateSnapshot snapshot;
            const SimulationTick boundary = live_run_->scheduler().next_step().end_tick;
            live_run_->run_until(boundary,
                                 [&snapshot](const StateSnapshot& frame) { snapshot = frame; });
            const std::uint64_t hash = live_run_->state_hash();
            {
                std::lock_guard lock(live_mutex_);
                live_snapshot_ = std::move(snapshot);
            }
            live_state_hash_.store(hash, std::memory_order_release);
        } catch (const std::exception& exception) {
            std::lock_guard lock(live_mutex_);
            live_error_ = exception.what();
            live_pending_steps_ = 0;
        }
        live_busy_.store(false, std::memory_order_release);
    }
}

void PlanetMeshNode::apply_live_snapshot(StateSnapshot snapshot, bool reset_smoothing) {
    const bool complete_reference_year =
        snapshot.simulation_tick >= orbital_year_begin_tick(1, parameters_);
    if (complete_reference_year &&
        presentation_reference_.climatology_surface_temperature_mean_K.empty() &&
        !snapshot.climatology_surface_temperature_mean_K.empty()) {
        presentation_reference_.climatology_surface_temperature_mean_K =
            snapshot.climatology_surface_temperature_mean_K;
        presentation_reference_.climatology_surface_temperature_variance_K2 =
            snapshot.climatology_surface_temperature_variance_K2;
    }
    clock_.set_tick(snapshot.simulation_tick);
    set_visual_frame(presentation::make_visual_frame(snapshot, terrain_, presentation_reference_),
                     reset_smoothing);
}

std::int64_t PlanetMeshNode::find_cell(const godot::Vector3& direction) const {
    if (!mesh_ || direction.length_squared() <= 0.0F)
        return -1;
    const godot::Vector3 unit = direction.normalized();
    std::size_t closest = 0U;
    float closest_dot = -std::numeric_limits<float>::infinity();
    for (const auto& cell : mesh_->cells()) {
        const float alignment = unit.dot(to_godot(cell.center_unit));
        if (alignment > closest_dot) {
            closest_dot = alignment;
            closest = cell.id.to_index();
        }
    }
    return static_cast<std::int64_t>(closest);
}

bool PlanetMeshNode::has_channel(const godot::String& channel_name) const {
    for (const auto& descriptor : presentation::channel_registry) {
        if (channel_name == godot::String(descriptor.name.data())) {
            return !displayed_frame_.channels[channel_index(descriptor.id)].values.empty();
        }
    }
    return false;
}

double PlanetMeshNode::get_channel_value(std::int64_t cell,
                                         const godot::String& channel_name) const {
    if (cell < 0 || !mesh_ || static_cast<std::size_t>(cell) >= mesh_->cell_count()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    for (const auto& descriptor : presentation::channel_registry) {
        if (descriptor.kind != presentation::ChannelKind::scalar ||
            channel_name != godot::String(descriptor.name.data())) {
            continue;
        }
        const auto& values = displayed_frame_.channels[channel_index(descriptor.id)].values;
        return values.size() == mesh_->cell_count()
                   ? static_cast<double>(values[static_cast<std::size_t>(cell)])
                   : std::numeric_limits<double>::quiet_NaN();
    }
    return std::numeric_limits<double>::quiet_NaN();
}

godot::Vector2 PlanetMeshNode::get_vector_channel_value(
    std::int64_t cell, const godot::String& channel_name) const {
    const auto absent = [] {
        const auto nan = std::numeric_limits<godot::real_t>::quiet_NaN();
        return godot::Vector2{nan, nan};
    };
    if (cell < 0 || !mesh_ || static_cast<std::size_t>(cell) >= mesh_->cell_count()) {
        return absent();
    }
    for (const auto& descriptor : presentation::channel_registry) {
        if (descriptor.kind != presentation::ChannelKind::vector ||
            channel_name != godot::String(descriptor.name.data())) {
            continue;
        }
        const auto& values = displayed_frame_.channels[channel_index(descriptor.id)].values;
        const std::size_t cells = mesh_->cell_count();
        if (descriptor.id != presentation::ChannelId::wind ||
            values.size() != cells * presentation::wind_vector_component_count) {
            return absent();
        }
        const std::size_t offset =
            static_cast<std::size_t>(cell) * presentation::wind_vector_component_count;
        return {values[offset], values[offset + 1U]};
    }
    return absent();
}

std::int64_t PlanetMeshNode::get_wind_streak_count() const noexcept {
    return wind_streak_count_;
}

std::int64_t PlanetMeshNode::get_seed() const noexcept { return static_cast<std::int64_t>(seed_); }
std::int64_t PlanetMeshNode::get_subdivision() const noexcept {
    return static_cast<std::int64_t>(parameters_.mesh_subdivision);
}
godot::String PlanetMeshNode::get_preset() const { return preset_; }
double PlanetMeshNode::get_sea_level() const noexcept { return terrain_.sea_level_m; }
double PlanetMeshNode::get_land_fraction() const noexcept { return terrain_.land_area_fraction; }
std::int64_t PlanetMeshNode::get_plate_count() const noexcept { return terrain_.plate_count; }
std::int64_t PlanetMeshNode::get_drainage_outlet_count() const noexcept {
    return terrain_.drainage_outlet_count;
}
std::int64_t PlanetMeshNode::get_drainage_basin_count() const noexcept {
    return terrain_.drainage_basin_count;
}
std::int64_t PlanetMeshNode::get_drainage_depression_count() const noexcept {
    return terrain_.drainage_depression_count;
}

// Relief: each cell is a fan of triangles from its centre to its corners. A
// vertex stands at sea level over ocean and at its height above sea level
// over land, multiplied by relief_exaggeration_. Corner values are the mean
// of the three cells sharing the corner. Normals are smooth: the area-weighted
// mean of the face normals around each cell centre and each corner.
void PlanetMeshNode::build_geometry() {
    ++geometry_revision_;
    const std::size_t packed_size = mesh_->directed_edge_count() * 3U;
    if (packed_size > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::length_error("PlanetMeshNode preview is too large");
    }
    const std::size_t cell_count = mesh_->cell_count();
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
    std::vector<godot::Vector3> center_position(cell_count);
    for (const auto& cell : mesh_->cells()) {
        const std::size_t index = cell.id.to_index();
        const double radius = 1.0 + scale * height_above_sea(terrain_.mean_elevation_m[index],
                                                             terrain_.land_fraction[index]);
        center_position[index] = to_godot(cell.center_unit * radius);
    }

    // Smooth normals: accumulate unnormalised face normals (area-weighted).
    std::vector<godot::Vector3> center_normal(cell_count);
    std::vector<godot::Vector3> corner_normal(mesh_->corner_count());
    for (const auto& cell : mesh_->cells()) {
        const std::size_t index = cell.id.to_index();
        const auto corners = mesh_->cell_corners(cell.id);
        for (std::size_t k = 0; k < corners.size(); ++k) {
            const CornerIndex first = corners[k];
            const CornerIndex second = corners[(k + 1U) % corners.size()];
            godot::Vector3 face = (corner_position[first] - center_position[index])
                                      .cross(corner_position[second] - center_position[index]);
            if (face.dot(center_position[index]) < 0.0F) {
                face = -face;
            }
            center_normal[index] += face;
            corner_normal[first] += face;
            corner_normal[second] += face;
        }
    }

    vertices_.resize(static_cast<std::int64_t>(packed_size));
    normals_.resize(static_cast<std::int64_t>(packed_size));
    texels_.resize(static_cast<std::int64_t>(packed_size));
    std::int64_t packed = 0;
    for (const auto& cell : mesh_->cells()) {
        const std::size_t index = cell.id.to_index();
        const auto corners = mesh_->cell_corners(cell.id);
        for (std::size_t k = 0; k < corners.size(); ++k) {
            const CornerIndex first = corners[k];
            const CornerIndex second = corners[(k + 1U) % corners.size()];
            vertices_.set(packed, center_position[index]);
            normals_.set(packed, center_normal[index].normalized());
            texels_.set(packed++, texel_of(index));
            vertices_.set(packed, corner_position[first]);
            normals_.set(packed, corner_normal[first].normalized());
            texels_.set(packed++, texel_of(cell_count + first));
            vertices_.set(packed, corner_position[second]);
            normals_.set(packed, corner_normal[second].normalized());
            texels_.set(packed++, texel_of(cell_count + second));
        }
    }

    constexpr std::size_t route_segments = 8U;
    constexpr double overlay_lift = 1.003;
    std::size_t route_count = 0U;
    for (const std::uint32_t downstream : terrain_.downstream) {
        route_count += downstream != no_downstream ? 1U : 0U;
    }
    const std::size_t route_vertex_count = route_count * route_segments * 2U;
    if (route_vertex_count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::length_error("PlanetMeshNode drainage preview is too large");
    }
    drainage_vertices_.resize(static_cast<std::int64_t>(route_vertex_count));
    drainage_texels_.resize(static_cast<std::int64_t>(route_vertex_count));
    drainage_colors_.resize(static_cast<std::int64_t>(route_vertex_count));
    std::int64_t route_vertex = 0;
    for (const auto& cell : mesh_->cells()) {
        const std::size_t index = cell.id.to_index();
        const std::uint32_t downstream = terrain_.downstream[index];
        if (downstream == no_downstream) {
            continue;
        }
        if (downstream >= cell_count) {
            throw std::logic_error("terrain snapshot has an invalid downstream cell");
        }
        const Vec3d first = cell.center_unit;
        const Vec3d second = mesh_->cells()[downstream].center_unit;
        const double first_radius = center_position[index].length() * overlay_lift;
        const double second_radius = center_position[downstream].length() * overlay_lift;
        const float weight = drainage_weight(terrain_.catchment_area_m2[index],
                                              terrain_.largest_catchment_area_m2);
        const godot::Color color =
            godot::Color{0.06F, 0.28F, 0.34F}.lerp({0.72F, 1.0F, 1.0F}, weight);
        for (std::size_t segment = 0; segment < route_segments; ++segment) {
            for (std::size_t endpoint = 0; endpoint < 2U; ++endpoint) {
                const double t =
                    static_cast<double>(segment + endpoint) / static_cast<double>(route_segments);
                const Vec3d direction = normalized(first * (1.0 - t) + second * t);
                const double radius = first_radius * (1.0 - t) + second_radius * t;
                drainage_vertices_.set(route_vertex, to_godot(direction * radius));
                drainage_texels_.set(route_vertex, texel_of(index));
                drainage_colors_.set(route_vertex++, color);
            }
        }
    }
}

// Base colours of the current view. Terrain interpolates elevation and land
// fraction to the corners; the categorical views stay flat per cell.
void PlanetMeshNode::build_colors() {
    const double sea_level_m = terrain_.sea_level_m;
    const auto terrain_color = [&](double elevation_m, double land_fraction) {
        return land_fraction >= 0.5 ? land_color(static_cast<float>(elevation_m - sea_level_m))
                                    : ocean_color(static_cast<float>(sea_level_m - elevation_m));
    };
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
            return godot::Color{1.0F, 1.0F, 1.0F};  // unused: the shader colours this view
        case view_drainage:
            return drainage_color(
                terrain_.land_fraction[index], terrain_.downstream[index],
                terrain_.depression_id[index],
                drainage_weight(terrain_.catchment_area_m2[index],
                                 terrain_.largest_catchment_area_m2));
        default:
            return terrain_color(terrain_.mean_elevation_m[index], terrain_.land_fraction[index]);
        }
    };
    std::vector<godot::Color> corner_color(mesh_->corner_count());
    for (std::size_t corner = 0; corner < corner_cells_.size(); ++corner) {
        double elevation = 0.0;
        double land = 0.0;
        for (const std::uint32_t cell : corner_cells_[corner]) {
            elevation += terrain_.mean_elevation_m[cell];
            land += terrain_.land_fraction[cell];
        }
        corner_color[corner] = terrain_color(elevation / 3.0, land / 3.0);
    }
    const bool smooth = view_mode_ == view_terrain;
    colors_.resize(vertices_.size());
    std::int64_t packed = 0;
    for (const auto& cell : mesh_->cells()) {
        const std::size_t index = cell.id.to_index();
        const godot::Color center = cell_color(index);
        const auto corners = mesh_->cell_corners(cell.id);
        for (std::size_t k = 0; k < corners.size(); ++k) {
            colors_.set(packed++, center);
            colors_.set(packed++, smooth ? corner_color[corners[k]] : center);
            colors_.set(packed++, smooth ? corner_color[corners[(k + 1U) % corners.size()]] : center);
        }
    }
}

void PlanetMeshNode::upload_mesh() {
    godot::Array arrays;
    arrays.resize(godot::Mesh::ARRAY_MAX);
    arrays[godot::Mesh::ARRAY_VERTEX] = vertices_;
    arrays[godot::Mesh::ARRAY_NORMAL] = normals_;
    arrays[godot::Mesh::ARRAY_COLOR] = colors_;
    arrays[godot::Mesh::ARRAY_TEX_UV] = texels_;
    if (rendered_mesh_.is_null()) {
        rendered_mesh_.instantiate();
        set_mesh(rendered_mesh_);
    }
    if (material_.is_null()) {
        material_.instantiate();
        set_material_override(material_);
    }
    rendered_mesh_->clear_surfaces();
    rendered_mesh_->add_surface_from_arrays(godot::Mesh::PRIMITIVE_TRIANGLES, arrays);
    if (view_mode_ == view_drainage) {
        godot::Array drainage_arrays;
        drainage_arrays.resize(godot::Mesh::ARRAY_MAX);
        drainage_arrays[godot::Mesh::ARRAY_VERTEX] = drainage_vertices_;
        drainage_arrays[godot::Mesh::ARRAY_COLOR] = drainage_colors_;
        drainage_arrays[godot::Mesh::ARRAY_TEX_UV] = drainage_texels_;
        rendered_mesh_->add_surface_from_arrays(godot::Mesh::PRIMITIVE_LINES, drainage_arrays);
    }
    apply_render_material();
}

void PlanetMeshNode::update_shader_flags() {
    if (material_.is_null()) {
        return;
    }
    material_->set_shader_parameter("day_night", day_night_shading_);
    if (view_mode_ != view_style) {
        material_->set_shader_parameter("insolation_view", view_mode_ == view_insolation);
        material_->set_shader_parameter("temperature_anomaly_view",
                                        view_mode_ == view_temperature_anomaly);
        material_->set_shader_parameter("wind_view", view_mode_ == view_wind);
        material_->set_shader_parameter("precipitation_view",
                                        view_mode_ == view_precipitation);
        material_->set_shader_parameter(
            "data_view", view_mode_ == view_insolation || view_mode_ == view_drainage ||
                             view_mode_ == view_temperature_anomaly || view_mode_ == view_wind ||
                             view_mode_ == view_precipitation);
    }
    if (wind_overlay_node_) {
        wind_overlay_node_->set_visible(view_mode_ == view_wind && wind_streak_count_ > 0);
    }
}

void PlanetMeshNode::refresh() {
    update_solar_forcing(*state_, parameters_, clock_.tick());
    set_visual_frame(presentation::make_visual_frame(make_state_snapshot(*state_, clock_), terrain_,
                                                     presentation_reference_),
                     false);
}

void PlanetMeshNode::build_preview_reference() {
    constexpr std::size_t samples = 48U;
    const SimulationTick restore_tick = clock_.tick();
    const double orbital_ticks =
        parameters_.orbital_period_s / static_cast<double>(simulation_seconds_per_tick);
    std::vector<StateSnapshot> frames;
    frames.reserve(samples);
    for (std::size_t sample = 0; sample < samples; ++sample) {
        const auto tick = static_cast<SimulationTick>(std::llround(
            orbital_ticks * static_cast<double>(sample) / static_cast<double>(samples)));
        update_solar_forcing(*state_, parameters_, tick);
        SimulationClock sample_clock;
        sample_clock.set_tick(tick);
        StateSnapshot frame = make_state_snapshot(*state_, sample_clock);
        frame.surface_temperature_K.clear();
        frame.land_snow_water_equivalent_kg_m2.clear();
        frame.sea_ice_mass_kg_m2.clear();
        frame.climatology_surface_temperature_mean_K.clear();
        frame.climatology_surface_temperature_variance_K2.clear();
        frame.sea_level_pressure_Pa.clear();
        frame.surface_eastward_wind_m_s.clear();
        frame.surface_northward_wind_m_s.clear();
        frames.push_back(std::move(frame));
    }
    presentation_reference_ = presentation::make_presentation_reference(frames);
    update_solar_forcing(*state_, parameters_, restore_tick);
}

void PlanetMeshNode::load_styles() {
    static constexpr std::array<const char*, 2> paths{{
        "res://styles/stylised/style.tres",
        "res://styles/map/style.tres",
    }};
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const godot::Ref<godot::Resource> resource =
            godot::ResourceLoader::get_singleton()->load(paths[index]);
        godot::Ref<PlanetStyle> style = resource;
        const godot::String error = validate_style(style);
        if (!error.is_empty()) {
            throw std::runtime_error(std::string(error.utf8().get_data()));
        }
        styles_[index] = style;
    }
}

godot::String PlanetMeshNode::validate_style(const godot::Ref<PlanetStyle>& style) const {
    if (style.is_null())
        return "style resource did not load as PlanetStyle";
    if (style->get_style_name().is_empty())
        return "style has no name";
    if (style->get_channel_set_version() !=
        static_cast<std::int64_t>(presentation::channel_set_version)) {
        return godot::String("style ") + style->get_style_name() +
               " has incompatible channel_set_version";
    }
    const godot::PackedStringArray implemented = style->get_required_channels();
    for (const auto& descriptor : presentation::channel_registry) {
        if (!descriptor.required_in_style)
            continue;
        const godot::String name{descriptor.name.data()};
        if (!implemented.has(name)) {
            return godot::String("style ") + style->get_style_name() +
                   " is missing required channel " + name;
        }
    }
    if (style->get_surface_shader().is_null()) {
        return godot::String("style ") + style->get_style_name() + " has no surface shader";
    }
    return {};
}

void PlanetMeshNode::apply_render_material() {
    if (material_.is_null())
        return;
    if (view_mode_ == view_style) {
        if (styles_[active_style_].is_null())
            return;
        material_->set_shader(styles_[active_style_]->get_surface_shader());
    } else {
        if (overlay_shader_.is_null()) {
            const godot::Ref<godot::Resource> resource =
                godot::ResourceLoader::get_singleton()->load(
                    "res://shaders/climate_lab_overlay.gdshader");
            overlay_shader_ = resource;
            if (overlay_shader_.is_null()) {
                throw std::runtime_error("shared climate-lab overlay shader did not load");
            }
        }
        material_->set_shader(overlay_shader_);
    }
    bind_channel_textures();
    update_shader_flags();
}

void PlanetMeshNode::set_visual_frame(presentation::VisualFrame frame, bool reset_smoothing) {
    if (frame.channel_set_version != presentation::channel_set_version) {
        throw std::runtime_error("VisualFrame channel-set version does not match the bridge");
    }
    if (reset_smoothing ||
        displayed_frame_.channels[channel_index(presentation::ChannelId::relief)].values.empty()) {
        target_frame_ = frame;
        displayed_frame_ = std::move(frame);
    } else {
        target_frame_ = std::move(frame);
        displayed_frame_.tick = target_frame_.tick;
        displayed_frame_.events = target_frame_.events;
        for (std::size_t index = 0; index < presentation::channel_count; ++index) {
            const auto id = static_cast<presentation::ChannelId>(index + 1U);
            if (id != presentation::ChannelId::snow_cover &&
                id != presentation::ChannelId::sea_ice) {
                displayed_frame_.channels[index] = target_frame_.channels[index];
            }
        }
    }
    upload_changed_channels();
}

void PlanetMeshNode::upload_changed_channels() {
    if (!mesh_ || texture_width_ <= 0 || texture_height_ <= 0)
        return;
    upload_surface_class();
    upload_scalar_channel(presentation::ChannelId::relief);
    upload_scalar_channel(presentation::ChannelId::daylight);
    upload_scalar_channel(presentation::ChannelId::snow_cover);
    upload_scalar_channel(presentation::ChannelId::sea_ice);
    upload_scalar_channel(presentation::ChannelId::temperature_anomaly);
    upload_scalar_channel(presentation::ChannelId::atmosphere_density);
    upload_scalar_channel(presentation::ChannelId::precipitation);
    update_wind_overlay();
    upload_scalar_channel(presentation::ChannelId::known, 1.0F);
    upload_scalar_channel(presentation::ChannelId::guessed);
    upload_scalar_channel(presentation::ChannelId::knowledge_age);
    bind_channel_textures();
}

void PlanetMeshNode::upload_scalar_channel(presentation::ChannelId id, float absent_value) {
    const std::size_t index = channel_index(id);
    const auto& source = displayed_frame_.channels[index].values;
    if (channel_textures_[index].is_valid() && uploaded_channels_[index] == source)
        return;
    const std::size_t cells = mesh_->cell_count();
    if (!source.empty() && source.size() != cells) {
        throw std::logic_error("scalar presentation channel does not match the mesh");
    }
    const std::int64_t pixel_count = static_cast<std::int64_t>(texture_width_) * texture_height_;
    godot::PackedFloat32Array texels;
    texels.resize(pixel_count);
    for (std::size_t cell = 0; cell < cells; ++cell) {
        texels.set(static_cast<std::int64_t>(cell), source.empty() ? absent_value : source[cell]);
    }
    for (std::size_t corner = 0; corner < corner_cells_.size(); ++corner) {
        float sum = 0.0F;
        for (const std::uint32_t cell : corner_cells_[corner]) {
            sum += source.empty() ? absent_value : source[cell];
        }
        texels.set(static_cast<std::int64_t>(cells + corner), sum / 3.0F);
    }
    const godot::Ref<godot::Image> image = godot::Image::create_from_data(
        texture_width_, texture_height_, false, godot::Image::FORMAT_RF, texels.to_byte_array());
    if (channel_textures_[index].is_null()) {
        channel_textures_[index] = godot::ImageTexture::create_from_image(image);
    } else {
        channel_textures_[index]->update(image);
    }
    uploaded_channels_[index] = source;
}

void PlanetMeshNode::upload_surface_class() {
    const std::size_t index = channel_index(presentation::ChannelId::surface_class);
    const auto& source = displayed_frame_.channels[index].values;
    if (channel_textures_[index].is_valid() && surface_class_ice_texture_.is_valid() &&
        uploaded_channels_[index] == source) {
        return;
    }
    const std::size_t cells = mesh_->cell_count();
    if (source.size() != cells * presentation::surface_class_weight_count) {
        throw std::logic_error("surface_class presentation channel does not match the mesh");
    }
    const std::int64_t pixel_count = static_cast<std::int64_t>(texture_width_) * texture_height_;
    godot::PackedFloat32Array first;
    godot::PackedFloat32Array second;
    first.resize(pixel_count * 4);
    second.resize(pixel_count * 4);
    const auto set_weights = [&](std::size_t pixel, const std::array<float, 5>& weights) {
        const std::int64_t offset = static_cast<std::int64_t>(pixel) * 4;
        for (std::int64_t component = 0; component < 4; ++component) {
            first.set(offset + component, weights[static_cast<std::size_t>(component)]);
        }
        second.set(offset, weights[4]);
    };
    for (std::size_t cell = 0; cell < cells; ++cell) {
        std::array<float, 5> weights{};
        for (std::size_t component = 0; component < weights.size(); ++component) {
            weights[component] = source[cell * weights.size() + component];
        }
        set_weights(cell, weights);
    }
    for (std::size_t corner = 0; corner < corner_cells_.size(); ++corner) {
        std::array<float, 5> weights{};
        for (const std::uint32_t cell : corner_cells_[corner]) {
            for (std::size_t component = 0; component < weights.size(); ++component) {
                weights[component] += source[cell * weights.size() + component] / 3.0F;
            }
        }
        set_weights(cells + corner, weights);
    }
    const godot::Ref<godot::Image> first_image = godot::Image::create_from_data(
        texture_width_, texture_height_, false, godot::Image::FORMAT_RGBAF, first.to_byte_array());
    const godot::Ref<godot::Image> second_image = godot::Image::create_from_data(
        texture_width_, texture_height_, false, godot::Image::FORMAT_RGBAF, second.to_byte_array());
    if (channel_textures_[index].is_null()) {
        channel_textures_[index] = godot::ImageTexture::create_from_image(first_image);
    } else {
        channel_textures_[index]->update(first_image);
    }
    if (surface_class_ice_texture_.is_null()) {
        surface_class_ice_texture_ = godot::ImageTexture::create_from_image(second_image);
    } else {
        surface_class_ice_texture_->update(second_image);
    }
    uploaded_channels_[index] = source;
}

void PlanetMeshNode::ensure_wind_overlay() {
    if (wind_overlay_node_)
        return;
    wind_overlay_node_ = memnew(godot::MeshInstance3D);
    wind_overlay_node_->set_name("WindParticles");
    add_child(wind_overlay_node_);
    wind_overlay_mesh_.instantiate();
    wind_overlay_node_->set_mesh(wind_overlay_mesh_);
    wind_overlay_material_.instantiate();
    const godot::Ref<godot::Resource> resource =
        godot::ResourceLoader::get_singleton()->load("res://shaders/wind_particles.gdshader");
    const godot::Ref<godot::Shader> shader = resource;
    if (shader.is_null()) {
        throw std::runtime_error("wind particle shader did not load");
    }
    wind_overlay_material_->set_shader(shader);
    wind_overlay_node_->set_material_override(wind_overlay_material_);
    wind_overlay_node_->set_cast_shadows_setting(godot::GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
}

void PlanetMeshNode::update_wind_overlay() {
    const std::size_t channel = channel_index(presentation::ChannelId::wind);
    const auto& source = displayed_frame_.channels[channel].values;
    if (uploaded_channels_[channel] == source && wind_overlay_node_)
        return;
    ensure_wind_overlay();
    wind_overlay_mesh_->clear_surfaces();
    wind_streak_count_ = 0;
    const std::size_t cells = mesh_->cell_count();
    if (source.empty()) {
        uploaded_channels_[channel] = source;
        wind_overlay_node_->set_visible(false);
        return;
    }
    if (source.size() != cells * presentation::wind_vector_component_count) {
        throw std::logic_error("wind presentation channel does not match the mesh");
    }

    constexpr std::size_t maximum_streaks = 2'048U;
    const std::size_t stride = std::max<std::size_t>(1U, (cells + maximum_streaks - 1U) /
                                                            maximum_streaks);
    godot::PackedVector3Array vertices;
    godot::PackedVector2Array uv;
    godot::PackedColorArray colors;
    for (std::size_t cell = 0; cell < cells; cell += stride) {
        const std::size_t offset = cell * presentation::wind_vector_component_count;
        const double east = source[offset];
        const double north = source[offset + 1U];
        const double speed = std::hypot(east, north);
        if (!(speed > 0.1))
            continue;
        const auto& geometry = mesh_->cells()[cell];
        const Vec3d direction =
            normalized(geometry.east_unit * east + geometry.north_unit * north);
        const godot::Vector3 normal = to_godot(geometry.center_unit);
        const godot::Vector3 tangent = to_godot(direction);
        const godot::Vector3 side = normal.cross(tangent).normalized() * 0.0025F;
        const float strength = static_cast<float>(std::clamp(speed / 60.0, 0.0, 1.0));
        const godot::Vector3 centre = normal * 1.006F;
        const godot::Vector3 half = tangent * (0.009F + 0.020F * strength);
        const godot::Vector3 begin = centre - half;
        const godot::Vector3 end = centre + half;
        const float seed = std::fmod(static_cast<float>(cell) * 0.61803398875F, 1.0F);
        const godot::Color color =
            godot::Color{0.18F, 0.72F, 1.0F, seed}.lerp({1.0F, 0.74F, 0.12F, seed}, strength);
        for (const auto& [position, coordinate] :
             std::array<std::pair<godot::Vector3, godot::Vector2>, 6>{{
                 {begin - side, {0.0F, -1.0F}}, {end - side, {1.0F, -1.0F}},
                 {end + side, {1.0F, 1.0F}},    {begin - side, {0.0F, -1.0F}},
                 {end + side, {1.0F, 1.0F}},    {begin + side, {0.0F, 1.0F}},
             }}) {
            vertices.push_back(position);
            uv.push_back(coordinate);
            colors.push_back(color);
        }
        ++wind_streak_count_;
    }
    if (!vertices.is_empty()) {
        godot::Array arrays;
        arrays.resize(godot::Mesh::ARRAY_MAX);
        arrays[godot::Mesh::ARRAY_VERTEX] = vertices;
        arrays[godot::Mesh::ARRAY_TEX_UV] = uv;
        arrays[godot::Mesh::ARRAY_COLOR] = colors;
        wind_overlay_mesh_->add_surface_from_arrays(godot::Mesh::PRIMITIVE_TRIANGLES, arrays);
    }
    uploaded_channels_[channel] = source;
    wind_overlay_node_->set_visible(view_mode_ == view_wind && wind_streak_count_ > 0);
}

void PlanetMeshNode::bind_channel_textures() {
    if (material_.is_null())
        return;
    const auto bind = [&](const char* parameter, presentation::ChannelId id) {
        const auto& texture = channel_textures_[channel_index(id)];
        if (texture.is_valid())
            material_->set_shader_parameter(parameter, texture);
    };
    bind("relief_map", presentation::ChannelId::relief);
    bind("surface_class_map", presentation::ChannelId::surface_class);
    if (surface_class_ice_texture_.is_valid()) {
        material_->set_shader_parameter("surface_class_ice_map", surface_class_ice_texture_);
    }
    bind("daylight_map", presentation::ChannelId::daylight);
    bind("snow_cover_map", presentation::ChannelId::snow_cover);
    bind("sea_ice_map", presentation::ChannelId::sea_ice);
    bind("temperature_anomaly_map", presentation::ChannelId::temperature_anomaly);
    bind("atmosphere_density_map", presentation::ChannelId::atmosphere_density);
    bind("precipitation_map", presentation::ChannelId::precipitation);
    bind("known_map", presentation::ChannelId::known);
    bind("guessed_map", presentation::ChannelId::guessed);
    bind("knowledge_age_map", presentation::ChannelId::knowledge_age);
}

}  // namespace planetsim::godot_bridge
