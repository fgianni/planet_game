// Sub-cell procedural detail shared by the style packs (specification §31.5,
// land rendering survey step 3). Every kind of detail is a coverage: seeded
// noise, equalised to be uniform on [0, 1], is thresholded against the
// channel's interpolated fraction, so a place is drawn as snow, water or rock
// on about the fraction of its area the channel says, never more or less on
// average. test_detail_coverage.gd checks that against the channels.
//
// Detail fades in with the camera: none at planet scale, where a cell covers
// a few pixels (the channels alone, corners interpolated), full at region
// scale. The bridge sets the uniforms below; the noise offsets come from the
// presentation RNG stream keyed by (world_seed, detail_kind), ADR-0018 §4.8.

uniform float cell_angular_size = 0.0175; // rad, mean cell spacing on the unit sphere
uniform vec3 detail_seed_coast = vec3(0.0);
uniform vec3 detail_seed_land_mix = vec3(0.0);
uniform vec3 detail_seed_snow = vec3(0.0);
uniform vec3 detail_seed_sea_ice = vec3(0.0);
uniform vec3 detail_seed_texture = vec3(0.0);

const float DETAIL_FEATURES_PER_CELL = 4.0; // patches about a quarter of a cell across
const int DETAIL_OCTAVES = 3;
const float DETAIL_START_PX = 8.0; // cell size on screen where detail begins
const float DETAIL_FULL_PX = 24.0; // and where it is complete
// Quantiles of detail_field at probabilities 0, 1/32, ..., 1, measured by
// test_detail_coverage.gd; detail_uniform interpolates them to map the
// field to uniform on [0, 1].
const float DETAIL_FIELD_QUANTILES[33] = float[](
	0.0686, 0.2357, 0.2672, 0.2893, 0.3068, 0.3217, 0.3348, 0.3470, 0.3584, 0.3693, 0.3793,
	0.3891, 0.3984, 0.4076, 0.4168, 0.4259, 0.4349, 0.4440, 0.4532, 0.4624, 0.4713, 0.4803,
	0.4898, 0.4997, 0.5102, 0.5214, 0.5336, 0.5469, 0.5617, 0.5791, 0.6009, 0.6318, 0.7888);

float detail_hash(vec3 p) {
	p = fract(p * 0.1031);
	p += dot(p, p.zyx + 31.32);
	return fract((p.x + p.y) * p.z);
}

float detail_value_noise(vec3 p) {
	vec3 i = floor(p);
	vec3 f = fract(p);
	vec3 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(detail_hash(i), detail_hash(i + vec3(1, 0, 0)), u.x),
	               mix(detail_hash(i + vec3(0, 1, 0)), detail_hash(i + vec3(1, 1, 0)), u.x), u.y),
	           mix(mix(detail_hash(i + vec3(0, 0, 1)), detail_hash(i + vec3(1, 0, 1)), u.x),
	               mix(detail_hash(i + vec3(0, 1, 1)), detail_hash(i + vec3(1, 1, 1)), u.x), u.y), u.z);
}

float detail_fbm(vec3 p) {
	float amplitude = 0.5;
	float sum = 0.0;
	for (int octave = 0; octave < DETAIL_OCTAVES; octave++) {
		sum += amplitude * detail_value_noise(p);
		p = p * 2.03 + vec3(1.7, 9.2, 3.1);
		amplitude *= 0.5;
	}
	return sum;
}

// The raw noise field of one detail kind at a direction on the unit sphere.
float detail_field(vec3 direction, vec3 seed) {
	return detail_fbm(direction * (DETAIL_FEATURES_PER_CELL / cell_angular_size) + seed);
}

// The field mapped to uniform on [0, 1] through its measured quantiles.
float detail_uniform(vec3 direction, vec3 seed) {
	float value = detail_field(direction, seed);
	float result = step(DETAIL_FIELD_QUANTILES[32], value);
	for (int knot = 32; knot >= 1; knot--) {
		float lower = DETAIL_FIELD_QUANTILES[knot - 1];
		float upper = DETAIL_FIELD_QUANTILES[knot];
		if (value >= lower && value < upper) {
			result = (float(knot - 1) + (value - lower) / (upper - lower)) / 32.0;
		}
	}
	return result;
}

