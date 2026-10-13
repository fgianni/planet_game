shader_type spatial;
render_mode cull_disabled;

// SURVEY PROTOTYPE: same channels as `stylised`, plus
//  - exact smooth interpolation (cell + two corners, barycentric in CUSTOM0)
//  - sub-cell procedural detail keyed on the planet-space position
//  - fractal coastlines on the water-weight isoline, beaches, shelves
//  - slope rock, noisy snow line, patchy class mixing, micro bump

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

varying vec2 t_cell;
varying vec2 t_c1;
varying vec4 t_c2_bary;
varying vec3 p_model;
varying vec3 n_model;

vec3 srgb_to_linear(vec3 c) {
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

float hash31(vec3 p) {
	p = fract(p * 0.1031);
	p += dot(p, p.zyx + 31.32);
	return fract((p.x + p.y) * p.z);
}

float vnoise(vec3 p) {
	vec3 i = floor(p);
	vec3 f = fract(p);
	vec3 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(hash31(i), hash31(i + vec3(1, 0, 0)), u.x),
	               mix(hash31(i + vec3(0, 1, 0)), hash31(i + vec3(1, 1, 0)), u.x), u.y),
	           mix(mix(hash31(i + vec3(0, 0, 1)), hash31(i + vec3(1, 0, 1)), u.x),
	               mix(hash31(i + vec3(0, 1, 1)), hash31(i + vec3(1, 1, 1)), u.x), u.y), u.z);
}

float fbm(vec3 p, int octaves) {
	float a = 0.5;
	float s = 0.0;
	for (int i = 0; i < octaves; i++) {
		s += a * vnoise(p);
		p = p * 2.03 + vec3(1.7, 9.2, 3.1);
		a *= 0.5;
	}
	return s;
}

// Barycentric blend of the cell texel and its two corner texels.
vec4 fetch3(sampler2D tex, vec3 w) {
	return w.x * texelFetch(tex, ivec2(t_cell + 0.5), 0)
	     + w.y * texelFetch(tex, ivec2(t_c1 + 0.5), 0)
	     + w.z * texelFetch(tex, ivec2(t_c2_bary.xy + 0.5), 0);
}

void vertex() {
	t_cell = UV;
	t_c1 = UV2;
	t_c2_bary = CUSTOM0;
	p_model = VERTEX;
	n_model = NORMAL;
}

