#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(DrawPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec3 out_color;

void main() {
    Vertex vert = deref(push.vertices[gl_VertexIndex]);

    out_color = vert.color;
    gl_Position = f32vec4(vert.position, 0.0, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec3 in_color;

layout(location = 0) out f32vec4 color;

void main() {
    color = f32vec4(in_color, 1.0);
}

#endif