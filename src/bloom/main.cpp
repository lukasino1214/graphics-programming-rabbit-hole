#include "../app.hpp"
#include <glm/glm.hpp>

#include <daxa/utils/imgui.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>

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
            *ptr++ = { { 0.5f, 0.5f }, };
            *ptr++ = { { 0.5f, -0.5f }, };
            *ptr++ = { { -0.5f, -0.5f }, };
            *ptr++ = { { -0.5f, 0.5f }, };
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
        daxa::ImageColorAttachment<> render_image = {};
        daxa::ImageColorAttachment<> bloom_image = {};
        daxa::BufferVertexShaderRead vertex_buffer = {};
        daxa::BufferVertexShaderRead index_buffer = {};
    } uses = {};

    std::string_view name = "render";
    RasterPipelineHolder* pipeline = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        daxa::Device device = ti.get_device();
        u32 size_x = ti.get_device().info_image(uses.render_image.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.render_image.image()).size.y;

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { 
                daxa::RenderAttachmentInfo {
                    .image_view = uses.render_image.view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<f32, 4>{0.0f, 0.0f, 0.0f, 1.0f},
                }, 
                daxa::RenderAttachmentInfo {
                    .image_view = uses.bloom_image.view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<f32, 4>{0.0f, 0.0f, 0.0f, 1.0f},
                }
            },
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*pipeline->pipeline);
        cmd_list.push_constant(DrawPush {
            .vertices = device.get_device_address(uses.vertex_buffer.buffer())
        });
        cmd_list.set_index_buffer(uses.index_buffer.buffer(), 0);
        cmd_list.draw_indexed({ .index_count = 6});
        cmd_list.end_renderpass();
    }
};

struct DownSampleTask {
    struct Uses {
        daxa::ImageShaderRead<> higher_mip = {};
        daxa::ImageColorAttachment<> lower_mip = {};
    } uses = {};

    std::string_view name = "down sample";
    RasterPipelineHolder* pipeline = {};
    daxa::SamplerId sampler_id = {};
    f32* filter_radius = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();

        auto higher_size = ti.get_device().info_image(uses.higher_mip.image()).size;
        auto lower_size = ti.get_device().info_image(uses.lower_mip.image()).size;

        cmd_list.begin_renderpass({
            .color_attachments = {
                {
                    .image_view = uses.lower_mip.view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<f32, 4>{0.0f, 0.0f, 0.0f, 1.0f},
                },
            },
            .render_area = {.x = 0, .y = 0, .width = lower_size.x, .height = lower_size.y },
        });
        cmd_list.set_pipeline(*pipeline->pipeline);
        cmd_list.push_constant(BloomPush {
            .src_resolution = { static_cast<f32>(higher_size.x), static_cast<f32>(higher_size.y) },
            .filter_radius = *filter_radius,
            .image_view_id = uses.higher_mip.view(),
            .sampler_id = sampler_id,
        });
        cmd_list.draw({ .vertex_count = 3 });
        cmd_list.end_renderpass();
    }
};

struct UpSampleTask {
    struct Uses {
        daxa::ImageShaderRead<> lower_mip = {};
        daxa::ImageColorAttachment<> higher_mip = {};
    } uses = {};

    std::string_view name = "up sample";
    RasterPipelineHolder* pipeline = {};
    daxa::SamplerId sampler_id = {};
    f32* filter_radius = {};


    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();

        auto higher_size = ti.get_device().info_image(uses.higher_mip.image()).size;
        auto lower_size = ti.get_device().info_image(uses.lower_mip.image()).size;

        cmd_list.begin_renderpass({
                .color_attachments = {
                    {
                        .image_view = uses.higher_mip.view(),
                        .load_op = daxa::AttachmentLoadOp::CLEAR,
                        .clear_value = std::array<f32, 4>{0.0f, 0.0f, 0.0f, 1.0f},
                    },
                },
                .render_area = {.x = 0, .y = 0, .width = higher_size.x, .height = higher_size.y },
            });
            cmd_list.set_pipeline(*pipeline->pipeline);
            cmd_list.push_constant(BloomPush {
                .src_resolution = { static_cast<f32>(lower_size.x), static_cast<f32>(lower_size.y) },
                .filter_radius = *filter_radius,
                .image_view_id = uses.lower_mip.view(),
                .sampler_id = sampler_id
            });
            cmd_list.draw({ .vertex_count = 3 });
            cmd_list.end_renderpass();
    }
};


