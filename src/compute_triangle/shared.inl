#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

struct ComputeDraw {
    daxa_ImageViewId image;
    daxa_u32vec2 frame_dim;
};