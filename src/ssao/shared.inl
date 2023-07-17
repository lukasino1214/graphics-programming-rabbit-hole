#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

#include "../common.inl"

struct CameraInfo {
    f32mat4x4 projection_matrix;
    f32mat4x4 inverse_projection_matrix;
    f32mat4x4 view_matrix;
    f32mat4x4 inverse_view_matrix;
    f32vec3 position;
};

DAXA_DECL_BUFFER_PTR(CameraInfo)

struct ObjectInfo {
    f32mat4x4 model_matrix;
    f32mat4x4 normal_matrix;
};

DAXA_DECL_BUFFER_PTR(ObjectInfo)

struct GBufferGatherPush {
    daxa_BufferPtr(CameraInfo) camera_info;
    daxa_BufferPtr(ObjectInfo) object_info;
    daxa_BufferPtr(Vertex) vertices;
    daxa_BufferPtr(Material) materials;
    u32 material_index;
};

struct CompositionPush {
    daxa_ImageViewId albedo_image;
    daxa_ImageViewId normal_image;
    daxa_ImageViewId depth_image;
    daxa_ImageViewId ssao_image;
    daxa_SamplerId sampler_id;
    daxa_RWBufferPtr(CameraInfo) camera_info;
    f32 ssao_strength;
};

struct SSAOGenerationPush {
    daxa_ImageViewId normal;
    daxa_ImageViewId depth;
    daxa_SamplerId sampler_id;
    daxa_RWBufferPtr(CameraInfo) camera_info;
    f32 bias;
    f32 radius;
    i32 kernel_size;
    TextureId noise_texture;
};

struct SSAOBlurPush {
    daxa_ImageViewId ssao;
    daxa_SamplerId sampler_id;
};
