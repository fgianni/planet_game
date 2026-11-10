// The coast from canonical pages (ADR-0030 Amendment 1). A terrain tile's
// vertices carry, in CUSTOM2: their place in the tile's own page (xy), the
// page's layer in `coast_pages` (z) and a mode (w):
//   0  no page: the fallback (the drawn surface's waterline) stands;
//   1  water throughout, -1 land throughout (no layer);
//   2  a point page: the signed water field s (metres, water above 0), whose
//      zero line is the coast, antialiased over a pixel;
//   3  a coverage page (A1.3a): water coverage - 0.5, so the water is the
//      bilinear value + 0.5.
// (Per-instance uniforms would hold layer and mode, but the Compatibility
// renderer has room for too few instances.)

uniform sampler2DArray coast_pages : filter_linear, repeat_disable;

float coast_page_water(vec2 page_uv, vec2 layer_and_mode, float fallback) {
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
