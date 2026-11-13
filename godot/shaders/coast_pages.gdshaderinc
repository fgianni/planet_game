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

float coast_page_water(vec2 page_uv, float slot, float fallback) {
	if (slot < -0.5) {
		return fallback;
	}
	vec2 layer_and_mode = texelFetch(coast_slots, ivec2(int(round(slot)), 0), 0).rg;
	float mode = layer_and_mode.y;
	if (mode < 1.5) {
		return mode > 0.5 ? 1.0 : (mode < -0.5 ? 0.0 : fallback);
	}
	// 257 nodes a side, a node at each texel centre: linear filtering is the
	// page's bilinear interpolation.
	vec2 texel = (page_uv * 256.0 + 0.5) / 257.0;
	float value = texture(coast_pages, vec3(texel, round(layer_and_mode.x))).r;
	if (mode > 2.5) {
		return clamp(value + 0.5, 0.0, 1.0);
	}
	return clamp(0.5 + value / max(fwidth(value), 1e-6), 0.0, 1.0);
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
	// The equal-angle mapping: u = atan(U) 4 / pi, then [0, 1].
	vec2 plane = vec2(dot(direction, coast_face_u[face]), dot(direction, coast_face_v[face])) / best;
	vec2 on_face = clamp((atan(plane) * 4.0 / 3.14159265358979 + 1.0) * 0.5, 0.0, 1.0);
	vec2 square = min(floor(on_face * coast_table_squares), vec2(coast_table_squares - 1.0));
	vec2 entry = texelFetch(coast_table, ivec2(int(square.x) + face * int(coast_table_squares), int(square.y)), 0).rg;
	float tiles = exp2(entry.y);
	vec2 tile = min(floor(on_face * tiles), vec2(tiles - 1.0));
	return coast_page_water(on_face * tiles - tile, entry.x, fallback);
}
