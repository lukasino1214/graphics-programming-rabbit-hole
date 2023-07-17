
#include "../app.hpp"

#include <glm/glm.hpp>
#include <stb_image.h>
#include <cstring>

#include "shared.inl"

struct TextureQuadApp : public App {
    daxa::BufferId vertex_buffer;
    daxa::BufferId index_buffer;
    std::shared_ptr<daxa::RasterPipeline> raster_pipeline;
    daxa::ImageId image_id;
    daxa::SamplerId sampler_id;

    TextureQuadApp() : App("Texture Quad Example") {
        vertex_buffer = device.create_buffer({
            .size = 4 * sizeof(Vertex),
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
            .name = "vertex buffer"
        });

        daxa::BufferId vertex_staging_buffer = device.create_buffer({
            .size = 4 * sizeof(Vertex),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "staging vertex buffer"
        });

        {
            auto ptr = device.get_host_address_as<Vertex>(vertex_staging_buffer);
            *ptr++ = { { 0.5f, 0.5f }, { 1.0f, 1.0f }};
            *ptr++ = { { 0.5f, -0.5f }, { 1.0f, 0.0f  }};
            *ptr++ = { { -0.5f, -0.5f }, { 0.0f, 0.0f }};
            *ptr++ = { { -0.5f, 0.5f }, { 0.0f, 1.0f }};
        }

        index_buffer = device.create_buffer({
            .size = 6 * sizeof(u32),
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
            .name = "index buffer"
        });

        daxa::BufferId index_staging_buffer = device.create_buffer({
            .size = 6 * sizeof(u32),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "staging index buffer"
        });

        {
            auto ptr = device.get_host_address_as<u32>(index_staging_buffer);
            *ptr++ = 0;
            *ptr++ = 1;
            *ptr++ = 3;
            *ptr++ = 1;
            *ptr++ = 2;
            *ptr++ = 3;
        }

        auto upload_cmd_list = device.create_command_list({.name = "upload command list"});

        upload_cmd_list.copy_buffer_to_buffer( daxa::BufferCopyInfo {
            .src_buffer = vertex_staging_buffer,
            .src_offset = 0,
            .dst_buffer = vertex_buffer,
            .dst_offset = 0,
            .size = 4 * sizeof(Vertex)
        });

        upload_cmd_list.copy_buffer_to_buffer( daxa::BufferCopyInfo {
            .src_buffer = index_staging_buffer,
            .src_offset = 0,
            .dst_buffer = index_buffer,
            .dst_offset = 0,
            .size = 6 * sizeof(u32)
        });

        upload_cmd_list.complete();
        device.submit_commands({
            .command_lists = {std::move(upload_cmd_list)},
        });
        device.wait_idle();
        device.destroy_buffer(vertex_staging_buffer);
        device.destroy_buffer(index_staging_buffer);

        i32 size_x = 0;
        i32 size_y = 0;
        i32 num_channels = 0;
        u8* data = stbi_load("src/textured_quad/texture.png", &size_x, &size_y, &num_channels, 4);
        if(data == nullptr) {
            throw std::runtime_error("Textures couldn't be found");
        }

        u32 mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(size_x, size_y)))) + 1;

        daxa::BufferId staging_texture_buffer = device.create_buffer({
            .size = static_cast<u32>(size_x * size_y) * static_cast<u32>(4 * sizeof(u8)),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "staging texture buffer"
        });

        image_id = device.create_image({
            .dimensions = 2,
            .format = daxa::Format::R8G8B8A8_SRGB,
            .aspect = daxa::ImageAspectFlagBits::COLOR,
            .size = { static_cast<u32>(size_x), static_cast<u32>(size_y), 1 },
            .mip_level_count = mip_levels,
            .array_layer_count = 1,
            .sample_count = 1,
            .usage = daxa::ImageUsageFlagBits::SHADER_READ_ONLY | daxa::ImageUsageFlagBits::TRANSFER_DST | daxa::ImageUsageFlagBits::TRANSFER_SRC,
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY
        });

        sampler_id = device.create_sampler({
            .magnification_filter = daxa::Filter::LINEAR,
            .minification_filter = daxa::Filter::LINEAR,
            .mipmap_filter = daxa::Filter::LINEAR,
            .address_mode_u = daxa::SamplerAddressMode::REPEAT,
            .address_mode_v = daxa::SamplerAddressMode::REPEAT,
            .address_mode_w = daxa::SamplerAddressMode::REPEAT,
            .mip_lod_bias = 0.0f,
            .enable_anisotropy = true,
            .max_anisotropy = 16.0f,
            .enable_compare = false,
            .compare_op = daxa::CompareOp::ALWAYS,
            .min_lod = 0.0f,
            .max_lod = static_cast<f32>(mip_levels),
            .enable_unnormalized_coordinates = false,
        });

        auto* staging_texture_buffer_ptr = device.get_host_address_as<u8>(staging_texture_buffer);
        std::memcpy(staging_texture_buffer_ptr, data, static_cast<u32>(size_x * size_y) * static_cast<u32>(4 * sizeof(u8)));

        daxa::CommandList cmd_list = device.create_command_list({.name = "upload command list"});

        cmd_list.pipeline_barrier_image_transition({
            .src_access = daxa::AccessConsts::TRANSFER_READ_WRITE,
            .dst_access = daxa::AccessConsts::READ_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .image_slice = {
                .image_aspect = daxa::ImageAspectFlagBits::COLOR,
                .base_mip_level = 0,
                .level_count = mip_levels,
                .base_array_layer = 0,
                .layer_count = 1,
            },
            .image_id = image_id,
        });

        cmd_list.copy_buffer_to_image({
            .buffer = staging_texture_buffer,
            .buffer_offset = 0,
            .image = image_id,
            .image_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .image_slice = {
                .image_aspect = daxa::ImageAspectFlagBits::COLOR,
                .mip_level = 0,
                .base_array_layer = 0,
                .layer_count = 1,
            },
            .image_offset = { 0, 0, 0 },
            .image_extent = { static_cast<u32>(size_x), static_cast<u32>(size_y), 1 }
        });

        std::array<i32, 3> mip_size = {
            static_cast<i32>(size_x),
            static_cast<i32>(size_y),
            static_cast<i32>(1),
        };

        for (u32 i = 0; i < mip_levels - 1; ++i) {
            cmd_list.pipeline_barrier_image_transition({
                .src_access = daxa::AccessConsts::TRANSFER_WRITE,
                .dst_access = daxa::AccessConsts::BLIT_READ,
                .src_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
                .dst_layout = daxa::ImageLayout::TRANSFER_SRC_OPTIMAL,
                .image_slice = {
                    .image_aspect = daxa::ImageAspectFlagBits::COLOR,
                    .base_mip_level = i,
                    .level_count = 1,
                    .base_array_layer = 0,
                    .layer_count = 1,
                },
                .image_id = image_id,
            });

            cmd_list.pipeline_barrier_image_transition({
                .dst_access = daxa::AccessConsts::BLIT_READ,
                .src_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
                .image_slice = {
                    .image_aspect = daxa::ImageAspectFlagBits::COLOR,
                    .base_mip_level = i + 1,
                    .level_count = 1,
                    .base_array_layer = 0,
                    .layer_count = 1,
                },
                .image_id = image_id,
            });

            std::array<i32, 3> next_mip_size = {
                std::max<i32>(1, mip_size[0] / 2),
                std::max<i32>(1, mip_size[1] / 2),
                std::max<i32>(1, mip_size[2] / 2),
            };

            cmd_list.blit_image_to_image({
                .src_image = image_id,
                .src_image_layout = daxa::ImageLayout::TRANSFER_SRC_OPTIMAL,
                .dst_image = image_id,
                .dst_image_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
                .src_slice = {
                    .image_aspect = daxa::ImageAspectFlagBits::COLOR,
                    .mip_level = i,
                    .base_array_layer = 0,
                    .layer_count = 1,
                },
                .src_offsets = {{{0, 0, 0}, {mip_size[0], mip_size[1], mip_size[2]}}},
                .dst_slice = {
                    .image_aspect = daxa::ImageAspectFlagBits::COLOR,
                    .mip_level = i + 1,
                    .base_array_layer = 0,
                    .layer_count = 1,
                },
                .dst_offsets = {{{0, 0, 0}, {next_mip_size[0], next_mip_size[1], next_mip_size[2]}}},
                .filter = daxa::Filter::LINEAR,
            });
            
            mip_size = next_mip_size;
        }
        for (u32 i = 0; i < mip_levels - 1; ++i) {
            cmd_list.pipeline_barrier_image_transition({
                .src_access = daxa::AccessConsts::TRANSFER_READ_WRITE,
                .dst_access = daxa::AccessConsts::READ_WRITE,
                .src_layout = daxa::ImageLayout::TRANSFER_SRC_OPTIMAL,
                .dst_layout = daxa::ImageLayout::GENERAL,
                .image_slice = {
                    .image_aspect = daxa::ImageAspectFlagBits::COLOR,
                    .base_mip_level = i,
                    .level_count = 1,
                    .base_array_layer = 0,
                    .layer_count = 1,
                },
                .image_id = image_id,
            });
        }
        cmd_list.pipeline_barrier_image_transition({
            .src_access = daxa::AccessConsts::TRANSFER_READ_WRITE,
            .dst_access = daxa::AccessConsts::READ_WRITE,
            .src_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .dst_layout = daxa::ImageLayout::GENERAL,
            .image_slice = {
                .image_aspect = daxa::ImageAspectFlagBits::COLOR,
                .base_mip_level = mip_levels - 1,
                .level_count = 1,
                .base_array_layer = 0,
                .layer_count = 1,
            },
            .image_id = image_id,
        });

        cmd_list.complete();

        device.submit_commands({
            .command_lists = {std::move(cmd_list)},
        });
        device.wait_idle();
        device.destroy_buffer(staging_texture_buffer);

        stbi_image_free(data);

        raster_pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/textured_quad/shader.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/textured_quad/shader.glsl" } }
            },
            .color_attachments = {{ .format = swapchain.get_format() }},
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::NONE
            },
            .push_constant_size = sizeof(DrawPush),
        }).value();
    }

    ~TextureQuadApp() {
        device.destroy_buffer(vertex_buffer);
        device.destroy_buffer(index_buffer);
        device.destroy_image(image_id);
        device.destroy_sampler(sampler_id);
    }

    void render() {
        auto swapchain_image = swapchain.acquire_next_image();
        if(swapchain_image.is_empty()) { return; }

        auto cmd_list = device.create_command_list({
            .name = "render command list"
        });

        cmd_list.pipeline_barrier_image_transition({
            .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .image_id = swapchain_image
        });

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = {swapchain_image},
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = std::array<float, 4>{0.2f, 0.4f, 1.0f, 1.0f},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });

        cmd_list.set_pipeline(*raster_pipeline);
        cmd_list.set_index_buffer(index_buffer, 0);
        cmd_list.push_constant(DrawPush{
            .vertices = device.get_device_address(vertex_buffer),
            .image_view_id = image_id.default_view(),
            .sampler_id = sampler_id
        });
        cmd_list.draw_indexed({ .index_count = 6});
        cmd_list.end_renderpass();

        cmd_list.pipeline_barrier_image_transition({
            .dst_access = daxa::AccessConsts::ALL_GRAPHICS_READ_WRITE,
            .src_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .dst_layout = daxa::ImageLayout::PRESENT_SRC,
            .image_id = swapchain_image
        });

        cmd_list.complete();

        device.submit_commands({
            .command_lists = {std::move(cmd_list)},
            .wait_binary_semaphores = {swapchain.get_acquire_semaphore()},
            .signal_binary_semaphores = {swapchain.get_present_semaphore()},
            .signal_timeline_semaphores = {{swapchain.get_gpu_timeline_semaphore(), swapchain.get_cpu_timeline_value()}},
        });

        device.present_frame({
            .wait_binary_semaphores = {swapchain.get_present_semaphore()},
            .swapchain = swapchain,
        });
    }

    void update() {
        while (!glfwWindowShouldClose(glfw_window_ptr)) {
            glfwPollEvents();
            render();
        }
    }
};

auto main() -> i32 {
    TextureQuadApp app;
    app.update();
    return 0;
}