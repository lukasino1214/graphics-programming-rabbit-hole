#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

struct Vertex {
    f32vec3 position;
};

DAXA_DECL_BUFFER_PTR(Vertex)

struct DrawPush {
    f32mat4x4 mvp;
    daxa_BufferPtr(Vertex) vertices;
};