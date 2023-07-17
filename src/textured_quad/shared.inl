#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

struct Vertex {
    f32vec2 position;
    f32vec2 uv;
};

DAXA_DECL_BUFFER_PTR(Vertex)

struct DrawPush {
    daxa_BufferPtr(Vertex) vertices;
    ImageViewId image_view_id;
    SamplerId sampler_id;
};