shader_type spatial;
// The map style's cloud layer (ADR-0023 §4.8, task M8-06): white hatching
// over a faint veil, so the map beneath stays readable. Drawn on the cell
// mesh's geometry lifted to `cloud_radius`.
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
	float amount = detail_amount(unit);
	float patch = detail_cover(cover, detail_uniform(unit, detail_seed_cloud), amount);
	// Hatched, as a map shades cloud: nearly opaque diagonal lines and a
	// faint veil between them that is stronger where the cloud is thick. The lines show cloud even at partial cover, and
	// the gaps keep the map readable under overcast. They keep a fixed
	// screen period, so the hatch never aliases at orbit, where a cell is a
	// pixel or two.
	// The cover sets the lines' width (half the period when overcast), so
	// partial cover reads as sparser hatching at full strength.
	const float HATCH_PERIOD_PX = 6.0;
	float phase = mod(FRAGCOORD.x + FRAGCOORD.y, HATCH_PERIOD_PX) / HATCH_PERIOD_PX;
	float width = 0.5 * patch;
	float edge = 1.0 / HATCH_PERIOD_PX;
	float line = 1.0 - smoothstep(width - 0.5 * edge, width + 0.5 * edge, phase);
	line *= step(1e-4, patch);
	float gap = mix(0.10, 0.22, step(0.5, thickness));
	float alpha = max(0.85 * line, gap * patch);
	if (alpha <= 0.002) {
		discard;
	}
	// Two-tone lines, a white stroke edged in dark slate: over light land
	// the dark edge contrasts, over dark ocean the white, so the hatch keeps
	// its contrast over both, in luminance too. The veil between is white.
	float stroke = phase / max(width, 1e-4);
	vec3 colour = line > 0.5 && stroke > 0.6 ? vec3(0.16, 0.20, 0.30) : vec3(0.96, 0.97, 0.99);
	if (day_night) {
		colour *= mix(0.2, 1.0, smoothstep(0.0, 0.15, daylight));
	}
	if (!OUTPUT_IS_SRGB) {
		colour = srgb_to_linear(colour);
	} else {
		colour = display_unshaded(colour);
	}
	ALBEDO = colour;
	ALPHA = alpha;
}
