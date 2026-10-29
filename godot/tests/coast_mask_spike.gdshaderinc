// Probe-only cube atlas lookup. Coordinates are the inverse of the
// equal-angle cube-face directions in sim/presentation/terrain_tile.cpp.
uniform sampler2D coast_probe_atlas : filter_nearest, repeat_disable;
uniform int coast_probe_side = 512;

float coast_probe_water(vec3 d) {
	int face;
	vec2 warped;
	vec3 a = abs(d);
	if (a.x >= a.y && a.x >= a.z) {
		face = d.x >= 0.0 ? 0 : 1;
		warped = vec2(d.x >= 0.0 ? -d.z : d.z, d.y) / a.x;
	} else if (a.y >= a.z) {
		face = d.y >= 0.0 ? 2 : 3;
		warped = vec2(d.x, d.y >= 0.0 ? -d.z : d.z) / a.y;
	} else {
		face = d.z >= 0.0 ? 4 : 5;
		warped = vec2(d.z >= 0.0 ? d.x : -d.x, d.y) / a.z;
	}
	vec2 uv = clamp(0.5 + atan(warped) * (2.0 / 3.141592653589793), vec2(0.0), vec2(0.999999));
	ivec2 xy = ivec2(uv * float(coast_probe_side));
	xy += ivec2(face % 3, face / 3) * coast_probe_side;
	return texelFetch(coast_probe_atlas, xy, 0).r;
}
