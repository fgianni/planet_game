shader_type spatial;
render_mode cull_disabled;

#include "res://shaders/surface_detail.gdshaderinc"
#include "res://shaders/display_transfer.gdshaderinc"

uniform sampler2D relief_map : filter_nearest;
uniform sampler2D surface_class_map : filter_nearest;
uniform sampler2D surface_class_ice_map : filter_nearest;
uniform sampler2D daylight_map : filter_nearest;
uniform sampler2D snow_cover_map : filter_nearest;
uniform sampler2D sea_ice_map : filter_nearest;
uniform sampler2D atmosphere_density_map : filter_nearest;
uniform sampler2D known_map : filter_nearest;
uniform sampler2D guessed_map : filter_nearest;
uniform sampler2D knowledge_age_map : filter_nearest;
uniform bool day_night = true;

varying vec2 cell_texel;
varying vec2 first_corner_texel;
varying vec4 second_corner_and_weights;
varying vec3 model_position;

#define CHANNEL(map) detail_fetch(map, cell_texel, first_corner_texel, second_corner_and_weights)

vec3 srgb_to_linear(vec3 c) {
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void vertex() {
	cell_texel = UV;
	first_corner_texel = UV2;
	second_corner_and_weights = CUSTOM0;
	model_position = detail_planet_position(VERTEX, CUSTOM1);
}

void fragment() {
	vec4 classes = CHANNEL(surface_class_map);
	float ice_weight = CHANNEL(surface_class_ice_map).r;
	float relief = CHANNEL(relief_map).r;
	float daylight = clamp(CHANNEL(daylight_map).r, 0.0, 1.0);
	float snow = clamp(CHANNEL(snow_cover_map).r, 0.0, 1.0);
	float sea_ice = clamp(CHANNEL(sea_ice_map).r, 0.0, 1.0);
	float atmosphere = clamp(CHANNEL(atmosphere_density_map).r, 0.0, 1.0);
	float known = clamp(CHANNEL(known_map).r, 0.0, 1.0);
	float guessed = clamp(CHANNEL(guessed_map).r, 0.0, 1.0);
	float age = clamp(CHANNEL(knowledge_age_map).r, 0.0, 1.0);

	// Sub-cell detail: none at planet scale, coverages at region scale.
	vec3 direction = normalize(model_position);
	float amount = detail_amount(direction);
	float open_water = detail_cover(classes.r, detail_uniform(direction, detail_seed_coast), amount);
	vec4 cover = detail_partition(vec4(classes.g, classes.b, classes.a, ice_weight),
	                              detail_uniform(direction, detail_seed_land_mix), amount);
	float snow_patch = detail_cover(snow, detail_uniform(direction, detail_seed_snow), amount);
	float floe = detail_cover(sea_ice, detail_uniform(direction, detail_seed_sea_ice), amount);
	float grain = detail_field(direction, detail_seed_texture);

	float highland = smoothstep(500.0, 4200.0, relief);
	vec3 water = mix(vec3(0.03, 0.11, 0.27), vec3(0.08, 0.31, 0.54), smoothstep(-6000.0, 0.0, relief));
	vec3 rock = mix(vec3(0.36, 0.30, 0.28), vec3(0.68, 0.65, 0.60), highland);
	vec3 sand = vec3(0.78, 0.64, 0.34);
	vec3 soil = mix(vec3(0.25, 0.54, 0.20), vec3(0.52, 0.43, 0.20), highland);
	vec3 ice = vec3(0.84, 0.94, 1.0);
	vec3 ground = cover.x * rock + cover.y * sand + cover.z * soil + cover.w * ice;
	vec3 base = mix(ground, water, open_water);
	// At region scale snow lies on land and floes on water, at full strength.
	float snow_tint = mix(0.22, 0.85, amount) * snow_patch * mix(1.0, 1.0 - open_water, amount);
	float floe_tint = mix(0.18, 0.85, amount) * floe * mix(1.0, open_water, amount);
	base = mix(base, ice, clamp(snow_tint + floe_tint, 0.0, 1.0));
	base *= 1.0 + amount * 0.24 * (grain - 0.47);
	base = mix(base, vec3(0.49, 0.51, 0.53), 0.42 * guessed + 0.28 * age);
	base *= mix(0.42, 1.0, known);
	// Day/night: the bridge's sun light shades the day side towards the
	// terminator; daylight keeps the night side dark under the fill light.
	if (day_night) {
		base *= mix(0.25, 1.0, smoothstep(0.0, 0.12, daylight));
	}

	// Fine relief on land at region scale (surface-gradient bump).
	float bump = amount * (1.0 - open_water) * (0.002 + 0.006 * highland) * grain;
	vec3 dpdx = dFdx(VERTEX);
	vec3 dpdy = dFdy(VERTEX);
	vec3 r1 = cross(dpdy, NORMAL);
	vec3 r2 = cross(NORMAL, dpdx);
	float det = dot(dpdx, r1);
	vec3 gradient = (dFdx(bump) * r1 + dFdy(bump) * r2) / max(abs(det), 1e-12) * sign(det);
	NORMAL = normalize(NORMAL - gradient * 8.0);

	float air_mass = pow(1.0 - clamp(dot(normalize(NORMAL), normalize(VIEW)), 0.0, 1.0), 3.0);
	base = mix(base, vec3(0.18, 0.55, 0.96), 0.38 * atmosphere * air_mass);
	if (!OUTPUT_IS_SRGB) {
		base = srgb_to_linear(base);
	} else {
		base = display_albedo(base);
	}
	ALBEDO = base;
	ROUGHNESS = mix(0.88, 0.6, amount * open_water * (1.0 - floe));
}
