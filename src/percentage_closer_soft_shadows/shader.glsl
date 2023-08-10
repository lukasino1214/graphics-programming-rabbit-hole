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
    out_position_shadow = deref(push.light_buffer).projection_matrix * deref(push.light_buffer).view_matrix * deref(push.object_info).model_matrix * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
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

#define POISSON_DISK_SIZE 16

f32vec2 poisson_disk[POISSON_DISK_SIZE] = {
    f32vec2( -0.94201624, -0.39906216 ),
    f32vec2( 0.94558609, -0.76890725 ),
    f32vec2( -0.094184101, -0.92938870 ),
    f32vec2( 0.34495938, 0.29387760 ),
    f32vec2( -0.91588581, 0.45771432 ),
    f32vec2( -0.81544232, -0.87912464 ),
    f32vec2( -0.38277543, 0.27676845 ),
    f32vec2( 0.97484398, 0.75648379 ),
    f32vec2( 0.44323325, -0.97511554 ),
    f32vec2( 0.53742981, -0.47373420 ),
    f32vec2( -0.26496911, -0.41893023 ),
    f32vec2( 0.79197514, 0.19090188 ),
    f32vec2( -0.24188840, 0.99706507 ),
    f32vec2( -0.81409955, 0.91437590 ),
    f32vec2( 0.19984126, 0.78641367 ),
    f32vec2( 0.14383161, -0.14100790 ) 
};

f32 calculate_shadow(daxa_ImageViewId shadow_image, daxa_SamplerId shadow_sampler, f32vec3 shadow_coord, f32vec2 off, f32 bias) {
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
	i32 range = deref(push.light_buffer).pcf_range;
	
	for (i32 x = -range; x <= range; x++) {
		for (i32 y = -range; y <= range; y++) {
			shadow_factor += calculate_shadow(shadow_image, shadow_sampler, shadow_coord.xyz, f32vec2(dx*x, dy*y), bias);
			count++;
		}
	
	}
	return (shadow_factor / count);
}

#define LIGHT_NEAR 0.1 // <- it has to be same for the one use for projection matrix

f32 penumbra_size(f32 receiver, f32 blocker) {
    return (receiver - blocker) / blocker;
} 

f32 z_clip_to_eye(f32 d) {
    return LIGHT_NEAR + (128.0 - LIGHT_NEAR ) * d;
}

struct BlockerInfo {
    f32 average_blocker_depth;
    i32 number_blockers;
};

BlockerInfo find_blocker(f32vec3 shadow_coords, f32 receiver, daxa_ImageViewId shadow_image, daxa_SamplerId shadow_sampler) {
    f32 light_size = deref(push.light_buffer).light_size;
    f32 search_width = light_size * (receiver - LIGHT_NEAR) / receiver;

    BlockerInfo info;
    info.average_blocker_depth = 0.0;
    info.number_blockers = 0;

    for(u32 i = 0; i < POISSON_DISK_SIZE; i++) {
        f32vec3 proj_coord = f32vec3(shadow_coords.xy * 0.5 + 0.5 + poisson_disk[i] * search_width, shadow_coords.z);
        f32 depth = texture(daxa_sampler2D(shadow_image, shadow_sampler), proj_coord.xy).r;
        if(depth < receiver) {
            info.average_blocker_depth += depth;
            info.number_blockers++;
        }
    }

    info.average_blocker_depth /= info.number_blockers;

    return info;
}

float linearize_depth(float d,float zNear,float zFar) {
   // return zNear * zFar / (zFar + d * (zNear - zFar));
    return (2.0 * zNear) / (zFar + zNear - d * (zFar - zNear));	
}

f32 shadow_pcss(daxa_ImageViewId shadow_image, daxa_SamplerId shadow_sampler, f32vec4 shadow_coords, f32 bias) {
    f32 receiver = shadow_coords.z;
    f32 light_size = deref(push.light_buffer).light_size;

    BlockerInfo info = find_blocker(shadow_coords.xyz, receiver, shadow_image, shadow_sampler);
    
    if(info.number_blockers == 0) {
        return 1.0;
    }

    f32 penumbra_ratio = penumbra_size(receiver, z_clip_to_eye(info.average_blocker_depth));
    f32 filter_radius = penumbra_ratio * light_size * LIGHT_NEAR / receiver;

    f32 shadow_factor = 0.0;
    for (i32 x = 0; x < POISSON_DISK_SIZE; x++) {
        shadow_factor += calculate_shadow(shadow_image, deref(push.light_buffer).shadow_sampler, shadow_coords.xyz, poisson_disk[x] * filter_radius, bias);
	}

    return (shadow_factor / POISSON_DISK_SIZE);
}

void main() {

    f32vec3 color = f32vec4(sample_texture(MATERIAL.albedo_image, in_uv).rgb, 1.0).rgb;
    color *= push.shadow_intensity;
    f32 bias = deref(push.light_buffer).bias;

#if USE_PCSS == 0
    f32 shadow = shadow_pcf(deref(push.light_buffer).shadow_image, deref(push.light_buffer).shadow_sampler, in_position_shadow / in_position_shadow.w, bias);
#else
    f32 shadow = shadow_pcss(deref(push.light_buffer).shadow_image, deref(push.light_buffer).image_sampler, in_position_shadow / in_position_shadow.w, bias);
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