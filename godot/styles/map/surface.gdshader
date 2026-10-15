shader_type spatial;
render_mode unshaded, cull_disabled;

#include "res://shaders/surface_detail.gdshaderinc"
#include "res://shaders/display_transfer.gdshaderinc"
#include "res://shaders/view_transition.gdshaderinc"

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
varying vec2 tile_surface; // (1, height above sea) on a terrain tile, (0, 0) on the cell mesh

#define CHANNEL(map) detail_fetch(map, cell_texel, first_corner_texel, second_corner_and_weights)

vec3 srgb_to_linear(vec3 c) {
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void vertex() {
	cell_texel = UV;
	first_corner_texel = UV2;
	second_corner_and_weights = CUSTOM0;
	view_relief(VERTEX, NORMAL, CUSTOM1);
	model_position = detail_planet_position(VERTEX, CUSTOM1);
	tile_surface = detail_tile_surface(CUSTOM1);
}

void fragment() {
	if (view_faded_out(tile_surface.x, FRAGCOORD.xy)) {
		discard;
	}
	vec4 classes = CHANNEL(surface_class_map);
	float ice_weight = CHANNEL(surface_class_ice_map).r;
	// On a terrain tile, the drawn surface's own height (sub-cell relief).
	float relief = mix(CHANNEL(relief_map).r, tile_surface.y, tile_surface.x);
	float daylight = clamp(CHANNEL(daylight_map).r, 0.0, 1.0);
	float snow = clamp(CHANNEL(snow_cover_map).r, 0.0, 1.0);
	float sea_ice = clamp(CHANNEL(sea_ice_map).r, 0.0, 1.0);
	float atmosphere = clamp(CHANNEL(atmosphere_density_map).r, 0.0, 1.0);
	float known = clamp(CHANNEL(known_map).r, 0.0, 1.0);
	float guessed = clamp(CHANNEL(guessed_map).r, 0.0, 1.0);
	float age = clamp(CHANNEL(knowledge_age_map).r, 0.0, 1.0);

	// Sub-cell detail: none at planet scale; at region scale flat symbolic
	// patches with fractal coasts, no texture.
	vec3 direction = normalize(model_position);
	float amount = detail_amount(direction);
	// On a terrain tile the coast follows the drawn surface.
	float open_water = mix(detail_cover(classes.r, detail_uniform(direction, detail_seed_coast), amount),
	                       detail_geometry_water(tile_surface.y), tile_surface.x);
	float land_ice = mix(ice_weight, detail_land_ice(ice_weight, classes.r, sea_ice), tile_surface.x);
	vec4 cover = detail_partition(vec4(classes.g, classes.b, classes.a, land_ice),
	                              detail_uniform(direction, detail_seed_land_mix), amount);
	float snow_patch = detail_cover(snow, detail_uniform(direction, detail_seed_snow), amount);
	float floe = detail_cover(sea_ice, detail_uniform(direction, detail_seed_sea_ice), amount);

	float contour = 0.5 + 0.5 * cos(relief * 0.0062831853);
	vec3 water = vec3(0.14, 0.29, 0.40);
	vec3 rock = vec3(0.48, 0.45, 0.42);
	vec3 sand = vec3(0.88, 0.78, 0.53);
	vec3 soil = vec3(0.55, 0.62, 0.25);
	vec3 ice = vec3(0.95, 0.97, 0.96);
	vec3 ground = cover.x * rock + cover.y * sand + cover.z * soil + cover.w * ice;
	vec3 base = mix(ground, water, open_water);
	base *= mix(0.93, 1.05, smoothstep(0.42, 0.58, contour));
	float snow_tint = mix(0.28, 0.85, amount) * snow_patch * mix(1.0, 1.0 - open_water, amount);
	float floe_tint = mix(0.20, 0.85, amount) * floe * mix(1.0, open_water, amount);
	base = mix(base, ice, clamp(snow_tint + floe_tint, 0.0, 1.0));
	base = mix(base, vec3(0.74, 0.70, 0.61), 0.55 * guessed);
	base = mix(base, vec3(dot(base, vec3(0.299, 0.587, 0.114))), 0.55 * age);
	base *= mix(0.52, 1.0, known);
	if (day_night) {
		base *= 0.32 + 0.68 * daylight;
	}
	float air_mass = pow(1.0 - clamp(dot(normalize(NORMAL), normalize(VIEW)), 0.0, 1.0), 3.0);
	base = mix(base, vec3(0.30, 0.63, 0.90), 0.22 * atmosphere * air_mass);
	if (!OUTPUT_IS_SRGB) {
		base = srgb_to_linear(base);
	} else {
		base = display_unshaded(base);
	}
	ALBEDO = base;
}