void fragment() {
	vec3 w = vec3(t_c2_bary.z, t_c2_bary.w, 1.0 - t_c2_bary.z - t_c2_bary.w);
	vec3 dir = normalize(p_model);
	vec4 classes = fetch3(surface_class_map, w);
	float ice_weight = fetch3(surface_class_ice_map, w).r;
	float relief = fetch3(relief_map, w).r;
	float daylight = clamp(fetch3(daylight_map, w).r, 0.0, 1.0);
	float snow = clamp(fetch3(snow_cover_map, w).r, 0.0, 1.0);
	float sea_ice = clamp(fetch3(sea_ice_map, w).r, 0.0, 1.0);
	float atmosphere = clamp(fetch3(atmosphere_density_map, w).r, 0.0, 1.0);
	float known = clamp(fetch3(known_map, w).r, 0.0, 1.0);
	float guessed = clamp(fetch3(guessed_map, w).r, 0.0, 1.0);
	float age = clamp(fetch3(knowledge_age_map, w).r, 0.0, 1.0);

	// Detail noise at three scales (cell ~ 1/60 of the radius at L6).
	float n_cell = fbm(dir * 55.0, 4);
	float n_fine = fbm(dir * 260.0 + 7.0, 4);
	float n_micro = fbm(dir * 1400.0 + 3.0, 3);

	// Coast: the 0.5 isoline of the interpolated water weight, roughened.
	float water_w = classes.r + sea_ice * ice_weight;
	float coast_field = water_w - 0.5 + 0.34 * (n_cell - 0.5) + 0.12 * (n_fine - 0.5);
	float is_water = smoothstep(-0.015, 0.015, coast_field);

	// Ocean: depth ramp, light shelf near the coast.
	float depth01 = smoothstep(-5500.0, -50.0, relief);
	vec3 deep = vec3(0.020, 0.080, 0.215);
	vec3 shelf = vec3(0.075, 0.36, 0.55);
	vec3 water = mix(deep, shelf, depth01 * depth01);
	water = mix(water, vec3(0.16, 0.50, 0.60), smoothstep(0.12, 0.0, coast_field) * 0.55);
	water *= 0.94 + 0.12 * n_fine;

	// Land: patchy mixing of the land classes (weights kept, borders broken).
	vec3 lw = classes.gba + 1e-4;
	lw *= vec3(0.55 + 0.9 * fbm(dir * 90.0 + 11.0, 3),
	           0.55 + 0.9 * fbm(dir * 90.0 + 23.0, 3),
	           0.55 + 0.9 * fbm(dir * 90.0 + 37.0, 3));
	lw = pow(lw, vec3(2.2));
	lw /= (lw.x + lw.y + lw.z);
	float highland = smoothstep(400.0, 4000.0, relief + 600.0 * (n_cell - 0.5));
	vec3 rock = mix(vec3(0.40, 0.34, 0.30), vec3(0.66, 0.63, 0.59), highland) * (0.92 + 0.16 * n_micro);
	vec3 sand = vec3(0.80, 0.67, 0.40) * (0.88 + 0.24 * n_fine);
	vec3 soil_lo = mix(vec3(0.22, 0.47, 0.17), vec3(0.33, 0.52, 0.20), n_fine);
	vec3 soil = mix(soil_lo, vec3(0.50, 0.44, 0.24), highland) * (0.93 + 0.14 * n_micro);
	vec3 land = lw.x * rock + lw.y * sand + lw.z * soil;

	// Slope rock from the (exaggerated) geometry.
	float slope = 1.0 - clamp(dot(normalize(n_model), dir), 0.0, 1.0);
	land = mix(land, rock, smoothstep(0.035, 0.11, slope + 0.03 * (n_fine - 0.5)));

	// Beach band on the land side of the coast, low ground only.
	float beach = smoothstep(-0.07, -0.01, coast_field) * (1.0 - smoothstep(60.0, 400.0, relief));
	land = mix(land, vec3(0.86, 0.78, 0.56), beach * 0.85);

	// Snow: the channel decides how much; noise and slope decide where.
	vec3 ice = vec3(0.86, 0.94, 1.0);
	float land_ice = clamp(ice_weight - sea_ice * water_w, 0.0, 1.0);
	float snow_amt = max(snow, land_ice);
	float snow_mask = smoothstep(0.0, 0.08, snow_amt - (1.0 - snow_amt) * (0.25 + 0.5 * n_fine) - 0.6 * slope);
	land = mix(land, ice * (0.92 + 0.1 * n_micro), clamp(snow_mask, 0.0, 1.0));

	// Sea ice as floes.
	float floes = smoothstep(0.0, 0.06, sea_ice - 0.8 * (1.0 - sea_ice) * n_fine - 0.25 * n_micro * (1.0 - sea_ice));
	water = mix(water, ice * (0.9 + 0.1 * n_micro), floes);

	vec3 base = mix(land, water, is_water);
	base = mix(base, vec3(0.49, 0.51, 0.53), 0.42 * guessed + 0.28 * age);
	base *= mix(0.42, 1.0, known);
	if (day_night) {
		base *= 0.13 + 0.87 * sqrt(daylight);
	}

	// Micro bump on land (screen-space, surface-gradient method).
	float bump_h = (1.0 - is_water) * (0.7 * n_fine + 0.3 * n_micro) * (0.004 + 0.02 * highland);
	vec3 dpdx = dFdx(VERTEX);
	vec3 dpdy = dFdy(VERTEX);
	vec3 r1 = cross(dpdy, NORMAL);
	vec3 r2 = cross(NORMAL, dpdx);
	float det = dot(dpdx, r1);
	vec3 grad = (dFdx(bump_h) * r1 + dFdy(bump_h) * r2) / max(abs(det), 1e-12) * sign(det);
	NORMAL = normalize(NORMAL - grad * 60.0);

	float air_mass = pow(1.0 - clamp(dot(normalize(NORMAL), normalize(VIEW)), 0.0, 1.0), 3.0);
	base = mix(base, vec3(0.18, 0.55, 0.96), 0.38 * atmosphere * air_mass);
	if (!OUTPUT_IS_SRGB) {
		base = srgb_to_linear(base);
	}
	ALBEDO = base;
	ROUGHNESS = mix(0.9, 0.35, is_water * (1.0 - floes));
	SPECULAR = mix(0.3, 0.55, is_water);
}
