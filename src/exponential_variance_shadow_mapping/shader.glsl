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

f32vec2 warp_depth(f32 depth, f32vec2 exponents) {
    depth = 2.0f * depth - 1.0f;
    f32 pos =  exp( exponents.x * depth);
    f32 neg = -exp(-exponents.y * depth);
    return f32vec2(pos, neg);
}

f32 linstep(f32 a, f32 b, f32 v) {
    return clamp((v - a) / (b - a), 0.0, 1.0);
}

f32 reduce_light_bleeding(f32 pMax, f32 amount) {
   return linstep(amount, 1.0f, pMax);
}

f32 ChebyshevUpperBound(f32vec2 moments, f32 mean, f32 min_variance, f32 light_bleeding_reduction) {
    f32 variance = moments.y - (moments.x * moments.x);
    variance = max(variance, min_variance);

    f32 d = mean - moments.x;
    f32 pMax = variance / (variance + (d * d));

    pMax = reduce_light_bleeding(pMax, light_bleeding_reduction);

    return (mean <= moments.x ? 1.0f : pMax);
}

f32 calculate_shadow(daxa_ImageViewId shadow_image, daxa_SamplerId shadow_sampler, f32vec4 shadow_coord) {
    f32vec3 proj_coord = f32vec3(shadow_coord.xy * 0.5 + 0.5, shadow_coord.z);
    f32vec4 depth = texture(daxa_sampler2D(shadow_image, shadow_sampler), proj_coord.xy).xyzw;
    
    f32vec2 exponents = f32vec2(push.positive_exponential_factor, push.negative_exponential_factor);

    f32vec2 warped_depth = warp_depth(proj_coord.z, exponents);
    f32vec2 depth_scale = 0.01 * 0.01 * exponents * warped_depth;
    f32vec2 min_variance = depth_scale * depth_scale;

    f32 positive_contribution = ChebyshevUpperBound(depth.xz, warped_depth.x, min_variance.x, push.light_bleed);
    f32 negative_contribution = ChebyshevUpperBound(depth.yw, warped_depth.y, min_variance.y, push.light_bleed);
    f32 shadow = min(positive_contribution, negative_contribution);

    return clamp(pow(shadow, push.darkening_factor), 0.0, 1.0);
}

void main() {
    color = f32vec4(sample_texture(MATERIAL.albedo_image, in_uv).rgb, 1.0);
    color.rgb *= max(push.shadow_intensity, calculate_shadow(deref(push.light_buffer).shadow_image, deref(push.light_buffer).shadow_sampler, in_position_shadow / in_position_shadow.w));
}

#endif