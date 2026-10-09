shader_type spatial;
render_mode unshaded, cull_disabled, blend_mix, depth_draw_never;

void fragment() {
	float phase = fract(TIME * 0.32 + COLOR.a);
	float along = abs(UV.x - phase);
	along = min(along, 1.0 - along);
	float streak = smoothstep(0.12, 0.025, along);
	float edge = smoothstep(1.0, 0.25, abs(UV.y));
	ALBEDO = COLOR.rgb;
	EMISSION = COLOR.rgb * 0.65;
	ALPHA = 0.88 * streak * edge;
}
