#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(ShadowPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

void main() {
    gl_Position = push.mvp * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) out f32vec4 out_color;

f32vec2 warp_depth(f32 depth, f32vec2 exponents) {
    depth = 2.0f * depth - 1.0f;
    f32 pos =  exp( exponents.x * depth);
    f32 neg = -exp(-exponents.y * depth);
    return f32vec2(pos, neg);
}

void main() {
    f32 depth = gl_FragCoord.z;

    f32vec2 new_depth = warp_depth(depth, f32vec2(push.positive_exponential_factor, push.negative_exponential_factor));
    out_color = f32vec4(new_depth.xy, new_depth.xy * new_depth.xy);
}

#endif