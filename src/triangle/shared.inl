#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

struct Vertex {
    f32vec2 position;
    f32vec3 color;
};

DAXA_DECL_BUFFER_PTR(Vertex)

struct DrawPush {
    daxa_BufferPtr(Vertex) vertices;
};