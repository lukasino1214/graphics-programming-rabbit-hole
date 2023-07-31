#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(DrawPush, push)

#define MATERIAL deref(push.materials[push.material_index])

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;
layout(location = 1) out f32vec4 out_position_shadow;
layout(location = 2) out f32vec3 out_position;
layout(location = 3) out f32vec3 out_normal;

void main() {
    out_uv = deref(push.vertices[gl_VertexIndex]).uv;
    out_position_shadow = deref(push.light_buffer).light_matrix * deref(push.object_info).model_matrix * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
    out_normal = normalize(f32mat3x3(deref(push.object_info).normal_matrix) * deref(push.vertices[gl_VertexIndex]).normal);
    out_position = (deref(push.object_info).model_matrix * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0)).xyz;
    gl_Position = push.mvp * deref(push.object_info).model_matrix * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;
layout(location = 1) in f32vec4 in_position_shadow;
layout(location = 2) in f32vec3 in_position;
layout(location = 3) in f32vec3 in_normal;

layout(location = 0) out f32vec4 out_color;

f32 calculate_shadow(daxa_ImageViewId shadow_image, daxa_SamplerId shadow_sampler, f32vec4 shadow_coord, f32vec2 off, f32 bias) {
    f32vec3 proj_coord = f32vec3(shadow_coord.xy * 0.5 + 0.5 + off, shadow_coord.z - bias);
    if(shadow_coord.z > 1.0) { return 1.0; }
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

    f32vec3 color = f32vec4(sample_texture(MATERIAL.albedo_image, in_uv).rgb, 1.0).rgb;
    color *= 0.1;
#if USE_PCF == 0
    f32 shadow = max(calculate_shadow(deref(push.light_buffer).shadow_image, deref(push.light_buffer).shadow_sampler, in_position_shadow / in_position_shadow.w, f32vec2(0.0, 0.0), push.bias), push.shadow_intensity);
#else
    f32 shadow = max(shadow_pcf(deref(push.light_buffer).shadow_image, deref(push.light_buffer).shadow_sampler, in_position_shadow / in_position_shadow.w, push.bias), push.shadow_intensity);
#endif

    f32vec3 light_position = deref(push.light_buffer).position;
    f32vec3 light_direction = normalize(deref(push.light_buffer).direction);
    f32 light_inner_cut_off = deref(push.light_buffer).inner_cut_off;
    f32 light_outer_cut_off = deref(push.light_buffer).outer_cut_off;
    f32 light_intensity = deref(push.light_buffer).intensity;

    f32vec3 light_dir = normalize(light_position - in_position);

    f32 theta = dot(light_dir, normalize(-light_direction)); 
    f32 epsilon = (light_inner_cut_off - light_outer_cut_off);
    f32 intensity = clamp((theta - light_outer_cut_off) / epsilon, 0, 1.0);

    f32 distance = length(light_position - in_position);
    f32 attenuation = 1.0 / (distance * distance); 

    f32vec3 view_dir = normalize(push.camera_position - in_position);
    f32vec3 halfway_dir = normalize(light_dir + view_dir);

    f32 diffuse = max(dot(normalize(in_normal), light_dir), 0);
    f32 normal_half = acos(dot(halfway_dir, normalize(in_normal)));
    f32 exponent = normal_half / 1.0;
    exponent = -(exponent * exponent);
    out_color = f32vec4(color + color * f32vec3(1.0) * ((diffuse + exp(exponent)) * shadow) * attenuation * light_intensity * intensity, 1.0);
}

#endif