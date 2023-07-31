#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(ShadowPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

void main() {
    gl_Position = push.mvp * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

void main() {}

#endif