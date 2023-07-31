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

f32vec3 get_world_position_from_depth(f32vec2 uv, f32 depth) {
    f32vec4 clipSpacePosition = f32vec4(uv * 2.0 - 1.0, depth, 1.0);
    f32vec4 viewSpacePosition = deref(push.camera_info).inverse_projection_matrix * clipSpacePosition;

    viewSpacePosition /= viewSpacePosition.w;
    f32vec4 worldSpacePosition = deref(push.camera_info).inverse_view_matrix * viewSpacePosition;

    return worldSpacePosition.xyz;
}

f32vec3 get_view_position_from_depth(f32vec2 uv, f32 depth) {
    f32vec4 clipSpacePosition = f32vec4(uv * 2.0 - 1.0, depth, 1.0);
    f32vec4 viewSpacePosition = deref(push.camera_info).inverse_projection_matrix * clipSpacePosition;

    viewSpacePosition /= viewSpacePosition.w;

    return viewSpacePosition.xyz;
}

void main() {
    //color = f32vec4(get_world_position_from_depth(in_uv, texture(daxa_sampler2D(push.depth_image, push.sampler_id), in_uv).r), 1.0);
    //color = f32vec4(f32vec3(texture(daxa_sampler2D(push.depth_image, push.sampler_id), in_uv).r), 1.0);

#if DEBUG_SSAO == 0
    color = f32vec4(texture(daxa_sampler2D(push.albedo_image, push.sampler_id), in_uv).rgb, 1.0f);
    color.rgb *= pow(texture(daxa_sampler2D(push.ssao_image, push.sampler_id), in_uv).r, push.ssao_strength); // apply ssao
#else
    color = f32vec4(pow(texture(daxa_sampler2D(push.ssao_image, push.sampler_id), in_uv).r, push.ssao_strength));
#endif
   
    //color = f32vec4(texture(daxa_sampler2D(push.normal_image, push.sampler_id), in_uv).rgb  * 0.5 + 0.5, 1.0f); // to see normals
    //color = f32vec4(texture(daxa_sampler2D(push.ssao_image, push.sampler_id), in_uv).r);
}

#endif