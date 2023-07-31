#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(CompositionPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;

void main() {
    out_uv = f32vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = f32vec4(out_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;

layout(location = 0) out f32vec4 color;

void main() {
    color = f32vec4(
        texture(daxa_sampler2D(push.render_image_view_id, push.sampler_id), in_uv).rgb
        + texture(daxa_sampler2D(push.bloom_image_view_id, push.sampler_id), in_uv).rgb * push.bloom_strength, 
        1.0
    );
}

#endif