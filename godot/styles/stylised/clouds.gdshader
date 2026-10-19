shader_type spatial;
// The stylised cloud layer (ADR-0023 §4.8, task M8-06): soft white cloud on a
// shell over the surface. The bridge draws the cell mesh's own geometry with
// this shader, lifted to `cloud_radius`.
render_mode blend_mix, depth_draw_never, cull_disabled, unshaded;

#include "res://shaders/surface_detail.gdshaderinc"
#include "res://shaders/display_transfer.gdshaderinc"

uniform sampler2D cloud_cover_map : filter_nearest;
uniform sampler2D cloud_thickness_map : filter_nearest;
uniform sampler2D daylight_map : filter_nearest;
uniform float cloud_radius = 1.0005;
uniform bool day_night = true;
uniform vec3 detail_seed_cloud = vec3(0.0);

varying vec2 cell_texel;
varying vec2 first_corner_texel;
varying vec4 second_corner_and_weights;
varying vec3 direction;

#define CHANNEL(map) detail_fetch(map, cell_texel, first_corner_texel, second_corner_and_weights)

vec3 srgb_to_linear(vec3 c) {
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void vertex() {
	cell_texel = UV;
	first_corner_texel = UV2;
	second_corner_and_weights = CUSTOM0;
	direction = normalize(VERTEX);
	VERTEX = direction * cloud_radius;
	NORMAL = direction;
}

void fragment() {
	float cover = clamp(CHANNEL(cloud_cover_map).r, 0.0, 1.0);
	float thickness = clamp(CHANNEL(cloud_thickness_map).r, 0.0, 1.0);
	float daylight = clamp(CHANNEL(daylight_map).r, 0.0, 1.0);
	vec3 unit = normalize(direction);
	// Cover as patches over its area (surface_detail), softened at the edge.
	float amount = detail_amount(unit);
	float noise = detail_uniform(unit, detail_seed_cloud);
	float patch = detail_cover(cover, noise, amount);
	float billow = detail_field(unit * 2.0, detail_seed_cloud + vec3(5.3, 1.1, 7.7));
	float density = mix(0.36, 0.9, thickness) * mix(1.0, 0.75 + 0.5 * billow, amount);
	float alpha = clamp(patch * density, 0.0, 0.95);
	if (alpha <= 0.002) {
		discard;
	}
	// White tops, greyer where the cloud is thick.
	vec3 colour = mix(vec3(0.97, 0.98, 1.0), vec3(0.70, 0.73, 0.78), 0.6 * thickness * thickness);
	colour *= mix(0.9, 1.05, billow);
	if (day_night) {
		colour *= mix(0.12, 1.0, smoothstep(0.0, 0.15, daylight));
	}
	if (!OUTPUT_IS_SRGB) {
		colour = srgb_to_linear(colour);
	} else {
		colour = display_unshaded(colour);
	}
	ALBEDO = colour;
	ALPHA = alpha;
}
