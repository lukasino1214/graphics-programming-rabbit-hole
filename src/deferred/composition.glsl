#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(CompositionPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out vec2 out_uv;

void main() {
    out_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(out_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 color;

void main() {
    color = f32vec4(texture(daxa_sampler2D(push.albedo_image, push.sampler_id), in_uv).rgb, 1.0f);
    //color = f32vec4(texture(daxa_sampler2D(push.normal_image, push.sampler_id), in_uv).rgb  * 0.5 + 0.5, 1.0f); // to see normals
}

#endif