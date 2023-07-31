#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

#include "../common.inl"

struct LightInfo {
    f32mat4x4 projection_matrix;
    f32mat4x4 inverse_projection_matrix;
    f32mat4x4 view_matrix;
    f32mat4x4 inverse_view_matrix;
    daxa_ImageViewId shadow_depth_image;
    daxa_ImageViewId shadow_normal_image;
    daxa_ImageViewId shadow_flux_image;
    daxa_SamplerId shadow_sampler;
    daxa_SamplerId image_sampler;
    f32vec3 light_direction;
};

DAXA_DECL_BUFFER_PTR(LightInfo)

struct ModelInfo {
    f32mat4x4 model_matrix;
    f32mat4x4 normal_matrix;
};

DAXA_DECL_BUFFER_PTR(ModelInfo)

struct ShadowPush {
    daxa_BufferPtr(LightInfo) light_buffer;
    daxa_BufferPtr(ModelInfo) model_buffer;
    daxa_BufferPtr(Vertex) vertices;
    daxa_BufferPtr(Material) materials;
    u32 material_index;
};

struct DrawPush {
    f32mat4x4 mvp;
    daxa_BufferPtr(ModelInfo) model_buffer;
    daxa_BufferPtr(Vertex) vertices;
    daxa_BufferPtr(Material) materials;
    u32 material_index;
    daxa_BufferPtr(LightInfo) light_buffer;
    f32 bias;
    i32 pcf_range;
    f32 shadow_intensity;
    f32 gi_intensity;
    f32 gi_radius;
};