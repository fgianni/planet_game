shader_type canvas_item;
// One step of the persistent cloud field (latitude–longitude texture, R = cloud density).
// Each pixel looks back upwind (semi-Lagrangian advection) and takes the density from where the air
// came from. Winds = the climate model's winds + slowly evolving eddies (visual weather: depressions,
// fronts) so clouds stretch, split and merge. Clouds form where the model says air rises and is
// humid (climate B channel) and evaporate where it sinks and is dry.
#include "res://shaders/noise.gdshaderinc"

uniform sampler2D prev : filter_linear, repeat_enable;
uniform sampler2D wind_tex : filter_linear, repeat_enable;     // RG: wind east/north, ±15 m/s
uniform sampler2D climate_tex : filter_linear, repeat_enable;  // B: cloudiness from the model
uniform bool init = false;
uniform float dt = 0.05;
uniform float time = 0.0;
uniform float vis_speed = 0.008;     // radians per second per m/s (matches wind streaks)
uniform float eddy_speed = 7.0;      // m/s, strength of the weather eddies
uniform float form_rate = 0.35;      // how fast new cloud forms where the climate allows it
uniform float decay_rate = 0.45;     // how fast cloud evaporates where air sinks / is dry
uniform float sharpen = 0.25;        // keeps cloud edges crisp against numerical blurring

vec3 dir_from_uv(vec2 uv) {
	float lon = uv.x * TAU - PI;
	float lat = PI * 0.5 - uv.y * PI;
	return vec3(cos(lat) * sin(lon), sin(lat), cos(lat) * cos(lon));
}

float eddy_psi(vec3 p, float t) {
	return vnoise(p * 4.0 + vec3(0.0, t * 0.035, 11.0)) + 0.5 * vnoise(p * 9.0 + vec3(t * 0.05, 3.0, 0.0));
}

// Divergence-free eddy velocity (east, north) = curl of a noise streamfunction on the sphere.
vec2 eddies(vec3 n, float lat, float t) {
	vec3 e = normalize(cross(vec3(0.0, 1.0, 0.0), n) + vec3(1e-4, 0.0, 0.0));
	vec3 no = cross(n, e);
	float h = 0.01;
	float dx = (eddy_psi(normalize(n + e * h), t) - eddy_psi(normalize(n - e * h), t)) / (2.0 * h);
	float dy = (eddy_psi(normalize(n + no * h), t) - eddy_psi(normalize(n - no * h), t)) / (2.0 * h);
	// strongest in the storm tracks (~50°), gentler in the steady trade winds
	float a = abs(degrees(lat));
	float env = mix(0.35, 1.0, exp(-pow((a - 50.0) / 16.0, 2.0)));
	return vec2(-dy, dx) * (eddy_speed * env / 6.0);
}

void fragment() {
	vec2 uv = UV;
	float lat = PI * 0.5 - uv.y * PI;
	vec3 n = dir_from_uv(uv);
	vec4 clim = texture(climate_tex, uv);
	float target = clim.b;
	float seed = fbm(n * 9.0 + vec3(time * 0.021, -time * 0.013, time * 0.017));   // many smaller cloud cells
	float d;
	if (init) {
		d = smoothstep(0.3, 0.8, target) * smoothstep(0.45, 0.62, seed);
	} else {
		vec2 w = (texture(wind_tex, uv).rg * 2.0 - 1.0) * 15.0 + eddies(n, lat, time);
		float dlon = w.x * vis_speed * dt / max(cos(lat), 0.12);
		float dlat = w.y * vis_speed * dt;
		vec2 src = vec2(uv.x - dlon / TAU, clamp(uv.y + dlat / PI, 0.001, 0.999));
		d = texture(prev, src).r;
		float form = form_rate * smoothstep(0.3, 0.75, target) * smoothstep(0.5, 0.66, seed);
		float decay = decay_rate * (1.0 - smoothstep(0.2, 0.55, target)) + 0.03;
		float change = (form * (1.0 - d) - decay * d) * dt;
		// never let a change vanish below 8-bit precision, so thin cloud can still fade away
		if (change < 0.0) { change = min(change, -0.6 / 255.0 * step(0.004, d)); }
		d = clamp(d + change, 0.0, 1.0);
		d = mix(d, smoothstep(0.05, 0.95, d), clamp(sharpen * dt, 0.0, 1.0));   // gentle anti-diffusion
	}
	COLOR = vec4(d, d, d, 1.0);
}
