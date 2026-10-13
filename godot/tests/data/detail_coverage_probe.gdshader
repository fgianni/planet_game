shader_type spatial;
render_mode unshaded, cull_back;

// Test-only probe for test_detail_coverage.gd: draws the coverages of
// surface_detail.gdshaderinc, the code the style packs draw with.
//   mode 0: the raw noise field of the coast kind, 16-bit.
//   mode 1: interpolated water, snow and sea-ice fractions (no detail).
//   mode 2: their drawn coverages at full detail.
//   mode 3: the first three class shares of the interpolated weights.
//   mode 4: the same shares drawn at full detail.
//   mode 5: detail_amount at this camera.
//   mode 6: the equalised noise of the coast kind, 16-bit.

#include "res://shaders/surface_detail.gdshaderinc"

uniform sampler2D fraction_map : filter_nearest; // r water, g snow, b sea ice
uniform sampler2D class_map : filter_nearest; // four class weights
uniform int mode = 0;

// The offscreen readback is exact only from about 62/255 up (darker values
// come back compressed, 7/255 and below as 0), so every value is written in
// 64/255..1: an affine code, or two such bytes for 16 bits.
vec3 probe_encode(vec3 value) {
	return 0.25 + 0.75 * clamp(value, 0.0, 1.0);
}

vec3 probe_encode_16(float value) {
	float scaled = clamp(value, 0.0, 1.0) * 127.0;
	return vec3((64.0 + floor(scaled)) / 255.0, (64.0 + 127.0 * fract(scaled)) / 255.0, 0.25);
}

varying vec2 cell_texel;
varying vec2 first_corner_texel;
varying vec4 second_corner_and_weights;
varying vec3 model_position;

void vertex() {
	cell_texel = UV;
	first_corner_texel = UV2;
	second_corner_and_weights = CUSTOM0;
	model_position = VERTEX;
}

void fragment() {
	vec3 direction = normalize(model_position);
	vec4 fractions = detail_fetch(fraction_map, cell_texel, first_corner_texel, second_corner_and_weights);
	vec4 weights = detail_fetch(class_map, cell_texel, first_corner_texel, second_corner_and_weights);
	vec3 colour;
	if (mode == 0 || mode == 6) {
		float value = mode == 0 ? detail_field(direction, detail_seed_coast)
		                        : detail_uniform(direction, detail_seed_coast);
		colour = probe_encode_16(value);
	} else if (mode == 1) {
		colour = probe_encode(fractions.rgb);
	} else if (mode == 2) {
		colour = probe_encode(vec3(
			detail_cover(fractions.r, detail_uniform(direction, detail_seed_coast), 1.0),
			detail_cover(fractions.g, detail_uniform(direction, detail_seed_snow), 1.0),
			detail_cover(fractions.b, detail_uniform(direction, detail_seed_sea_ice), 1.0)));
	} else if (mode == 3) {
		colour = probe_encode(detail_partition(weights, 0.5, 0.0).rgb);
	} else if (mode == 4) {
		colour = probe_encode(detail_partition(weights, detail_uniform(direction, detail_seed_land_mix), 1.0).rgb);
	} else if (mode == 5) {
		colour = probe_encode(vec3(detail_amount(direction)));
	}
	ALBEDO = colour;
}
