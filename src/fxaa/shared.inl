#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

#include "../common.inl"

struct DrawPush {
    f32mat4x4 mvp;
    daxa_BufferPtr(Vertex) vertices;
    daxa_BufferPtr(Material) materials;
    u32 material_index;
};

struct FXAAPush {
    f32vec2 image_size;
    daxa_ImageViewId image;
    daxa_SamplerId image_sampler;
    f32 luma_threshold;
    f32 mul_reduce;
    f32 min_reduce;
    f32 max_span;
};