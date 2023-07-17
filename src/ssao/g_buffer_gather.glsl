#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(GBufferGatherPush, push)

#define MATERIAL deref(push.materials[push.material_index])
#define CAMERA deref(push.camera_info)
#define TRANSFORM deref(push.object_info)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;
layout(location = 1) out f32vec3 out_normal;

void main() {
    out_uv = deref(push.vertices[gl_VertexIndex]).uv;
    out_normal = normalize(f32mat3x3(TRANSFORM.normal_matrix) * deref(push.vertices[gl_VertexIndex]).normal);
    gl_Position = CAMERA.projection_matrix * CAMERA.view_matrix * TRANSFORM.model_matrix * vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;
layout(location = 1) in f32vec3 in_normal;

layout(location = 0) out f32vec4 out_albedo;
layout(location = 1) out f32vec4 out_normal;

void main() {
    out_albedo = f32vec4(sample_texture(MATERIAL.albedo_image, in_uv).rgb, 1.0);
    out_normal = f32vec4(normalize(in_normal), 1.0);
}

#endif