
#include "../app.hpp"

#include <glm/glm.hpp>
#include <stb_image.h>
#include <cstring>

#include "shared.inl"

struct UploadMeshData {
    struct Uses {
        daxa::BufferTransferWrite vertex_buffer = {};
        daxa::BufferTransferWrite index_buffer = {};
    } uses = {};

    void callback(daxa::TaskInterface ti) {
        daxa::BufferId vertex_staging_buffer = ti.get_device().create_buffer({
            .size = 4 * sizeof(Vertex),
            .allocate_info = daxa::AllocateInfo{daxa::MemoryFlagBits::HOST_ACCESS_RANDOM},
            .name = "staging vertex buffer"
        });

        daxa::BufferId index_staging_buffer = ti.get_device().create_buffer({
            .size = 6 * sizeof(u32),
            .allocate_info = daxa::AllocateInfo{daxa::MemoryFlagBits::HOST_ACCESS_RANDOM},
            .name = "staging index buffer"
        });

        daxa::CommandList cmd_list = ti.get_command_list();
        cmd_list.destroy_buffer_deferred(vertex_staging_buffer);
        cmd_list.destroy_buffer_deferred(index_staging_buffer);

        {
            auto ptr = ti.get_device().get_host_address_as<Vertex>(vertex_staging_buffer);
            *ptr++ = { { 0.5f, 0.5f }, { 1.0f, 1.0f }};
            *ptr++ = { { 0.5f, -0.5f }, { 1.0f, 0.0f  }};
            *ptr++ = { { -0.5f, -0.5f }, { 0.0f, 0.0f }};
            *ptr++ = { { -0.5f, 0.5f }, { 0.0f, 1.0f }};
        }

        {
            auto ptr = ti.get_device().get_host_address_as<u32>(index_staging_buffer);
            *ptr++ = 0;
            *ptr++ = 1;
            *ptr++ = 3;
            *ptr++ = 1;
            *ptr++ = 2;
            *ptr++ = 3;
        }

        cmd_list.copy_buffer_to_buffer( daxa::BufferCopyInfo {
            .src_buffer = vertex_staging_buffer,
            .src_offset = 0,
            .dst_buffer = uses.vertex_buffer.buffer(),
            .dst_offset = 0,
            .size = 4 * sizeof(Vertex)
        });

        cmd_list.copy_buffer_to_buffer( daxa::BufferCopyInfo {
            .src_buffer = index_staging_buffer,
            .src_offset = 0,
            .dst_buffer = uses.index_buffer.buffer(),
            .dst_offset = 0,
            .size = 6 * sizeof(u32)
        });
    }
};


struct RenderTask {
    struct Uses {
        daxa::BufferVertexShaderRead vertex_buffer = {};
        daxa::BufferVertexShaderRead index_buffer = {};
        daxa::ImageColorAttachment<> render_target = {};
    } uses = {};

    std::string_view name = "render";
    RasterPipelineHolder* pipeline = {};

    daxa::ImageId image_id = {};
    daxa::SamplerId sampler_id = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        u32 size_x = ti.get_device().info_image(uses.render_target.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.render_target.image()).size.y;

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = uses.render_target.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = std::array<float, 4>{0.2f, 0.4f, 1.0f, 1.0f},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });

        cmd_list.set_pipeline(*pipeline->pipeline);
        cmd_list.set_index_buffer(uses.index_buffer.buffer(), 0);
        cmd_list.push_constant(DrawPush{
            .vertices = ti.get_device().get_device_address(uses.vertex_buffer.buffer()),
            .image_view_id = image_id.default_view(),
            .sampler_id = sampler_id
        });
        cmd_list.draw_indexed({ .index_count = 6});
        cmd_list.end_renderpass();
    }
};

struct TextureQuadApp : public App {
    daxa::BufferId vertex_buffer;
    daxa::TaskBuffer task_vertex_buffer;

    daxa::BufferId index_buffer;
    daxa::TaskBuffer task_index_buffer;

    RasterPipelineHolder raster_pipeline;
    daxa::ImageId image_id;
    daxa::SamplerId sampler_id;

    daxa::TaskImage task_swapchain_image = {};
    daxa::TaskGraph render_task_graph = {};

