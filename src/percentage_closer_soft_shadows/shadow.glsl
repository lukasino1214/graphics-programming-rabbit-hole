#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(ShadowPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

void main() {
    gl_Position = deref(push.light_buffer).projection_matrix * deref(push.light_buffer).view_matrix * deref(push.object_info).model_matrix * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

void main() {}

#endif