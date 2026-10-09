shader_type spatial;
render_mode cull_disabled;

uniform sampler2D daylight_map : filter_nearest;
uniform sampler2D temperature_anomaly_map : filter_nearest;
uniform bool day_night = true;
uniform bool insolation_view = false;
uniform bool temperature_anomaly_view = false;
uniform bool data_view = false;

varying vec2 channel_texel;

vec3 srgb_to_linear(vec3 c) {
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void vertex() {
	channel_texel = UV;
}

void fragment() {
	ivec2 p = ivec2(channel_texel + 0.5);
	float x = clamp(texelFetch(daylight_map, p, 0).r, 0.0, 1.0);
	float daylight = sqrt(x);
	vec3 base;
	if (temperature_anomaly_view) {
		float anomaly = clamp(texelFetch(temperature_anomaly_map, p, 0).r, -1.0, 1.0);
		vec3 cold = vec3(0.035, 0.11, 0.28);
		vec3 neutral = vec3(0.88, 0.88, 0.84);
		vec3 warm = vec3(0.96, 0.63, 0.08);
		base = anomaly < 0.0 ? mix(neutral, cold, -anomaly) : mix(neutral, warm, anomaly);
	} else if (insolation_view) {
		base = vec3(0.015 + 0.95 * daylight, 0.025 + 0.68 * daylight, 0.08 + 0.28 * x);
	} else {
		base = COLOR.rgb;
		if (day_night && !data_view) {
			base *= 0.16 + 0.84 * daylight;
		}
	}
	if (!OUTPUT_IS_SRGB) {
		base = srgb_to_linear(base);
	}
	if (data_view) {
		ALBEDO = vec3(0.0);
		EMISSION = base;
	} else {
		ALBEDO = base;
	}
	ROUGHNESS = 0.9;
}
