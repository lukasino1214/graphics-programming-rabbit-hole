#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(ShadowPush, push)

#define MATERIAL deref(push.materials[push.material_index])

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;
layout(location = 1) out f32vec3 out_normal;

void main() {
    out_uv = deref(push.vertices[gl_VertexIndex]).uv;
    out_normal = normalize(f32mat3x3(deref(push.model_buffer).normal_matrix) * deref(push.vertices[gl_VertexIndex]).normal);
    gl_Position = deref(push.light_buffer).projection_matrix * deref(push.light_buffer).view_matrix * deref(push.model_buffer).model_matrix * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;
layout(location = 1) in f32vec3 in_normal;

layout(location = 0) out f32vec4 out_normal;
layout(location = 1) out f32vec4 out_flux;

void main() {
    out_normal = f32vec4(normalize(in_normal), 1.0);
    out_flux = f32vec4(sample_texture(MATERIAL.albedo_image, in_uv).rgb * max(0.0, dot(out_normal.rgb, -deref(push.light_buffer).light_direction)), 1.0);
}

#endif