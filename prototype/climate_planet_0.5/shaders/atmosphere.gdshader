shader_type spatial;
render_mode unshaded, blend_add, depth_draw_never, cull_back;

uniform vec4 glow : source_color = vec4(0.56, 0.86, 1.0, 1.0);
uniform float power = 4.0;
uniform float intensity = 1.8;

void fragment() {
	float f = pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), power);
	ALBEDO = glow.rgb;
	// fade to nothing at the very edge so the halo has no hard outline
	float edge = smoothstep(0.0, 0.25, dot(NORMAL, VIEW));
	ALPHA = clamp(f * intensity * edge, 0.0, 1.0);
}
