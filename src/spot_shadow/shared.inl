#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

#include "../common.inl"

struct LightInfo {
    f32mat4x4 light_matrix;
    daxa_ImageViewId shadow_image;
    daxa_SamplerId shadow_sampler;
    f32vec3 position;
    f32vec3 direction;
    f32 inner_cut_off;
    f32 outer_cut_off;
};

DAXA_DECL_BUFFER_PTR(LightInfo)

struct ObjectInfo {
    f32mat4x4 model_matrix;
    f32mat4x4 normal_matrix;
};

DAXA_DECL_BUFFER_PTR(ObjectInfo)

struct ShadowPush {
    f32mat4x4 mvp;
    daxa_BufferPtr(ObjectInfo) object_info;
    daxa_BufferPtr(Vertex) vertices;
};

struct DrawPush {
    f32mat4x4 mvp;
    daxa_BufferPtr(ObjectInfo) object_info;
    daxa_BufferPtr(Vertex) vertices;
    daxa_BufferPtr(Material) materials;
    u32 material_index;
    daxa_BufferPtr(LightInfo) light_buffer;
    f32 bias;
    int pcf_range;
    float shadow_intensity;
    f32vec3 camera_position;
};