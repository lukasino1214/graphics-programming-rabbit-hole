#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

#include "../common.inl"

#define TILE_SIZE 16
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
    f32vec3 color;
    f32 radius;
};

DAXA_DECL_BUFFER_PTR(PointLight)

struct PointLightIndex {
    u32 index;
};

DAXA_DECL_BUFFER_PTR(PointLightIndex)

struct PointLightGrid {
    u32 count;
};

DAXA_DECL_BUFFER_PTR(PointLightGrid)

struct Plane {
    f32vec3 normal;
    f32 dist; 
};

struct Frustum {
    Plane planes[4];
};

DAXA_DECL_BUFFER_PTR(Frustum)

struct DepthPrepassPush {
    daxa_BufferPtr(CameraInfo) camera_info;
    daxa_BufferPtr(ObjectInfo) object_info;
    daxa_BufferPtr(Vertex) vertices;
};

struct ComputeFrustumsPush {
    daxa_BufferPtr(CameraInfo) camera_info;
    daxa_BufferPtr(Frustum) frustum_buffer;
    i32vec2 viewport_size;
    i32vec2 tile_nums;
};

struct ComputeLightListPush {
    daxa_ImageViewId depth_image;
    daxa_SamplerId depth_sampler;
    daxa_BufferPtr(CameraInfo) camera_info;
    daxa_BufferPtr(Frustum) frustum_buffer;
    daxa_BufferPtr(PointLight) point_light_buffer;
    daxa_BufferPtr(PointLightIndex) point_light_index_buffer;
    daxa_BufferPtr(PointLightGrid) point_light_grid_buffer;
    i32vec2 viewport_size;
    i32vec2 tile_nums;
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
    daxa_BufferPtr(PointLightIndex) point_light_index_buffer;
    daxa_BufferPtr(PointLightGrid) point_light_grid_buffer;
    u32 material_index;
    i32vec2 tile_nums;
};