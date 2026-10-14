// Godot's Compatibility renderer (4.2.2 to 4.7.2 at least, on any GPU,
// llvmpipe included) treats a shader's ALBEDO and EMISSION as sRGB and takes
// them to linear with a fast cubic polynomial, then encodes the result with
// a power curve that omits sRGB's linear segment near black. The two do not
// invert each other at the dark end: 8/255 is shown as 1, 16 as 12, 32 as 30,
// and everything at 7/255 or below as black; from about 62/255 up the
// colour is unchanged. The screen shows the same as an offscreen capture.
//
// These functions undo it where OUTPUT_IS_SRGB is true (Compatibility), so
// a colour is shown as specified. test_display_transfer.gd measures the
// renderer's curve and checks that they do.

const vec3 DISPLAY_CUBIC = vec3(0.305306011, 0.682171111, 0.012522878);

// The renderer's polynomial: sRGB-encoded value -> linear.
vec3 display_cubic(vec3 value) {
	return value * (value * (value * DISPLAY_CUBIC.x + DISPLAY_CUBIC.y) + DISPLAY_CUBIC.z);
}

// The value the polynomial takes to this linear light (Newton's method; the
// polynomial is increasing and convex on [0, 1]).
vec3 display_cubic_inverse(vec3 linear) {
	vec3 value = clamp(sqrt(linear / DISPLAY_CUBIC.y), 0.0, 1.0);
	for (int step = 0; step < 6; step++) {
		vec3 slope = value * (3.0 * DISPLAY_CUBIC.x * value + 2.0 * DISPLAY_CUBIC.y) + DISPLAY_CUBIC.z;
		value = clamp(value - (display_cubic(value) - linear) / slope, 0.0, 1.0);
	}
	return value;
}

// For ALBEDO of an unshaded material, and for EMISSION: the value the
// renderer shows as this sRGB colour.
vec3 display_unshaded(vec3 srgb) {
	vec3 c = clamp(srgb, 0.0, 1.0);
	vec3 linear = pow((c + 0.055) / 1.055, vec3(2.4)) * step(vec3(1e-6), c);
	return display_cubic_inverse(linear);
}

// For ALBEDO of a lit material: the value whose linear light is this sRGB
// colour's, so lighting starts from the specified reflectance. (The final
// encoding, after lighting, still drops the linear segment: lit results
// under 6/255 stay up to 3 levels dark.)
vec3 display_albedo(vec3 srgb) {
	vec3 c = clamp(srgb, 0.0, 1.0);
	vec3 linear = mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
	return display_cubic_inverse(linear);
}
