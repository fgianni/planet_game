#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>

namespace planetsim::godot_bridge {

// Data-only style-pack manifest. The resource points at shader assets and
// declares the semantic-channel contract it implements; it contains no code.
class PlanetStyle : public godot::Resource {
    GDCLASS(PlanetStyle, godot::Resource)

  protected:
    static void _bind_methods();

  public:
    void set_style_name(const godot::String& value);
    [[nodiscard]] godot::String get_style_name() const;
    void set_version(std::int64_t value);
    [[nodiscard]] std::int64_t get_version() const noexcept;
    void set_author(const godot::String& value);
    [[nodiscard]] godot::String get_author() const;
    void set_license(const godot::String& value);
    [[nodiscard]] godot::String get_license() const;
    void set_channel_set_version(std::int64_t value);
    [[nodiscard]] std::int64_t get_channel_set_version() const noexcept;
    void set_required_channels(const godot::PackedStringArray& value);
    [[nodiscard]] godot::PackedStringArray get_required_channels() const;
    void set_surface_shader(const godot::Ref<godot::Shader>& value);
    [[nodiscard]] godot::Ref<godot::Shader> get_surface_shader() const;
    void set_cost_tier(const godot::String& value);
    [[nodiscard]] godot::String get_cost_tier() const;

  private:
    godot::String style_name_;
    std::int64_t version_ = 1;
    godot::String author_;
    godot::String license_;
    std::int64_t channel_set_version_ = 0;
    godot::PackedStringArray required_channels_;
    godot::Ref<godot::Shader> surface_shader_;
    godot::String cost_tier_ = "medium";
};

}  // namespace planetsim::godot_bridge
