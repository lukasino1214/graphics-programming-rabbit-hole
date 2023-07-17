#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(DrawPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

void main() {
    Vertex vert = deref(push.vertices[gl_VertexIndex]);
    gl_Position = f32vec4(vert.position, 0.0, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) out vec4 color;
layout(location = 1) out vec4 bloom;

void main() {
    color = vec4(0.2, 0.5, 1.0, 1.0);
    bloom = vec4(0.2, 0.5, 1.0, 1.0);
}

#endif