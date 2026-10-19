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

// Cloud features about this many per cell, the fBm's finer octaves adding
// texture inside them: coherent cloud shapes a couple of cells across, so
// the field has structure from orbit, where a cell is a few pixels. They
// switch on between these cell sizes on screen.
const float CLOUD_FEATURES_PER_CELL = 0.5;
// The soft edge: a cloud fades over this much of the noise's range.
const float CLOUD_EDGE = 0.16;
const float CLOUD_START_PX = 1.5;
const float CLOUD_FULL_PX = 5.0;
// cloud_thickness = 1 − exp(−τ / 10) (ADR-0023 §4.8).
const float THICKNESS_DEPTH = 10.0;
const float ASYMMETRY = 0.85;   // g, as the radiation's two-stream

void fragment() {
	float cover = clamp(CHANNEL(cloud_cover_map).r, 0.0, 1.0);
	float thickness = clamp(CHANNEL(cloud_thickness_map).r, 0.0, 0.999);
	float daylight = clamp(CHANNEL(daylight_map).r, 0.0, 1.0);
	vec3 unit = normalize(direction);
	float pixels = cell_angular_size / max(length(fwidth(unit)), 1e-7);
	float amount = smoothstep(CLOUD_START_PX, CLOUD_FULL_PX, pixels);
	vec3 coarse = unit * (CLOUD_FEATURES_PER_CELL / DETAIL_FEATURES_PER_CELL);
	// Soft-edged cover: where the uniform noise lies below the cover, faded
	// over CLOUD_EDGE. Its mean over an area stays the cover (the fade is
	// symmetric), and at planet scale it is the cover itself.
	float noise = detail_uniform(coarse, detail_seed_cloud);
	float soft = smoothstep(noise - CLOUD_EDGE, noise + CLOUD_EDGE, cover);
	float patch = mix(cover, soft * step(1e-4, cover), amount);
	float billow = detail_field(coarse * 3.0, detail_seed_cloud + vec3(5.3, 1.1, 7.7));
	// Opacity is the cloud's reflectance, as the radiation computes it:
	// R = (1 − g) τ / (2 + (1 − g) τ), τ from the thickness channel. Thin
	// cloud is a veil, thick cloud nearly opaque.
	float tau = -THICKNESS_DEPTH * log(1.0 - thickness);
	float scaled = (1.0 - ASYMMETRY) * tau;
	float reflectance = scaled / (2.0 + scaled);
	float density = clamp(0.10 + 1.7 * reflectance, 0.0, 0.97) * mix(1.0, 0.7 + 0.6 * billow, amount);
	float alpha = clamp(patch * density, 0.0, 0.97);
	if (alpha <= 0.002) {
		discard;
	}
	// From above, cloud is brightest where thick; from below, darkest.
	vec3 camera = (inverse(MODEL_MATRIX) * vec4(CAMERA_POSITION_WORLD, 1.0)).xyz;
	bool above = length(camera) > cloud_radius;
	vec3 colour = above
		? vec3(0.86, 0.88, 0.92) + vec3(0.13, 0.11, 0.08) * reflectance / 0.6
		: mix(vec3(0.95, 0.96, 0.98), vec3(0.50, 0.53, 0.58), clamp(reflectance / 0.6, 0.0, 1.0));
	colour *= mix(0.93, 1.04, billow);
	if (day_night) {
		colour *= mix(0.12, 1.0, smoothstep(0.0, 0.15, daylight));
	}
	if (!OUTPUT_IS_SRGB) {
		colour = srgb_to_linear(colour);
	} else {
		colour = display_unshaded(clamp(colour, 0.0, 1.0));
	}
	ALBEDO = colour;
	ALPHA = alpha;
}
