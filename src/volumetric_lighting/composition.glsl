#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(CompositionPush, push)

#define MATERIAL deref(push.materials[push.material_index])

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;

void main() {
    out_uv = f32vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = f32vec4(out_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;

layout(location = 0) out f32vec4 color;

const i32 NUM_STEPS_INT = 15;
const f32 NUM_STEPS = f32(NUM_STEPS_INT);
const f32 G = 0.7f;
const f32 PI = 3.14159265359f;
const mat4 DITHER_PATTERN = mat4
    (f32vec4(0.0f, 0.5f, 0.125f, 0.625f),
     f32vec4(0.75f, 0.22f, 0.875f, 0.375f),
     f32vec4(0.1875f, 0.6875f, 0.0625f, 0.5625f),
     f32vec4(0.9375f, 0.4375f, 0.8125f, 0.3125f));

f32 calculate_scattering(f32 cos_theta) {
    return (1.0 - G * G) / (4.0 * PI * pow(1.0 + G * G - 2.0 * G * cos_theta, 1.5));
}

f32 calculate_shadow(daxa_ImageViewId shadow_image, daxa_SamplerId shadow_sampler, f32vec4 shadow_coord, f32vec2 off, f32 bias) {
    f32vec3 proj_coord = f32vec3(shadow_coord.xy * 0.5 + 0.5 + off, shadow_coord.z - bias);
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

f32vec3 get_world_position_from_depth(f32vec2 uv, f32 depth) {
    f32vec4 clipSpacePosition = f32vec4(uv * 2.0 - 1.0, depth, 1.0);
    f32vec4 viewSpacePosition = deref(push.matrices).inverse_projection_matrix * clipSpacePosition;

    viewSpacePosition /= viewSpacePosition.w;
    f32vec4 worldSpacePosition = deref(push.matrices).inverse_view_matrix * viewSpacePosition;

    return worldSpacePosition.xyz;
}

void main() {
    f32vec3 normal = normalize(texture(daxa_sampler2D(push.normal_image, push.sampler_id), in_uv).rgb); 
    f32vec3 frag_position = get_world_position_from_depth(in_uv, texture(daxa_sampler2D(push.depth_image, push.sampler_id), in_uv).r);
    f32vec4 position_shadow = deref(push.light_buffer).light_matrix * f32vec4(frag_position, 1.0);

    vec3 fixed_camera_pos = vec3(push.camera_position);

    vec4 camera_in_shadow_space = deref(push.light_buffer).light_matrix * vec4(push.camera_position, 1.0f);
    vec4 clip_position = deref(push.light_buffer).light_matrix * vec4(frag_position, 1.0f);
    
    vec3 ray_vector = clip_position.xyz - camera_in_shadow_space.xyz;
    float ray_length = length(ray_vector);
    vec3 ray_direction = ray_vector / ray_length;

    float step_length = ray_length / float(NUM_STEPS);

    vec3 r_step = ray_direction * step_length;

    float accum_fog = 0.0;

    float dither_value = DITHER_PATTERN[i32(gl_FragCoord.x) % 4][i32(gl_FragCoord.y) % 4];
    for(int i = 0; i < NUM_STEPS_INT; i++) {
        vec3 clip_space_step = camera_in_shadow_space.xyz + r_step * float(i) + dither_value * r_step;
        vec3 proj_coord = vec3(clip_space_step.xy * 0.5f + 0.5f, clip_space_step.z);
        float depth = texture(daxa_sampler2DShadow(deref(push.light_buffer).shadow_image, deref(push.light_buffer).shadow_sampler), proj_coord).r;
        accum_fog += depth;
    }

    vec3 V = normalize(frag_position - push.camera_position);
    f32vec3 volumetric = vec3((accum_fog / NUM_STEPS) * calculate_scattering(dot(V, -normalize(deref(push.light_buffer).light_direction))));

    f32vec3 albedo = texture(daxa_sampler2D(push.albedo_image, push.sampler_id), in_uv).rgb;
    f32vec3 ambient = f32vec3(push.shadow_intensity);

#if USE_PCF == 0
    f32vec3 direct = f32vec3(calculate_shadow(
        deref(push.light_buffer).shadow_image, 
        deref(push.light_buffer).shadow_sampler, 
        position_shadow / position_shadow.w, 
        f32vec2(0.0, 0.0), 
        push.bias));
#else 
    f32vec3 direct = f32vec3(shadow_pcf(
        deref(push.light_buffer).shadow_image, 
        deref(push.light_buffer).shadow_sampler, 
        position_shadow / position_shadow.w, 
        push.bias));
#endif

    f32vec3 indirect = f32vec3(0.0);

    color = f32vec4((direct * max(0.0, dot(normal, -deref(push.light_buffer).light_direction)) + indirect + ambient) * albedo + volumetric, 1.0);
}

#endif