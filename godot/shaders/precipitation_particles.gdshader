shader_type spatial;
render_mode unshaded, cull_disabled, blend_mix, depth_draw_never;

void vertex() {
	float phase = fract(TIME * 0.85 + COLOR.a);
	VERTEX -= normalize(VERTEX) * (0.007 * phase);
}

void fragment() {
	float edge = smoothstep(1.0, 0.2, abs(UV.y));
	float tail = smoothstep(0.0, 0.18, UV.x);
	ALBEDO = COLOR.rgb;
	EMISSION = COLOR.rgb * 0.8;
	ALPHA = 0.82 * edge * tail;
}