    TextureQuadApp() : App("Texture Quad Example") {
        vertex_buffer = device.create_buffer({
            .size = 4 * sizeof(Vertex),
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
            .name = "vertex buffer"
        });

        task_vertex_buffer = daxa::TaskBuffer({
            .initial_buffers = {.buffers = std::span{&vertex_buffer, 1}},
            .name = "task vertex buffer",
        });

        index_buffer = device.create_buffer({
            .size = 6 * sizeof(u32),
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
            .name = "index buffer"
        });

        task_index_buffer = daxa::TaskBuffer({
            .initial_buffers = {.buffers = std::span{&index_buffer, 1}},
            .name = "task index buffer",
        });

        auto upload_task_graph = daxa::TaskGraph({
            .device = device,
            .name = "upload task graph",
        });

        upload_task_graph.use_persistent_buffer(task_vertex_buffer);
        upload_task_graph.use_persistent_buffer(task_index_buffer);

        upload_task_graph.add_task(UploadMeshData{
            .uses = {
                .vertex_buffer = task_vertex_buffer,
                .index_buffer = task_index_buffer
            },
        });

        upload_task_graph.submit({});
        upload_task_graph.complete({});
        upload_task_graph.execute({});

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
            .size = { static_cast<u32>(size_x), static_cast<u32>(size_y), 1 },
            .mip_level_count = mip_levels,
            .array_layer_count = 1,
            .sample_count = 1,
            .usage = daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::TRANSFER_DST | daxa::ImageUsageFlagBits::TRANSFER_SRC,
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
                .mip_level = 0,
                .base_array_layer = 0,
                .layer_count = 1,
            },
            .image_offset = { 0, 0, 0 },
            .image_extent = { static_cast<u32>(size_x), static_cast<u32>(size_y), 1 }
        });

        auto image_info = device.info_image(image_id);

        std::array<i32, 3> mip_size = {
            static_cast<i32>(image_info.size.x),
            static_cast<i32>(image_info.size.y),
            static_cast<i32>(image_info.size.z),
        };

        for(u32 i = 1; i < image_info.mip_level_count; i++) {
            cmd_list.pipeline_barrier_image_transition({
                .src_access = daxa::AccessConsts::TRANSFER_WRITE,
                .dst_access = daxa::AccessConsts::BLIT_READ,
                .src_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
                .dst_layout = daxa::ImageLayout::TRANSFER_SRC_OPTIMAL,
                .image_slice = {
                    .base_mip_level = i - 1,
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
                    .mip_level = i - 1,
                    .base_array_layer = 0,
                    .layer_count = 1,
                },
                .src_offsets = {{{0, 0, 0}, {mip_size[0], mip_size[1], mip_size[2]}}},
                .dst_slice = {
                    .mip_level = i,
                    .base_array_layer = 0,
                    .layer_count = 1,
                },
                .dst_offsets = {{{0, 0, 0}, {next_mip_size[0], next_mip_size[1], next_mip_size[2]}}},
                .filter = daxa::Filter::LINEAR,
            });

            cmd_list.pipeline_barrier_image_transition({
                .src_access = daxa::AccessConsts::TRANSFER_WRITE,
                .dst_access = daxa::AccessConsts::BLIT_READ,
                .src_layout = daxa::ImageLayout::TRANSFER_SRC_OPTIMAL,
                .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
                .image_slice = {
                    .base_mip_level = i - 1,
                    .level_count = 1,
                    .base_array_layer = 0,
                    .layer_count = 1,
                },
                .image_id = image_id,
            });
            
            mip_size = next_mip_size;
        }

        cmd_list.pipeline_barrier_image_transition({
            .src_access = daxa::AccessConsts::TRANSFER_READ_WRITE,
            .dst_access = daxa::AccessConsts::READ_WRITE,
            .src_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
            .image_slice = {
                .base_mip_level = image_info.mip_level_count - 1,
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

        raster_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
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

        task_swapchain_image = daxa::TaskImage{{.swapchain_image = true, .name = "swapchain image"}};

        render_task_graph = daxa::TaskGraph({
            .device = device,
            .swapchain = swapchain,
            .name = "render task graph" 
        });

        render_task_graph.use_persistent_buffer(task_vertex_buffer);
        render_task_graph.use_persistent_buffer(task_index_buffer);
        render_task_graph.use_persistent_image(task_swapchain_image);

        render_task_graph.add_task(RenderTask {
            .uses = {
                .vertex_buffer = task_vertex_buffer,
                .index_buffer = task_index_buffer,
                .render_target = task_swapchain_image,
            },
            .pipeline = &raster_pipeline,
            .image_id = image_id,
            .sampler_id = sampler_id
        });

        render_task_graph.submit({});
        render_task_graph.present({});
        render_task_graph.complete({});

    }

    ~TextureQuadApp() {
        device.wait_idle();
        device.collect_garbage();
        device.destroy_buffer(vertex_buffer);
        device.destroy_buffer(index_buffer);
        device.destroy_image(image_id);
        device.destroy_sampler(sampler_id);
    }

    void render() {
        auto swapchain_image = swapchain.acquire_next_image();
        task_swapchain_image.set_images({.images = std::span{&swapchain_image, 1}});
        if(swapchain_image.is_empty()) { return; }

        render_task_graph.execute({});
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