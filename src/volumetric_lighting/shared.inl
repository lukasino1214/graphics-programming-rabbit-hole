#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

#include "../common.inl"

struct LightInfo {
    f32mat4x4 light_matrix;
    daxa_ImageViewId shadow_image;
    daxa_SamplerId shadow_sampler;
    f32vec3 light_direction;
};

DAXA_DECL_BUFFER_PTR(LightInfo)

struct MatricesBuffer {
    f32mat4x4 mvp;
    f32mat4x4 model_matrix;
    f32mat4x4 normal_matrix;
    f32mat4x4 inverse_projection_matrix;
    f32mat4x4 inverse_view_matrix;
};

DAXA_DECL_BUFFER_PTR(MatricesBuffer)

struct GBufferGatherPush {
    daxa_BufferPtr(MatricesBuffer) matrices;
    daxa_BufferPtr(Vertex) vertices;
    daxa_BufferPtr(Material) materials;
    u32 material_index;
};

struct ShadowPush {
    f32mat4x4 mvp;
    daxa_BufferPtr(Vertex) vertices;
    daxa_BufferPtr(MatricesBuffer) matrices;
};

struct CompositionPush {
    daxa_ImageViewId albedo_image;
    daxa_ImageViewId normal_image;
    daxa_ImageViewId depth_image;
    daxa_SamplerId sampler_id;
    daxa_BufferPtr(MatricesBuffer) matrices;
    daxa_BufferPtr(LightInfo) light_buffer;
    f32 bias;
    i32 pcf_range;
    f32 shadow_intensity;
    f32vec3 camera_position;
};