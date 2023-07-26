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

f32 linstep(f32 low, f32 high, f32 v) {
    return clamp((v-low)/(high-low), 0.0, 1.0);
}

f32 variance_shadow(daxa_ImageViewId shadow_image, daxa_SamplerId image_sampler, f32vec4 shadow_coord) {
    f32vec3 proj_coord = shadow_coord.xyz * 0.5 + 0.5;
	
    f32vec2 moments = texture(daxa_sampler2D(shadow_image, image_sampler), proj_coord.xy).xy;
    f32 p = step(shadow_coord.z, moments.x);
    f32 variance = max(moments.y - moments.x * moments.x, 0.00002);
	f32 d = shadow_coord.z - moments.x;
	f32 pMax = linstep(0.1, 1.0, variance / (variance + d*d));

    return max(p, pMax);
}

void main() {

    color = f32vec4(sample_texture(MATERIAL.albedo_image, in_uv).rgb, 1.0);
    color.rgb *= max(push.shadow_intensity, variance_shadow(deref(push.light_buffer).shadow_image, deref(push.light_buffer).shadow_sampler, in_position_shadow / in_position_shadow.w));
}

#endif