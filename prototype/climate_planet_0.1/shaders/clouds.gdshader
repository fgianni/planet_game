shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_back;
#include "res://shaders/noise.gdshaderinc"

uniform vec4 cloud_color : source_color = vec4(1.0);
uniform float cover = 0.56;        // higher = fewer clouds
uniform float speed = 0.03;
uniform vec4 haze_color : source_color = vec4(0.85, 0.78, 0.45, 1.0);
uniform float haze = 0.0;          // smog / dust over the whole planet

varying vec3 v_pos;

void vertex() {
	v_pos = VERTEX;
}

void fragment() {
	vec3 n = normalize(v_pos);
	float a = TIME * speed;
	vec3 q = vec3(n.x * cos(a) - n.z * sin(a), n.y, n.x * sin(a) + n.z * cos(a));
	float c = fbm(q * 3.2 + vec3(0.0, 0.0, 7.0));
	float puff = smoothstep(cover, cover + 0.07, c);
	ALBEDO = mix(haze_color.rgb, cloud_color.rgb, puff / max(puff + haze, 0.001));
	ALPHA = clamp(puff * 0.92 + haze, 0.0, 1.0);
	ROUGHNESS = 1.0;
	SPECULAR = 0.0;
}
