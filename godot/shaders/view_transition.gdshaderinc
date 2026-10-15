// The continuous view's transitions (ADR-0022 §4.8, R1-04), shared by the
// style packs and the climate-lab overlay. The bridge sets the uniforms.
//
// A terrain tile carries CUSTOM1 (its planet-centred direction and its
// height above sea); the cell mesh has none, which reads as zero, so one
// material tells the two apart.

// The tiles' share across the planet-region band edge: the cell mesh and the
// tiles are both drawn, and an ordered screen-door dither passes each pixel
// from one to the other (0: the cell mesh only, 1: the tiles only).
uniform float band_fade = 0.0;
// Relief exaggeration fades towards true scale as the camera descends:
// tiles are made at the planet's exaggeration (relief_baked, per radius),
// and drawn at relief_rescale times it.
uniform float relief_rescale = 1.0;
uniform float relief_baked = 0.0;

float view_tile(vec4 custom1) {
	return dot(custom1.xyz, custom1.xyz) > 0.25 ? 1.0 : 0.0;
}

// In vertex(): a tile's height and the slope of its normal, rescaled.
void view_relief(inout vec3 vertex, inout vec3 normal, vec4 custom1) {
	if (view_tile(custom1) < 0.5 || relief_rescale == 1.0) {
		return;
	}
	vec3 up = normalize(custom1.xyz);
	vertex += up * ((relief_rescale - 1.0) * relief_baked * max(custom1.w, 0.0));
	float along = dot(normal, up);
	normal = normalize(up * along + (normal - up * along) * relief_rescale);
}

// In fragment(): whether this pixel belongs to the other mesh during the
// cross-fade (interleaved gradient noise, stable per pixel).
bool view_faded_out(float tile, vec2 fragcoord) {
	if (band_fade <= 0.0) {
		return tile > 0.5;
	}
	if (band_fade >= 1.0) {
		return tile < 0.5;
	}
	float noise = fract(52.9829189 * fract(dot(floor(fragcoord), vec2(0.06711056, 0.00583715))));
	return tile > 0.5 ? noise >= band_fade : noise < band_fade;
}
