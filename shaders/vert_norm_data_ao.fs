#version 330 core

in VS_OUT {
	vec3 vert_color;
	vec3 normal;
	vec4 frag_pos_lightspace;
} vs_in;

out vec4 color;
uniform vec3 dir_light;
uniform sampler2D shadow_map;

float ShadowCalc(vec4 frag) {
	vec3 xyz = frag.xyz / frag.w;
	xyz = xyz * 0.5 + 0.5;
	vec2 texel_size = textureSize(shadow_map, 0);
	
	int num_in_shadow = 0, num_samples = 0;
	
	
	for (int dy=-1; dy<=1; dy++) {
		for (int dx=-1; dx<=1; dx++) {		
			float closest_depth = texture(shadow_map,
				xyz.xy + vec2(dx, dy)*1.5 / texel_size).r;
			float curr_depth    = xyz.z;
			
			const float bias = 0.001f;
			num_in_shadow += curr_depth - bias > closest_depth ? 1 : 0;
			num_samples += 1;
		}
	}
	
	float shadow = num_in_shadow * 1.0f / num_samples;
	return shadow;
}

void main()
{
	float shadow   = ShadowCalc(vs_in.frag_pos_lightspace);
	float strength = dot(-dir_light, vs_in.normal);
	strength = strength * 0.2f + 0.8f - 0.2f * shadow;
    color = vec4(vs_in.vert_color * strength, 1.0f);
    //color = vec4(vs_in.normal, 1.0f);
}
