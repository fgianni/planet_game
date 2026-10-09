shader_type spatial;
render_mode cull_disabled;

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

	float highland = smoothstep(500.0, 4200.0, relief);
	vec3 water = mix(vec3(0.035, 0.17, 0.42), vec3(0.12, 0.43, 0.66), smoothstep(-6000.0, 0.0, relief));
	vec3 rock = mix(vec3(0.36, 0.30, 0.28), vec3(0.68, 0.65, 0.60), highland);
	vec3 sand = vec3(0.78, 0.64, 0.34);
	vec3 soil = mix(vec3(0.16, 0.42, 0.18), vec3(0.46, 0.38, 0.18), highland);
	vec3 ice = vec3(0.84, 0.94, 1.0);
	vec3 base = classes.r * water + classes.g * rock + classes.b * sand + classes.a * soil + ice_weight * ice;
	base = mix(base, ice, 0.22 * snow + 0.18 * sea_ice);
	base = mix(base, vec3(0.49, 0.51, 0.53), 0.42 * guessed + 0.28 * age);
	base *= mix(0.42, 1.0, known);
	if (day_night) {
		base *= 0.13 + 0.87 * sqrt(daylight);
	}
	if (!OUTPUT_IS_SRGB) {
		base = srgb_to_linear(base);
	}
	ALBEDO = base;
	ROUGHNESS = 0.88;
}
