#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

struct Vertex {
    f32vec2 position;
};

DAXA_DECL_BUFFER_PTR(Vertex)

struct DrawPush {
    daxa_BufferPtr(Vertex) vertices;
};

struct BloomPush {
    f32vec2 src_resolution;
    f32 filter_radius;
    ImageViewId image_view_id;
    SamplerId sampler_id;
};

struct CompositionPush {
    ImageViewId render_image_view_id;
    ImageViewId bloom_image_view_id;
    SamplerId sampler_id;
    f32 bloom_strength;
};