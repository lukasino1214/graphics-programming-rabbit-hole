#pragma once

#include <daxa/daxa.hpp>
using namespace daxa::types;

#include <memory>
#include "common.inl"

struct Texture {
    enum class Type : u8 {
        UNORM = 0,
        SRGB = 1
    };

    struct PayLoad {
        std::unique_ptr<Texture> texture;
        daxa::CommandList command_list;
    };

    Texture();
    Texture(daxa::Device device, u32 size_x, u32 size_y, unsigned char* data, Type type);
    Texture(daxa::Device device, const std::string& path, Type type);
    ~Texture();

    static auto load_texture(daxa::Device device, u32 size_x, u32 size_y, unsigned char* data, Type type) -> PayLoad;
    static auto load_texture(daxa::Device device, const std::string& file_path, Type type) -> PayLoad;

    auto get_texture_id() -> TextureId;

    daxa::Device device;
    daxa::ImageId image_id;
    daxa::SamplerId sampler_id;
};