#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(DrawPush, push)

#define MATERIAL deref(push.materials[push.material_index])

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;
layout(location = 1) out f32vec4 out_position_shadow;

void main() {
    out_uv = deref(push.vertices[gl_VertexIndex]).uv;
    out_position_shadow = deref(push.light_buffer).light_matrix * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
    gl_Position = push.mvp * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;
layout(location = 1) in f32vec4 in_position_shadow;

layout(location = 0) out f32vec4 color;

f32 calculate_shadow(daxa_ImageViewId shadow_image, daxa_SamplerId shadow_sampler, f32vec4 shadow_coord, f32vec2 off, f32 bias) {
    f32vec3 proj_coord = f32vec3(shadow_coord.xy * 0.5 + 0.5 + off, shadow_coord.z - bias);
    f32 depth = texture(daxa_sampler2D(shadow_image, shadow_sampler), proj_coord.xy).r;
    
    // f32 ex_d = exp(-200 * proj_coord.z);
    // f32 ex_o = exp(200 * depth);

    return clamp(pow(exp(push.exponential_factor * (proj_coord.z - depth)), push.darkening_factor), 0.0, 1.0);
    //return clamp(ex_d * ex_o, 0.0, 1.0);
}

void main() {
    color = f32vec4(sample_texture(MATERIAL.albedo_image, in_uv).rgb, 1.0);
    color.rgb *= max(calculate_shadow(
        deref(push.light_buffer).shadow_image, 
        deref(push.light_buffer).shadow_sampler, 
        in_position_shadow / in_position_shadow.w, 
        f32vec2(0.0, 0.0), 
        push.bias)
    , push.shadow_intensity);
}

#endif