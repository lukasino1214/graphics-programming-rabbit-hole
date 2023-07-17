#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

#include "../common.inl"

struct GBufferGatherPush {
    f32mat4x4 mvp;
    daxa_BufferPtr(Vertex) vertices;
    daxa_BufferPtr(Material) materials;
    u32 material_index;
};

struct CompositionPush {
    daxa_ImageViewId albedo_image;
    daxa_ImageViewId normal_image;
    daxa_ImageViewId depth_image;
    daxa_SamplerId sampler_id;
};