struct CompositionTask {
    struct Uses {
        daxa::ImageColorAttachment<> render_target = {};
        daxa::ImageShaderRead<> render_image = {};
        daxa::ImageShaderRead<> bloom_image = {};
    } uses = {};

    std::string_view name = "composition";
    RasterPipelineHolder* pipeline = {};
    daxa::ImGuiRenderer imgui_renderer = {};
    daxa::SamplerId sampler_id = {};
    f32* bloom_strength = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        u32 size_x = ti.get_device().info_image(uses.render_target.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.render_target.image()).size.y;

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { 
                daxa::RenderAttachmentInfo {
                    .image_view = uses.render_target.view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<f32, 4>{0.0f, 0.0f, 0.0f, 1.0f},
                }, 
            },
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*pipeline->pipeline);
        cmd_list.push_constant(CompositionPush {
            .render_image_view_id = uses.render_image.view(),
            .bloom_image_view_id = uses.bloom_image.view(),
            .sampler_id = sampler_id,
            .bloom_strength = *bloom_strength
        });
        cmd_list.draw({ .vertex_count = 3});
        cmd_list.end_renderpass();

        imgui_renderer.record_commands(ImGui::GetDrawData(), cmd_list, uses.render_target.image(), size_x, size_y);
    }
};


struct BloomApp : public App {
    struct BloomMip {
        glm::vec2 size;
        glm::ivec2 int_size;
        daxa::ImageId texture;
        daxa::TaskImage task_texture;
    };

    daxa::BufferId vertex_buffer = {};
    daxa::TaskBuffer task_vertex_buffer = {};
    daxa::BufferId index_buffer = {};
    daxa::TaskBuffer task_index_buffer = {};

    RasterPipelineHolder render_pipeline = {};
    RasterPipelineHolder composition_pipeline = {};

    daxa::ImageId render_image = {};
    daxa::TaskImage task_render_image = {};
    daxa::ImageId bloom_image = {};
    daxa::TaskImage task_bloom_image = {};

    daxa::SamplerId sampler_id = {};

    RasterPipelineHolder down_sample_pipeline = {};
    RasterPipelineHolder up_sample_pipeline = {};

    daxa::ImGuiRenderer imgui_renderer = {};

    std::vector<BloomMip> mip_chain = {};

    daxa::TaskImage task_swapchain_image = {};
    daxa::TaskGraph render_task_graph = {};

    i32 mip_levels = 5;
    f32 filter_radius = 0.05f;
    f32 bloom_strength = 2.0f;

