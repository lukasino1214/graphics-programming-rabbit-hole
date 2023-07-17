#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(DrawPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

void main() {
    gl_Position = push.mvp * vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec3 in_color;

layout(location = 0) out f32vec4 color;

void main() {
    color = f32vec4(1.0);
}

#endif