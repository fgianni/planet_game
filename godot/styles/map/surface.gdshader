shader_type spatial;
render_mode unshaded, cull_disabled;

uniform sampler2D relief_map : filter_nearest;
uniform sampler2D surface_class_map : filter_nearest;
uniform sampler2D surface_class_ice_map : filter_nearest;
uniform sampler2D daylight_map : filter_nearest;
uniform sampler2D snow_cover_map : filter_nearest;
uniform sampler2D sea_ice_map : filter_nearest;
uniform sampler2D known_map : filter_nearest;
uniform sampler2D guessed_map : filter_nearest;
uniform sampler2D knowledge_age_map : filter_nearest;
uniform bool day_night = true;

varying vec2 channel_texel;

vec3 srgb_to_linear(vec3 c) {
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void vertex() {
	channel_texel = UV;
}

void fragment() {
	ivec2 p = ivec2(channel_texel + 0.5);
	vec4 classes = texelFetch(surface_class_map, p, 0);
	float ice_weight = texelFetch(surface_class_ice_map, p, 0).r;
	float relief = texelFetch(relief_map, p, 0).r;
	float daylight = clamp(texelFetch(daylight_map, p, 0).r, 0.0, 1.0);
	float snow = clamp(texelFetch(snow_cover_map, p, 0).r, 0.0, 1.0);
	float sea_ice = clamp(texelFetch(sea_ice_map, p, 0).r, 0.0, 1.0);
	float known = clamp(texelFetch(known_map, p, 0).r, 0.0, 1.0);
	float guessed = clamp(texelFetch(guessed_map, p, 0).r, 0.0, 1.0);
	float age = clamp(texelFetch(knowledge_age_map, p, 0).r, 0.0, 1.0);

	float contour = 0.5 + 0.5 * cos(relief * 0.0062831853);
	vec3 water = vec3(0.53, 0.72, 0.78);
	vec3 rock = vec3(0.48, 0.45, 0.42);
	vec3 sand = vec3(0.88, 0.78, 0.53);
	vec3 soil = vec3(0.69, 0.74, 0.50);
	vec3 ice = vec3(0.95, 0.97, 0.96);
	vec3 base = classes.r * water + classes.g * rock + classes.b * sand + classes.a * soil + ice_weight * ice;
	base *= mix(0.93, 1.05, smoothstep(0.42, 0.58, contour));
	base = mix(base, ice, 0.16 * snow + 0.12 * sea_ice);
	base = mix(base, vec3(0.74, 0.70, 0.61), 0.55 * guessed);
	base = mix(base, vec3(dot(base, vec3(0.299, 0.587, 0.114))), 0.55 * age);
	base *= mix(0.52, 1.0, known);
	if (day_night) {
		base *= 0.32 + 0.68 * daylight;
	}
	if (!OUTPUT_IS_SRGB) {
		base = srgb_to_linear(base);
	}
	ALBEDO = base;
}
