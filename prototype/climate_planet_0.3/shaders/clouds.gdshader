shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_back;
#include "res://shaders/noise.gdshaderinc"

uniform vec4 cloud_color : source_color = vec4(1.0);
uniform float cover = 0.56;        // higher = fewer clouds
uniform float speed = 0.03;
uniform vec4 haze_color : source_color = vec4(0.85, 0.78, 0.45, 1.0);
uniform float haze = 0.0;          // smog / dust over the whole planet
uniform bool use_climate = false;
uniform sampler2D climate_tex : filter_linear, repeat_enable;
uniform sampler2D wind_tex : filter_linear, repeat_enable;   // RG = wind east/north, ±15 m/s
uniform float flow_speed = 0.008;   // radians per second per m/s (same as the wind streaks)
uniform float flow_cycle = 3.5;     // seconds before a flow layer resets (hidden by cross-fading);
                                    // keep flow_speed × flow_cycle × wind well below a cloud's size (~0.3 rad)

varying vec3 v_pos;

void vertex() {
	v_pos = VERTEX;
}

void fragment() {
	vec3 n = normalize(v_pos);
	float c;
	float cv = cover;
	if (use_climate) {
		// Flow map: shift the cloud pattern along the local wind. Two layers half a cycle apart are
		// cross-faded so the pattern moves continuously without stretching forever.
		vec2 uv = climate_uv(n);
		vec2 w = (texture(wind_tex, uv).rg * 2.0 - 1.0) * 15.0;
		vec3 east = normalize(cross(vec3(0.0, 1.0, 0.0), n) + vec3(1e-4, 0.0, 0.0));
		vec3 north = cross(n, east);
		vec3 disp = (east * w.x + north * w.y) * flow_speed * flow_cycle;
		float ph0 = fract(TIME / flow_cycle);
		float ph1 = fract(TIME / flow_cycle + 0.5);
		float w0 = 1.0 - abs(2.0 * ph0 - 1.0);
		float w1 = 1.0 - w0;
		float c0 = fbm((n - disp * ph0) * 3.2 + vec3(0.0, 0.0, 7.0));
		float c1 = fbm((n - disp * ph1) * 3.2 + vec3(3.1, 1.7, 7.0));
		// variance-preserving blend keeps cloud contrast steady during the cross-fade
		c = 0.5 + (c0 * w0 + c1 * w1 - 0.5) / sqrt(w0 * w0 + w1 * w1);
		cv = mix(0.7, 0.47, texture(climate_tex, uv).b);      // wetter, rising air → more cloud
	} else {
		float a = TIME * speed;
		vec3 q = vec3(n.x * cos(a) - n.z * sin(a), n.y, n.x * sin(a) + n.z * cos(a));
		c = fbm(q * 3.2 + vec3(0.0, 0.0, 7.0));
	}
	float puff = smoothstep(cv, cv + 0.07, c);
	ALBEDO = mix(haze_color.rgb, cloud_color.rgb, puff / max(puff + haze, 0.001));
	ALPHA = clamp(puff * 0.92 + haze, 0.0, 1.0);
	ROUGHNESS = 1.0;
	SPECULAR = 0.0;
}
