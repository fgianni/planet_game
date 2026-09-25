shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back;
#include "res://shaders/noise.gdshaderinc"

uniform vec4 deep : source_color = vec4(0.10, 0.37, 0.71, 1.0);
uniform vec4 shallow : source_color = vec4(0.22, 0.64, 0.92, 1.0);
uniform float ice_lat = 0.72;       // sea ice above this |latitude|
uniform float opacity = 0.8;
uniform bool use_climate = false;
uniform sampler2D climate_tex : filter_linear, repeat_enable;

varying vec3 v_pos;

void vertex() {
	v_pos = VERTEX;
}

void fragment() {
	vec3 n = normalize(v_pos);
	float fres = pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 2.5);
	float ripple = fbm(n * 40.0 + vec3(TIME * 0.05, 0.0, TIME * 0.03));
	vec3 col = mix(deep.rgb, shallow.rgb, 0.35 + 0.5 * fres) + (ripple - 0.5) * 0.05;
	float ice;
	if (use_climate) {
		float a = texture(climate_tex, climate_uv(n)).a + (fbm(n * 9.0) - 0.5) * 0.25;
		ice = smoothstep(0.45, 0.55, a);
	} else {
		float edge = ice_lat + 0.03 + (fbm(n * 9.0) - 0.5) * 0.06;
		ice = smoothstep(edge, edge + 0.01, abs(n.y));
	}
	ALBEDO = mix(col, vec3(0.93, 0.97, 1.0), ice);
	ALPHA = mix(opacity, 1.0, ice);
	ROUGHNESS = mix(0.12, 0.7, ice);
	SPECULAR = 0.6;
}
