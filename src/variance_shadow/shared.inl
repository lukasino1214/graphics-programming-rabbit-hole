#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

#include "../common.inl"

struct LightInfo {
    f32mat4x4 light_matrix;
    daxa_ImageViewId shadow_image;
    daxa_SamplerId shadow_sampler;
};

DAXA_DECL_BUFFER_PTR(LightInfo)

struct ShadowPush {
    f32mat4x4 mvp;
    daxa_BufferPtr(Vertex) vertices;
};

struct DrawPush {
    f32mat4x4 mvp;
    daxa_BufferPtr(Vertex) vertices;
    daxa_BufferPtr(Material) materials;
    u32 material_index;
    daxa_BufferPtr(LightInfo) light_buffer;
    f32 bias;
    int pcf_range;
    float shadow_intensity;
};

struct GaussPush {
    daxa_ImageViewId src_image;
    daxa_SamplerId image_sampler;
    f32vec2 blur_scale;
};