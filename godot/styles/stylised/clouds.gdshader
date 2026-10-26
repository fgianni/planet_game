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

// The field's coarse structures remain tied to cell cloud cover. Finer
// fluctuations break their edges without creating cover in clear cells.
const float CLOUD_FEATURES_PER_CELL = 0.65;
const float CLOUD_EDGE = 0.065;
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
	// The rank field distributes the cell's fraction spatially. A smaller
	// field perturbs the boundary and supplies bright cores and grey wisps;
	// it does not seed a new cloud where cover or optical depth is zero.
	float noise = detail_uniform(coarse, detail_seed_cloud);
	float billow = detail_field(coarse * 4.0, detail_seed_cloud + vec3(5.3, 1.1, 7.7));
	float boundary = noise + 0.28 * (billow - 0.44);
	float edge = max(CLOUD_EDGE, fwidth(boundary));
	float soft = smoothstep(boundary - edge, boundary + edge, cover);
	float patch = mix(cover, soft, amount) * step(1e-4, cover);
	patch = max(patch, step(1.0 - 1e-4, cover));
	// Opacity is the cloud's reflectance, as the radiation computes it:
	// R = (1 − g) τ / (2 + (1 − g) τ), τ from the thickness channel. Thin
	// cloud is a veil, thick cloud nearly opaque.
	float tau = -THICKNESS_DEPTH * log(1.0 - thickness);
	float scaled = (1.0 - ASYMMETRY) * tau;
	float reflectance = scaled / (2.0 + scaled);
	// Preserve the established look at ordinary depths, but do not draw the
	// opacity floor when the column has no optical depth at all.
	float density = clamp(0.04 + 2.8 * reflectance, 0.0, 0.97) *
		smoothstep(0.0, 0.02, thickness) * mix(1.0, 0.94 + 0.12 * billow, amount);
	float alpha = clamp(patch * density, 0.0, 0.97);
	if (alpha <= 0.002) {
		discard;
	}
	// From above, cloud is brightest where thick; from below, darkest.
	vec3 camera = (inverse(MODEL_MATRIX) * vec4(CAMERA_POSITION_WORLD, 1.0)).xyz;
	bool above = length(camera) > cloud_radius;
	vec3 colour = above
		? vec3(0.92, 0.93, 0.95) + vec3(0.07, 0.06, 0.04) * reflectance / 0.6
		: mix(vec3(0.95, 0.96, 0.98), vec3(0.50, 0.53, 0.58), clamp(reflectance / 0.6, 0.0, 1.0));
	colour *= mix(0.93, 1.04, smoothstep(0.25, 0.65, billow));
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
