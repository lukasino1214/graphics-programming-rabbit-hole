#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(DrawPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;

void main() {
    Vertex vert = deref(push.vertices[gl_VertexIndex]);

    out_uv = vert.uv;
    gl_Position = f32vec4(vert.position, 0.0, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;

layout(location = 0) out f32vec4 color;

void main() {
    color = f32vec4(texture(daxa_sampler2D(push.image_view_id, push.sampler_id), in_uv).rgb, 1.0);
}

#endif