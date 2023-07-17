#pragma once

#include <daxa/daxa.hpp>
using namespace daxa::types;

#include "common.inl"

#include "texture.hpp"

struct Model {
    Model(daxa::Device _device, const std::string_view& file_path);
    ~Model();

    daxa::Device device = {};
    daxa::BufferId vertex_buffer = {};
    daxa::BufferId index_buffer = {};
    daxa::BufferId material_buffer = {};

    std::unique_ptr<Texture> null_texture = {};
    std::vector<std::unique_ptr<Texture>> images = {};
    std::vector<Primitive> primitives = {};
};