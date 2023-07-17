#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(DrawPush, push)

#define MATERIAL deref(push.materials[push.material_index])

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;
layout(location = 1) out f32vec4 out_position_shadow;

void main() {
    out_uv = deref(push.vertices[gl_VertexIndex]).uv;
    out_position_shadow = deref(push.light_buffer).light_matrix * vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
    gl_Position = push.mvp * vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;
layout(location = 1) in f32vec4 in_position_shadow;

layout(location = 0) out f32vec4 color;

float calculate_shadow(daxa_ImageViewId shadow_image, daxa_SamplerId shadow_sampler, f32vec4 shadow_coord, f32vec2 off, f32 bias) {
    vec3 proj_coord = vec3(shadow_coord.xy * 0.5 + 0.5 + off, shadow_coord.z - bias);
	return texture(daxa_sampler2DShadow(shadow_image, shadow_sampler), proj_coord.xyz).r;
}

f32 shadow_pcf(daxa_ImageViewId shadow_image, daxa_SamplerId shadow_sampler, f32vec4 shadow_coord, f32 bias) {
    i32vec2 tex_dim = textureSize(daxa_sampler2DShadow(shadow_image, shadow_sampler), 0);
	f32 scale = 0.25;
	f32 dx = scale * 1.0 / f32(tex_dim.x);
	f32 dy = scale * 1.0 / f32(tex_dim.y);

	f32 shadow_factor = 0.0;
	i32 count = 0;
	i32 range = push.pcf_range;
	
	for (i32 x = -range; x <= range; x++) {
		for (i32 y = -range; y <= range; y++) {
			shadow_factor += calculate_shadow(shadow_image, shadow_sampler, shadow_coord, f32vec2(dx*x, dy*y), bias);
			count++;
		}
	
	}
	return (shadow_factor / count);
}

void main() {

    color = f32vec4(sample_texture(MATERIAL.albedo_image, in_uv).rgb, 1.0);
#if USE_PCF == 0
    color.rgb *= max(calculate_shadow(
        deref(push.light_buffer).shadow_image, 
        deref(push.light_buffer).shadow_sampler, 
        in_position_shadow / in_position_shadow.w, 
        vec2(0.0, 0.0), 
        push.bias)
    , push.shadow_intensity);
#else
    color.rgb *= max(shadow_pcf(
        deref(push.light_buffer).shadow_image, 
        deref(push.light_buffer).shadow_sampler, 
        in_position_shadow / in_position_shadow.w, 
        push.bias)
    , push.shadow_intensity);
#endif

    //color.rgb *= 0.1;
    //color = f32vec4(vec3(calculate_shadow(deref(push.light_buffer).shadow_image, deref(push.light_buffer).shadow_sampler, in_position_shadow / in_position_shadow.w), vec2(0.0, 0.0), push.bias), 1.0);
}

#endif