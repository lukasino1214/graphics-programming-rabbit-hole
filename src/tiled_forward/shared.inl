#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

#include "../common.inl"

#define TILE_SIZE 32
#define NUM_LIGHTS 1024

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

struct PointLight {
    f32vec3 position;
    u32 kys;
    f32vec3 color;
    u32 kys1;
    f32 radius;
    u32 kys2;
    u32 kys3;
    u32 kys4;
};

DAXA_DECL_BUFFER_PTR(PointLight)

struct PointLightIndex {
    u32 index;
    u32 kys;
    u32 kys1;
    u32 kys2;
};

DAXA_DECL_BUFFER_PTR(PointLightIndex)

struct DepthPrepassPush {
    daxa_BufferPtr(CameraInfo) camera_info;
    daxa_BufferPtr(ObjectInfo) object_info;
    daxa_BufferPtr(Vertex) vertices;
};

struct CullingPush {
    daxa_ImageViewId depth_image;
    daxa_SamplerId depth_sampler;
    daxa_BufferPtr(PointLight) point_light_buffer;
    daxa_BufferPtr(PointLightIndex) visible_point_light_indices;
    daxa_BufferPtr(CameraInfo) camera_info;
    i32vec2 viewport_size;
    i32vec2 tile_nums;
};

struct DrawPush {
    daxa_BufferPtr(CameraInfo) camera_info;
    daxa_BufferPtr(ObjectInfo) object_info;
    daxa_BufferPtr(Vertex) vertices;
    daxa_BufferPtr(Material) materials;
    daxa_BufferPtr(PointLight) point_light_buffer;
    daxa_BufferPtr(PointLightIndex) visible_point_light_indices;
    u32 material_index;
    i32vec2 tile_nums;
};