// 0 at planet scale, 1 at region scale.
float detail_amount(vec3 direction) {
	float pixels = cell_angular_size / max(length(fwidth(direction)), 1e-7);
	return smoothstep(DETAIL_START_PX, DETAIL_FULL_PX, pixels);
}

// Drawn coverage of a fraction: the fraction itself at planet scale, and at
// region scale 1 where the uniform noise lies below it, antialiased over a
// pixel. Fractions of exactly 0 or 1 stay exact, and the result never
// decreases with the fraction (the antialiasing width depends on the noise
// alone), so detail_partition's bands cannot overlap.
float detail_cover(float fraction, float noise, float amount) {
	float f = clamp(fraction, 0.0, 1.0);
	// Capped at 1: wider, a noise finer than a pixel would pull every
	// coverage toward one half.
	float width = clamp(fwidth(noise), 1e-4, 1.0);
	float hard = clamp((f - noise) / width + 0.5, 0.0, 1.0);
	hard = max(hard * step(1e-4, f), step(1.0 - 1e-4, f));
	return mix(f, hard, amount);
}

// Coverage shares of four classes with these weights (any total): each
// class gets a band of the noise as wide as its share, so a 30/70 mix
// shows as patches rather than an average. The shares sum to 1.
vec4 detail_partition(vec4 weights, float noise, float amount) {
	float total = weights.x + weights.y + weights.z + weights.w;
	if (total <= 1e-6) {
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	vec4 share = weights / total;
	float first = detail_cover(share.x, noise, amount);
	float second = detail_cover(share.x + share.y, noise, amount);
	float third = detail_cover(share.x + share.y + share.z, noise, amount);
	return vec4(first, second - first, third - second, 1.0 - third);
}

// The planet-centred position of a vertex: the cell mesh's VERTEX, or a
// terrain tile's CUSTOM1.xyz (its VERTEX is relative to the tile's origin;
// the cell mesh has no CUSTOM1, which reads as zero).
vec3 detail_planet_position(vec3 vertex, vec4 custom1) {
	return dot(custom1.xyz, custom1.xyz) > 0.25 ? custom1.xyz : vertex;
}

// Terrain tiles (R1-04) carry their drawn surface: (1, height above sea in
// metres) from CUSTOM1; the cell mesh reads (0, 0).
vec2 detail_tile_surface(vec4 custom1) {
	return dot(custom1.xyz, custom1.xyz) > 0.25 ? vec2(1.0, custom1.w) : vec2(0.0);
}

// On a tile the coast follows the geometry: water where the drawn surface
// is below sea level, antialiased over a pixel. The relief reproduces each
// cell's hypsometry, so the area below sea level is the cell's water.
float detail_geometry_water(float height_m) {
	return clamp(0.5 - height_m / max(fwidth(height_m), 1e-3), 0.0, 1.0);
}

// The ice weight that belongs to land: on a tile the water, sea ice
// included, comes from the geometry, so the land's share of the classes
// leaves out the sea ice of the cell's water.
float detail_land_ice(float ice_weight, float open_water_weight, float sea_ice) {
	float water = clamp(open_water_weight / max(1.0 - sea_ice, 1e-3), 0.0, 1.0);
	return max(0.0, ice_weight - sea_ice * water);
}

// Barycentric blend of the cell texel and its two corner texels, from the
// planet mesh's UV, UV2 and CUSTOM0 (exact; see test_cell_texels.gd).
vec4 detail_fetch(sampler2D channel, vec2 cell, vec2 first_corner, vec4 second_corner_and_weights) {
	vec2 w = second_corner_and_weights.zw;
	return w.x * texelFetch(channel, ivec2(cell + 0.5), 0)
	     + w.y * texelFetch(channel, ivec2(first_corner + 0.5), 0)
	     + (1.0 - w.x - w.y) * texelFetch(channel, ivec2(second_corner_and_weights.xy + 0.5), 0);
}
