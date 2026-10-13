shader_type spatial;
render_mode unshaded, cull_back; // also checks the fans face outward

// Test-only probe for test_cell_texels.gd: shows the texel addresses the
// planet mesh hands a style, not any channel's appearance.
//   mode 0: the code stored at the UV texel (the cell's own address).
//   mode 1: R = UV2 names a corner texel, G = CUSTOM0.xy names a corner
//           texel, B = CUSTOM0.zw is a barycentric pair.

uniform sampler2D codes : filter_nearest;
uniform int mode = 0;

varying vec2 cell_texel;
varying vec2 first_corner_texel;
varying vec4 second_corner_and_weights;

void vertex() {
	cell_texel = UV;
	first_corner_texel = UV2;
	second_corner_and_weights = CUSTOM0;
}

void fragment() {
	vec3 colour;
	if (mode == 0) {
		colour = texelFetch(codes, ivec2(cell_texel + 0.5), 0).rgb;
	} else {
		float first = texelFetch(codes, ivec2(first_corner_texel + 0.5), 0).a;
		float second = texelFetch(codes, ivec2(second_corner_and_weights.xy + 0.5), 0).a;
		vec2 w = second_corner_and_weights.zw;
		float barycentric = float(w.x >= -1e-4 && w.y >= -1e-4 && w.x + w.y <= 1.0 + 1e-4);
		colour = vec3(step(0.5, first), step(0.5, second), barycentric);
	}
	// Unshaded into a SubViewport: the stored bytes come back unchanged.
	ALBEDO = colour;
}
