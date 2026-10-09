#pragma once

#include "sim/core/scheduler/simulation_clock.hpp"
#include "sim/planet/planet_parameters.hpp"
#include "sim/planet/terrain/terrain_snapshot.hpp"
#include "sim/presentation/visual_frame.hpp"

#include "planet_style.hpp"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
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
//
// Geometry and base colours are built once per planet, view or relief change.
// Each tick only the per-cell insolation from the snapshot is uploaded, as a
// small float texture the shader looks up (cells first, then corners, one
// texel each), so large meshes stay cheap to animate.
class PlanetMeshNode : public godot::MeshInstance3D {
    GDCLASS(PlanetMeshNode, godot::MeshInstance3D)

  protected:
    static void _bind_methods();

  public:
    enum ViewMode : std::int64_t {
        view_style = 0,
        view_terrain = 1,
        view_plates = 2,
        view_crust_age = 3,
        view_insolation = 4,
        view_drainage = 5,
        view_mode_count = 6,
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
    void set_style(const godot::String& style_name);
    void next_style();
    [[nodiscard]] godot::String get_style() const;
    [[nodiscard]] godot::PackedStringArray get_available_styles() const;
    [[nodiscard]] godot::String validate_style_manifest(const godot::String& path) const;
    void load_presentation_record(const godot::String& path);
    void clear_presentation_record();
    [[nodiscard]] bool has_presentation_record() const noexcept;
    [[nodiscard]] std::int64_t get_presentation_frame_count() const noexcept;
    [[nodiscard]] std::int64_t get_presentation_frame() const noexcept;
    void set_presentation_frame(std::int64_t frame);
    void advance_presentation_frame();
    void advance_presentation(double wall_seconds, double simulated_years_per_wall_second);
    [[nodiscard]] std::int64_t get_geometry_revision() const noexcept;

    [[nodiscard]] std::int64_t get_seed() const noexcept;
    [[nodiscard]] std::int64_t get_subdivision() const noexcept;
    [[nodiscard]] godot::String get_preset() const;
    [[nodiscard]] double get_sea_level() const noexcept;
    [[nodiscard]] double get_land_fraction() const noexcept;
    [[nodiscard]] std::int64_t get_plate_count() const noexcept;
    [[nodiscard]] std::int64_t get_drainage_outlet_count() const noexcept;
    [[nodiscard]] std::int64_t get_drainage_basin_count() const noexcept;
    [[nodiscard]] std::int64_t get_drainage_depression_count() const noexcept;

  private:
    void build_geometry();
    void build_colors();
    void upload_mesh();
    void refresh();
    void build_preview_reference();
    void load_styles();
    [[nodiscard]] godot::String validate_style(const godot::Ref<PlanetStyle>& style) const;
    void apply_render_material();
    void set_visual_frame(presentation::VisualFrame frame, bool reset_smoothing);
    void upload_changed_channels();
    void upload_scalar_channel(presentation::ChannelId id, float absent_value = 0.0F);
    void upload_surface_class();
    void bind_channel_textures();
    void update_shader_flags();

    PlanetParameters parameters_ = PlanetParameters::earth_development();
    SimulationClock clock_;
    std::shared_ptr<const PlanetMesh> mesh_;
    std::unique_ptr<PlanetState> state_;
    TerrainSnapshot terrain_;
    std::vector<std::array<std::uint32_t, 3>> corner_cells_;
    std::uint64_t seed_ = 1;
    godot::String preset_ = "earth_like";

    std::int64_t view_mode_ = view_style;
    double relief_exaggeration_ = 25.0;
    bool day_night_shading_ = true;

    godot::PackedVector3Array vertices_;
    godot::PackedVector3Array normals_;
    godot::PackedVector2Array texels_;   // channel texel of each vertex
    godot::PackedColorArray colors_;
    godot::PackedVector3Array drainage_vertices_;
    godot::PackedVector2Array drainage_texels_;
    godot::PackedColorArray drainage_colors_;
    std::int32_t texture_width_ = 0;
    std::int32_t texture_height_ = 0;
    godot::Ref<godot::ArrayMesh> rendered_mesh_;
    godot::Ref<godot::ShaderMaterial> material_;
    godot::Ref<godot::Shader> overlay_shader_;
    std::array<godot::Ref<PlanetStyle>, 2> styles_;
    std::size_t active_style_ = 0U;
    presentation::PresentationReference presentation_reference_;
    presentation::VisualFrame displayed_frame_;
    presentation::VisualFrame target_frame_;
    std::vector<StateSnapshot> presentation_frames_;
    std::size_t presentation_frame_ = 0U;
    std::array<godot::Ref<godot::ImageTexture>, presentation::channel_count> channel_textures_;
    godot::Ref<godot::ImageTexture> surface_class_ice_texture_;
    std::array<std::vector<float>, presentation::channel_count> uploaded_channels_;
    std::int64_t geometry_revision_ = 0;
};

}  // namespace planetsim::godot_bridge
