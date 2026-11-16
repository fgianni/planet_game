// The coast from canonical pages (ADR-0030 Amendment 1). A terrain tile's
// vertices carry, in CUSTOM2: their place in the page the tile was made with
// (xy) and that page's slot (z, -1 for none). A slot table maps a slot to
// the page's layer in `coast_pages` and its mode:
//   0  no layer for now (or a free slot): the fallback stands;
//   1  water throughout, -1 land throughout (no layer);
//   2  a point page: the signed water field s (metres, water above 0), whose
//      zero line is the coast, antialiased over a pixel;
//   3  a coverage page (A1.3a): water coverage - 0.5, so the water is the
//      bilinear value + 0.5.
// The fallback is the drawn surface's waterline on a tile, the cell's
// fractional cover on the cell mesh. (Per-instance uniforms would hold the
// slot, but the Compatibility renderer has room for too few instances.)

uniform sampler2DArray coast_pages : filter_linear, repeat_disable;
uniform sampler2D coast_slots : filter_nearest, repeat_disable;
// R1-05's rendered gates (godot/tests/capture_coast_sweep.gd): the surface
// shows its water in red, 1 in green on a terrain tile (0 on the cell mesh),
// and in blue a hash of the tile's page slot (0 without one).
uniform bool coast_debug = false;

vec3 coast_debug_colour(float water, float tile, float slot) {
	return vec3(water, tile, slot < -0.5 ? 0.0 : 0.2 + 0.8 * fract(slot * 0.618034));
}

// One tap of a page: the water at a place in it, 0 to 1.
float coast_page_tap(vec2 page_uv, float layer, float mode, float ramp) {
	// 257 nodes a side, a node at each texel centre: linear filtering is the
	// page's bilinear interpolation.
	vec2 texel = (clamp(page_uv, 0.0, 1.0) * 256.0 + 0.5) / 257.0;
	float value = texture(coast_pages, vec3(texel, layer)).r;
	if (mode > 2.5) {
		return clamp(value + 0.5, 0.0, 1.0);
	}
	return clamp(0.5 + value / ramp, 0.0, 1.0);
}

// The water of a pixel, its footprint's mean: an n x n grid of taps over
// the pixel (from the page coordinates' screen derivatives dx and dy), at
// most a page node apart, so n is 2 for most pixels and grows to 8 where
// a pixel's footprint spans many nodes (grazing views at the limb, a
// coarser page while its children load). One tap would alias there.
float coast_page_water_grad(vec2 page_uv, vec2 dx, vec2 dy, float slot, float fallback) {
	if (slot < -0.5) {
		return fallback;
	}
	vec2 layer_and_mode = texelFetch(coast_slots, ivec2(int(round(slot)), 0), 0).rg;
	float mode = layer_and_mode.y;
	if (mode < 1.5) {
		return mode > 0.5 ? 1.0 : (mode < -0.5 ? 0.0 : fallback);
	}
	float layer = round(layer_and_mode.x);
	float span = max(length(dx), length(dy)) * 256.0; // page nodes across the pixel
	int n = int(clamp(ceil(span), 2.0, 8.0));
	// A point page's zero line, antialiased over a tap's share of the pixel.
	float centre = texture(coast_pages, vec3((clamp(page_uv, 0.0, 1.0) * 256.0 + 0.5) / 257.0, layer)).r;
	float ramp = max(fwidth(centre) / float(n), 1e-6);
	float sum = 0.0;
	for (int j = 0; j < n; j++) {
		for (int i = 0; i < n; i++) {
			vec2 at = vec2((float(i) + 0.5) / float(n) - 0.5, (float(j) + 0.5) / float(n) - 0.5);
			sum += coast_page_tap(page_uv + at.x * dx + at.y * dy, layer, mode, ramp);
		}
	}
	return sum / float(n * n);
}

// On a tile, whose page coordinates are continuous over its mesh.
float coast_page_water(vec2 page_uv, float slot, float fallback) {
	return coast_page_water_grad(page_uv, dFdx(page_uv), dFdy(page_uv), slot, fallback);
}

// The planet-band cell mesh reads the same pages (A1.3) through a page
// table: for every level-6 square of each cube face, the slot and level of
// the finest resident page covering it (slot -1 for none); faces side by
// side, 64 x 64 squares each (terrain_tiles.hpp, coast_table_level).
uniform sampler2D coast_table : filter_nearest, repeat_disable;
const float coast_table_squares = 64.0;

// The cube faces of the terrain tiles (terrain_tile.cpp): normal, u and v
// axes; a direction belongs to the face its normal is closest to.
const vec3 coast_face_normal[6] = vec3[](vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0),
	vec3(0.0, -1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, -1.0));
const vec3 coast_face_u[6] = vec3[](vec3(0.0, 0.0, -1.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0),
	vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0));
const vec3 coast_face_v[6] = vec3[](vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, -1.0),
	vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0));

// Where the view ray through a point of the drawn surface meets the unit
// sphere, all in model space (planet-centred, radii). The canonical coast
// lies on that sphere: the tiles draw water at sea level, while the cell
// mesh's flat faces sag below it and its land stands above, so its own
// surface point would shift the coast, most at grazing angles. Past the
// sphere's silhouette, the surface point's own direction.
vec3 coast_sphere_point(vec3 surface, vec3 eye) {
	vec3 ray = normalize(surface - eye);
	float b = dot(eye, ray);
	float disc = b * b - (dot(eye, eye) - 1.0);
	if (disc < 0.0) {
		return normalize(surface);
	}
	float t = -b - sqrt(disc);
	return t > 0.0 ? normalize(eye + ray * t) : normalize(surface);
}

// A direction's place on a given cube face, u and v in [0, 1] (the
// equal-angle mapping: u = atan(U) 4 / pi, then [0, 1]).
vec2 coast_on_face(vec3 direction, int face) {
	vec2 plane = vec2(dot(direction, coast_face_u[face]), dot(direction, coast_face_v[face])) /
	             dot(direction, coast_face_normal[face]);
	return clamp((atan(plane) * 4.0 / 3.14159265358979 + 1.0) * 0.5, 0.0, 1.0);
}

// The water of a cell-mesh point, from the page covering its direction, or
// the fallback where no page does yet.
float coast_cell_water(vec3 direction, float fallback) {
	int face = 0;
	float best = -2.0;
	for (int f = 0; f < 6; f++) {
		float a = dot(direction, coast_face_normal[f]);
		if (a > best) {
			best = a;
			face = f;
		}
	}
	vec2 on_face = coast_on_face(direction, face);
	vec2 square = min(floor(on_face * coast_table_squares), vec2(coast_table_squares - 1.0));
	vec2 entry = texelFetch(coast_table, ivec2(int(square.x) + face * int(coast_table_squares), int(square.y)), 0).rg;
	float tiles = exp2(entry.y);
	vec2 tile = min(floor(on_face * tiles), vec2(tiles - 1.0));
	// The screen derivatives of the place, from the direction's (continuous)
	// on this pixel's own face: a page's own coordinates jump by a whole page
	// where a pixel quad straddles two pages, and the face's own where it
	// straddles two faces.
	vec2 dx = (coast_on_face(direction + dFdx(direction), face) - on_face) * tiles;
	vec2 dy = (coast_on_face(direction + dFdy(direction), face) - on_face) * tiles;
	return coast_page_water_grad(on_face * tiles - tile, dx, dy, entry.x, fallback);
}
