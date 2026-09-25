shader_type spatial;
// Land colours depend on height (distance from the centre), latitude and the climate uniforms.
#include "res://shaders/noise.gdshaderinc"

uniform float base_sea = 1.004;          // original coastline radius
uniform float sea_level = 1.004;         // current sea radius (rises with warming)
uniform float ice_lat = 0.72;            // |latitude| above which land is ice
uniform float snow_height = 1.07;        // radius above which mountains are snowy
uniform float rock_height = 1.055;
uniform float desert = 0.15;             // 0..1 share of dry land
uniform vec4 grass : source_color = vec4(0.45, 0.78, 0.30, 1.0);
uniform vec4 dry : source_color = vec4(0.91, 0.77, 0.45, 1.0);
uniform vec4 sand : source_color = vec4(0.96, 0.86, 0.56, 1.0);
uniform vec4 rock : source_color = vec4(0.61, 0.54, 0.47, 1.0);
uniform vec4 flooded : source_color = vec4(0.50, 0.84, 0.86, 1.0);
uniform vec4 seafloor : source_color = vec4(0.16, 0.36, 0.62, 1.0);

varying vec3 v_pos;

void vertex() {
	v_pos = VERTEX;
}

void fragment() {
	float r = length(v_pos);
	vec3 n = v_pos / r;
	float wob = (fbm(n * 7.0) - 0.5) * 0.08;
	vec3 col;
	if (r < base_sea) {
		col = mix(seafloor.rgb * 0.7, seafloor.rgb, smoothstep(base_sea - 0.04, base_sea, r));
	} else if (r < sea_level) {
		col = flooded.rgb;                                  // land drowned by sea-level rise
	} else if (r < sea_level + 0.0012) {
		col = sand.rgb;
	} else {
		float thr = mix(0.72, 0.4, desert);
		float dmask = smoothstep(thr - 0.04, thr + 0.04, fbm(n * 3.1 + 4.0));
		col = mix(grass.rgb, dry.rgb, dmask);
		col *= 0.92 + 0.16 * fbm(n * 22.0);                 // gentle texture
		float rk = smoothstep(rock_height, rock_height + 0.01, r + wob * 0.05);
		col = mix(col, rock.rgb, rk);
		float sn = smoothstep(snow_height, snow_height + 0.006, r + wob * 0.05);
		col = mix(col, vec3(0.97, 0.99, 1.0), sn);
	}
	if (abs(n.y) + wob > ice_lat && r >= base_sea - 0.01) {
		col = vec3(0.95, 0.98, 1.0);                        // land ice caps
	}
	ALBEDO = col;
	ROUGHNESS = 0.9;
	SPECULAR = 0.2;
}
