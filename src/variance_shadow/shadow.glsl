#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(ShadowPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

void main() {
    gl_Position = push.mvp * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) out f32vec2 out_color;

void main() {
    f32 depth = gl_FragCoord.z;

    f32 dx = dFdx(depth);
    f32 dy = dFdy(depth);
    f32 moment2 = depth * depth + 0.25 * (dx * dx + dy * dy);

    out_color = f32vec2(depth, moment2);
}

#endif