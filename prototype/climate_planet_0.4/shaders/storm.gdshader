shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled;

uniform float strength = 1.0;

void fragment() {
	vec2 p = UV * 2.0 - 1.0;
	float r = length(p);
	float a = atan(p.y, p.x);
	float arms = 0.5 + 0.5 * sin(a * 3.0 + log(r + 0.02) * 7.0 + TIME * 2.5);
	float body = smoothstep(1.0, 0.35, r) * smoothstep(0.08, 0.2, r);
	float alpha = clamp(arms * 1.3 - 0.2, 0.0, 1.0) * body + smoothstep(0.26, 0.16, r) * smoothstep(0.08, 0.16, r) * 0.6;
	ALBEDO = vec3(1.0);
	ALPHA = clamp(alpha, 0.0, 1.0) * strength;
}
