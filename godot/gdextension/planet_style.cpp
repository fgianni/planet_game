#include "planet_style.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace planetsim::godot_bridge {

void PlanetStyle::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("set_style_name", "value"),
                                &PlanetStyle::set_style_name);
    godot::ClassDB::bind_method(godot::D_METHOD("get_style_name"), &PlanetStyle::get_style_name);
    godot::ClassDB::bind_method(godot::D_METHOD("set_version", "value"), &PlanetStyle::set_version);
    godot::ClassDB::bind_method(godot::D_METHOD("get_version"), &PlanetStyle::get_version);
    godot::ClassDB::bind_method(godot::D_METHOD("set_author", "value"), &PlanetStyle::set_author);
    godot::ClassDB::bind_method(godot::D_METHOD("get_author"), &PlanetStyle::get_author);
    godot::ClassDB::bind_method(godot::D_METHOD("set_license", "value"), &PlanetStyle::set_license);
    godot::ClassDB::bind_method(godot::D_METHOD("get_license"), &PlanetStyle::get_license);
    godot::ClassDB::bind_method(godot::D_METHOD("set_channel_set_version", "value"),
                                &PlanetStyle::set_channel_set_version);
    godot::ClassDB::bind_method(godot::D_METHOD("get_channel_set_version"),
                                &PlanetStyle::get_channel_set_version);
    godot::ClassDB::bind_method(godot::D_METHOD("set_required_channels", "value"),
                                &PlanetStyle::set_required_channels);
    godot::ClassDB::bind_method(godot::D_METHOD("get_required_channels"),
                                &PlanetStyle::get_required_channels);
    godot::ClassDB::bind_method(godot::D_METHOD("set_surface_shader", "value"),
                                &PlanetStyle::set_surface_shader);
    godot::ClassDB::bind_method(godot::D_METHOD("get_surface_shader"),
                                &PlanetStyle::get_surface_shader);
    godot::ClassDB::bind_method(godot::D_METHOD("set_cost_tier", "value"),
                                &PlanetStyle::set_cost_tier);
    godot::ClassDB::bind_method(godot::D_METHOD("get_cost_tier"), &PlanetStyle::get_cost_tier);

    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "style_name"), "set_style_name",
                 "get_style_name");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::INT, "version"), "set_version", "get_version");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "author"), "set_author", "get_author");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "license"), "set_license",
                 "get_license");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::INT, "channel_set_version"),
                 "set_channel_set_version", "get_channel_set_version");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::PACKED_STRING_ARRAY, "required_channels"),
                 "set_required_channels", "get_required_channels");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::OBJECT, "surface_shader",
                                     godot::PROPERTY_HINT_RESOURCE_TYPE, "Shader"),
                 "set_surface_shader", "get_surface_shader");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "cost_tier"), "set_cost_tier",
                 "get_cost_tier");
}

void PlanetStyle::set_style_name(const godot::String& value) { style_name_ = value; }
godot::String PlanetStyle::get_style_name() const { return style_name_; }
void PlanetStyle::set_version(std::int64_t value) { version_ = value; }
std::int64_t PlanetStyle::get_version() const noexcept { return version_; }
void PlanetStyle::set_author(const godot::String& value) { author_ = value; }
godot::String PlanetStyle::get_author() const { return author_; }
void PlanetStyle::set_license(const godot::String& value) { license_ = value; }
godot::String PlanetStyle::get_license() const { return license_; }
void PlanetStyle::set_channel_set_version(std::int64_t value) { channel_set_version_ = value; }
std::int64_t PlanetStyle::get_channel_set_version() const noexcept { return channel_set_version_; }
void PlanetStyle::set_required_channels(const godot::PackedStringArray& value) {
    required_channels_ = value;
}
godot::PackedStringArray PlanetStyle::get_required_channels() const { return required_channels_; }
void PlanetStyle::set_surface_shader(const godot::Ref<godot::Shader>& value) {
    surface_shader_ = value;
}
godot::Ref<godot::Shader> PlanetStyle::get_surface_shader() const { return surface_shader_; }
void PlanetStyle::set_cost_tier(const godot::String& value) { cost_tier_ = value; }
godot::String PlanetStyle::get_cost_tier() const { return cost_tier_; }

}  // namespace planetsim::godot_bridge