    BloomApp() : App("Bloom Example") {
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

        render_image = device.create_image({
            .format = swapchain.get_format(),
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
        });

        task_render_image = daxa::TaskImage { daxa::TaskImageInfo {
            .initial_images = {.images = std::span{&render_image, 1}},
            .swapchain_image = false,
            .name = "task render image"
        }};

        bloom_image = device.create_image({
            .format = swapchain.get_format(),
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
        });

        task_bloom_image = daxa::TaskImage { daxa::TaskImageInfo {
            .initial_images = {.images = std::span{&bloom_image, 1}},
            .swapchain_image = false,
            .name = "task bloom image"
        }};

        sampler_id = device.create_sampler({
            .magnification_filter = daxa::Filter::LINEAR,
            .minification_filter = daxa::Filter::LINEAR,
            .mipmap_filter = daxa::Filter::LINEAR,
            .address_mode_u = daxa::SamplerAddressMode::CLAMP_TO_EDGE,
            .address_mode_v = daxa::SamplerAddressMode::CLAMP_TO_EDGE,
            .address_mode_w = daxa::SamplerAddressMode::CLAMP_TO_EDGE,
            .mip_lod_bias = 0.0f,
            .enable_anisotropy = true,
            .max_anisotropy = 16.0f,
            .enable_compare = false,
            .compare_op = daxa::CompareOp::ALWAYS,
            .min_lod = 0.0f,
            .max_lod = static_cast<f32>(1),
            .enable_unnormalized_coordinates = false,
        });

        render_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/bloom/render.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/bloom/render.glsl" } }
            },
            .color_attachments = {{ .format = swapchain.get_format() }, { .format = swapchain.get_format() }},
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::NONE
            },
            .push_constant_size = sizeof(DrawPush),
        }).value();

        composition_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/bloom/composition.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/bloom/composition.glsl" } }
            },
            .color_attachments = {{ .format = swapchain.get_format() }},
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::NONE
            },
            .push_constant_size = sizeof(CompositionPush),
        }).value();

        down_sample_pipeline.pipeline = pipeline_manager.add_raster_pipeline({
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/bloom/down_sample.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/bloom/down_sample.glsl" } }
            },
            .color_attachments = {{ .format = swapchain.get_format() }},
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::NONE
            },
            .push_constant_size = sizeof(BloomPush),
        }).value();

        up_sample_pipeline.pipeline = pipeline_manager.add_raster_pipeline({
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/bloom/up_sample.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/bloom/up_sample.glsl" } }
            },
            .color_attachments = {{ .format = swapchain.get_format() }},
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::NONE
            },
            .push_constant_size = sizeof(BloomPush),
        }).value();

        glm::vec2 mip_size(static_cast<f32>(size_x), static_cast<f32>(size_y));
        glm::ivec2 mip_int_size = {size_x, size_y};

        for(u32 i = 0; i < mip_levels; i++) {
            mip_int_size /= 2;
            mip_size *= 0.5f;

            BloomMip mip;
            mip.int_size = mip_int_size;
            mip.size = mip_size;
            mip.texture = device.create_image({
                .format = swapchain.get_format(),
                .size = { static_cast<u32>(mip_int_size.x), static_cast<u32>(mip_int_size.y), 1 },
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
            });

            mip.task_texture = daxa::TaskImage { daxa::TaskImageInfo {
                .initial_images = {.images = std::span{&mip.texture, 1}},
                .swapchain_image = false,
                .name = "task texture " + std::to_string(i) + " image"
            }};

            mip_chain.push_back(std::move(mip));
        }

        ImGui::CreateContext();
        ImGui_ImplGlfw_InitForVulkan(glfw_window_ptr, true);
        imgui_renderer =  daxa::ImGuiRenderer({
            .device = device,
            .format = swapchain.get_format(),
        });

        task_swapchain_image = daxa::TaskImage{{.swapchain_image = true, .name = "swapchain image"}};

        rebuild_task_graph();
    }

    ~BloomApp() {
        device.destroy_buffer(vertex_buffer);
        device.destroy_buffer(index_buffer);
        device.destroy_sampler(sampler_id);
        device.destroy_image(render_image);
        device.destroy_image(bloom_image);
        for(auto& img : mip_chain) {
            device.destroy_image(img.texture);
        }

        ImGui_ImplGlfw_Shutdown();
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

            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGui::Begin("bloom settings");
            if(ImGui::DragInt("mip levels", &mip_levels, 1.0f, 1, 10)) {
                for(auto& img : mip_chain) {
                    device.destroy_image(img.texture);
                }

                mip_chain.clear();

                glm::vec2 mip_size(static_cast<f32>(size_x), static_cast<f32>(size_y));
                glm::ivec2 mip_int_size = {size_x, size_y};

                for(u32 i = 0; i < mip_levels; i++) {
                    mip_int_size /= 2;
                    mip_size *= 0.5f;

                    BloomMip mip;
                    mip.int_size = mip_int_size;
                    mip.size = mip_size;
                    mip.texture = device.create_image({
                        .format = swapchain.get_format(),
                        .size = { static_cast<u32>(mip_int_size.x), static_cast<u32>(mip_int_size.y), 1 },
                        .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                    });

                    mip.task_texture = daxa::TaskImage { daxa::TaskImageInfo {
                        .initial_images = {.images = std::span{&mip.texture, 1}},
                        .swapchain_image = false,
                        .name = "task texture " + std::to_string(i) + " image"
                    }};

                    mip_chain.push_back(std::move(mip));
                }

                rebuild_task_graph();
            }
            ImGui::DragFloat("filter radius", &filter_radius, 0.01f, 0.01f, 0.5f);
            ImGui::DragFloat("bloom strength", &bloom_strength, 0.10f, 0.01f, 5.0f);
            ImGui::End();

            ImGui::Render();

            render();
        }
    }

    void rebuild_task_graph() {
        render_task_graph = daxa::TaskGraph({
            .device = device,
            .swapchain = swapchain,
            .name = "render task graph" 
        });

        render_task_graph.use_persistent_image(task_swapchain_image);
        render_task_graph.use_persistent_image(task_render_image);
        render_task_graph.use_persistent_image(task_bloom_image);
        render_task_graph.use_persistent_buffer(task_vertex_buffer);
        render_task_graph.use_persistent_buffer(task_index_buffer);

        for(u32 i = 0; i < mip_levels; i++) {
            render_task_graph.use_persistent_image(mip_chain[i].task_texture);
        }

        render_task_graph.add_task(RenderTask {
            .uses = {
                .render_image = task_render_image,
                .bloom_image = task_bloom_image,
                .vertex_buffer = task_vertex_buffer,
                .index_buffer = task_index_buffer
            },
            .pipeline = &render_pipeline,
        });

        render_task_graph.add_task(DownSampleTask {
            .uses = {
                .higher_mip = task_bloom_image,
                .lower_mip = mip_chain[0].task_texture
            },
            .pipeline = &down_sample_pipeline,
            .sampler_id = sampler_id,
            .filter_radius = &filter_radius
        });

        for(u32 i = 0; i < mip_chain.size() - 1; i++) {
            render_task_graph.add_task(DownSampleTask {
                .uses = {
                    .higher_mip = mip_chain[i].task_texture,
                    .lower_mip = mip_chain[i + 1].task_texture,
                },
                .pipeline = &down_sample_pipeline,
                .sampler_id = sampler_id,
                .filter_radius = &filter_radius
            });
        }

        for(u32 i = mip_chain.size() - 1; i > 0; i--) {
            render_task_graph.add_task(UpSampleTask {
                .uses = {
                    .lower_mip = mip_chain[i].task_texture,
                    .higher_mip = mip_chain[i - 1].task_texture,
                },
                .pipeline = &up_sample_pipeline,
                .sampler_id = sampler_id,
                .filter_radius = &filter_radius
            });
        }

        render_task_graph.add_task(UpSampleTask {
            .uses = {
                .lower_mip = mip_chain[0].task_texture,
                .higher_mip = task_bloom_image
            },
            .pipeline = &up_sample_pipeline,
            .sampler_id = sampler_id,
            .filter_radius = &filter_radius
        });

        render_task_graph.add_task(CompositionTask {
            .uses = {
                .render_target = task_swapchain_image,
                .render_image = task_render_image,
                .bloom_image = task_bloom_image,
            },
            .pipeline = &composition_pipeline,
            .imgui_renderer = imgui_renderer,
            .sampler_id = sampler_id,
            .bloom_strength = &bloom_strength
        });

        render_task_graph.submit({});
        render_task_graph.present({});
        render_task_graph.complete({});
    }

    void resize(u32 x, u32 y) override {
        minimized = (x == 0 || y == 0);
        if (!minimized) {
            swapchain.resize();
            size_x = swapchain.get_surface_extent().x;
            size_y = swapchain.get_surface_extent().y;

            for(auto& img : mip_chain) {
                device.destroy_image(img.texture);
            }

            mip_chain.clear();

            glm::vec2 mip_size(static_cast<f32>(size_x), static_cast<f32>(size_y));
            glm::ivec2 mip_int_size = {size_x, size_y};

            for(u32 i = 0; i < mip_levels; i++) {
                mip_int_size /= 2;
                mip_size *= 0.5f;

                BloomMip mip;
                mip.int_size = mip_int_size;
                mip.size = mip_size;
                mip.texture = device.create_image({
                    .format = swapchain.get_format(),
                    .size = { static_cast<u32>(mip_int_size.x), static_cast<u32>(mip_int_size.y), 1 },
                    .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                });

                mip.task_texture = daxa::TaskImage { daxa::TaskImageInfo {
                    .initial_images = {.images = std::span{&mip.texture, 1}},
                    .swapchain_image = false,
                    .name = "task texture " + std::to_string(i) + " image"
                }};

                mip_chain.push_back(std::move(mip));
            }

            device.destroy_image(render_image);
            render_image = device.create_image({
                .format = swapchain.get_format(),
                .size = { size_x, size_y, 1 },
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
            });
            task_render_image.set_images({.images = std::span{&render_image, 1}});

            device.destroy_image(bloom_image);
            bloom_image = device.create_image({
                .format = swapchain.get_format(),
                .size = { size_x, size_y, 1 },
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
            });
            task_bloom_image.set_images({.images = std::span{&bloom_image, 1}});

            rebuild_task_graph();
        }
    }
};

auto main() -> i32 {
    BloomApp app;
    app.update();
    return 0;
}