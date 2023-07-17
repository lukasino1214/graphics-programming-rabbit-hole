#include "../app.hpp"
#include <glm/glm.hpp>

#include <daxa/utils/imgui.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>

#include "shared.inl"

struct BloomApp : public App {
    struct BloomMip {
        glm::vec2 size;
        glm::ivec2 int_size;
        daxa::ImageId texture;
    };

    daxa::BufferId vertex_buffer;
    daxa::BufferId index_buffer;
    std::shared_ptr<daxa::RasterPipeline> render_pipeline;
    std::shared_ptr<daxa::RasterPipeline> composition_pipeline;
    daxa::ImageId render_image;
    daxa::ImageId bloom_image;
    daxa::SamplerId sampler_id;

    std::shared_ptr<daxa::RasterPipeline> down_sample_pipeline;
    std::shared_ptr<daxa::RasterPipeline> up_sample_pipeline;

    daxa::ImGuiRenderer imgui_renderer;

    std::vector<BloomMip> mip_chain;

    i32 mip_levels = 5;
    f32 filter_radius = 0.05f;
    f32 bloom_strength = 2.0f;

    BloomApp() : App("Bloom Example") {
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
            *ptr++ = { { 0.5f, 0.5f }};
            *ptr++ = { { 0.5f, -0.5f }};
            *ptr++ = { { -0.5f, -0.5f }};
            *ptr++ = { { -0.5f, 0.5f }};
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

        auto cmd_list = device.create_command_list({.name = "upload command list"});

        cmd_list.copy_buffer_to_buffer( daxa::BufferCopyInfo {
            .src_buffer = vertex_staging_buffer,
            .src_offset = 0,
            .dst_buffer = vertex_buffer,
            .dst_offset = 0,
            .size = 4 * sizeof(Vertex)
        });

        cmd_list.copy_buffer_to_buffer( daxa::BufferCopyInfo {
            .src_buffer = index_staging_buffer,
            .src_offset = 0,
            .dst_buffer = index_buffer,
            .dst_offset = 0,
            .size = 6 * sizeof(u32)
        });

        cmd_list.complete();
        device.submit_commands({
            .command_lists = {std::move(cmd_list)},
        });
        device.wait_idle();
        device.destroy_buffer(vertex_staging_buffer);
        device.destroy_buffer(index_staging_buffer);

        render_pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
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

        render_image = device.create_image({
            .format = swapchain.get_format(),
            .aspect = daxa::ImageAspectFlagBits::COLOR,
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_READ_ONLY,
        });

        bloom_image = device.create_image({
            .format = swapchain.get_format(),
            .aspect = daxa::ImageAspectFlagBits::COLOR,
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_READ_ONLY,
        });

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

        composition_pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
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

        down_sample_pipeline = pipeline_manager.add_raster_pipeline({
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

        up_sample_pipeline = pipeline_manager.add_raster_pipeline({
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
                .aspect = daxa::ImageAspectFlagBits::COLOR,
                .size = { static_cast<u32>(mip_int_size.x), static_cast<u32>(mip_int_size.y), 1 },
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_READ_ONLY,
            });

            mip_chain.push_back(std::move(mip));
        }

        ImGui::CreateContext();
        ImGui_ImplGlfw_InitForVulkan(glfw_window_ptr, true);
        imgui_renderer =  daxa::ImGuiRenderer({
            .device = device,
            .format = swapchain.get_format(),
        });
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
        if(swapchain_image.is_empty()) { return; }

        auto cmd_list = device.create_command_list({.name = "render command list"});

        cmd_list.pipeline_barrier_image_transition({
            .src_access = daxa::AccessConsts::NONE,
            .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .image_id = render_image
        });

        cmd_list.pipeline_barrier_image_transition({
            .src_access = daxa::AccessConsts::NONE,
            .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .image_id = bloom_image
        });

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { 
                daxa::RenderAttachmentInfo {
                    .image_view = render_image.default_view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f},
                }, 
                daxa::RenderAttachmentInfo {
                    .image_view = bloom_image.default_view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f},
                }
            },
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*render_pipeline);
        cmd_list.push_constant(DrawPush {
            .vertices = device.get_device_address(vertex_buffer)
        });
        cmd_list.set_index_buffer(index_buffer, 0);
        cmd_list.draw_indexed({ .index_count = 6});
        cmd_list.end_renderpass();

        cmd_list.pipeline_barrier_image_transition({
            .src_access = daxa::AccessConsts::FRAGMENT_SHADER_WRITE,
            .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_READ,
            .src_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
            .image_id = render_image
        });

        cmd_list.pipeline_barrier_image_transition({
            .src_access = daxa::AccessConsts::FRAGMENT_SHADER_WRITE,
            .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_READ,
            .src_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
            .image_id = bloom_image
        });

        cmd_list.pipeline_barrier_image_transition({
            .src_access = daxa::AccessConsts::NONE,
            .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .image_id = mip_chain[0].texture
        });

        cmd_list.begin_renderpass({
            .color_attachments = {
                {
                    .image_view = this->mip_chain[0].texture.default_view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<f32, 4>{0.0f, 0.0f, 0.0f, 1.0f},
                },
            },
            .render_area = {.x = 0, .y = 0, .width = static_cast<u32>(mip_chain[0].int_size.x), .height = static_cast<u32>(mip_chain[0].int_size.y)},
        });
        cmd_list.set_pipeline(*down_sample_pipeline);
        cmd_list.push_constant(BloomPush {
            .src_resolution = { static_cast<f32>(size_x), static_cast<f32>(size_y) },
            .filter_radius = filter_radius,
            .image_view_id = bloom_image.default_view(),
            .sampler_id = sampler_id,
        });
        cmd_list.draw({ .vertex_count = 3 });
        cmd_list.end_renderpass();

        for(u32 i = 0; i < mip_chain.size() - 1; i++) {
            cmd_list.pipeline_barrier_image_transition({
                .src_access = daxa::AccessConsts::NONE,
                .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_WRITE,
                .src_layout = daxa::ImageLayout::UNDEFINED,
                .dst_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
                .image_id = mip_chain[i+1].texture
            });

            cmd_list.pipeline_barrier_image_transition({
                .src_access = daxa::AccessConsts::FRAGMENT_SHADER_WRITE,
                .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_READ,
                .src_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
                .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
                .image_id = mip_chain[i].texture
            });

            cmd_list.begin_renderpass({
                .color_attachments = {
                    {
                        .image_view = this->mip_chain[i+1].texture.default_view(),
                        .load_op = daxa::AttachmentLoadOp::CLEAR,
                        .clear_value = std::array<f32, 4>{0.0f, 0.0f, 0.0f, 1.0f},
                    },
                },
                .render_area = {.x = 0, .y = 0, .width = static_cast<u32>(mip_chain[i+1].int_size.x), .height = static_cast<u32>(mip_chain[i+1].int_size.y)},
            });
            cmd_list.set_pipeline(*down_sample_pipeline);
            cmd_list.push_constant(BloomPush {
                .src_resolution = { static_cast<f32>(mip_chain[i].int_size.x ), static_cast<f32>(mip_chain[i].int_size.y) },
                .filter_radius = filter_radius,
                .image_view_id = mip_chain[i].texture.default_view(),
                .sampler_id = sampler_id
            });
            cmd_list.draw({ .vertex_count = 3 });
            cmd_list.end_renderpass();
        }

        for(u32 i = mip_chain.size() - 1; i > 0; i--) {
            cmd_list.pipeline_barrier_image_transition({
                .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
                .src_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
                .dst_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
                .image_id = mip_chain[i-1].texture
            });

            cmd_list.pipeline_barrier_image_transition({
                .dst_access = daxa::AccessConsts::READ,
                .src_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
                .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
                .image_id = mip_chain[i].texture
            });

            cmd_list.begin_renderpass({
                .color_attachments = {
                    {
                        .image_view = mip_chain[i-1].texture.default_view(),
                        .load_op = daxa::AttachmentLoadOp::CLEAR,
                        .clear_value = std::array<f32, 4>{0.0f, 0.0f, 0.0f, 1.0f},
                    },
                },
                .render_area = {.x = 0, .y = 0, .width = static_cast<u32>(mip_chain[i-1].int_size.x), .height = static_cast<u32>(mip_chain[i-1].int_size.y)},
            });
            cmd_list.set_pipeline(*up_sample_pipeline);
            cmd_list.push_constant(BloomPush {
                .src_resolution = { static_cast<f32>(mip_chain[i].int_size.x), static_cast<f32>(mip_chain[i].int_size.y) },
                .filter_radius = filter_radius,
                .image_view_id = mip_chain[i].texture.default_view(),
                .sampler_id = sampler_id
            });
            cmd_list.draw({ .vertex_count = 3 });
            cmd_list.end_renderpass();
        }

        cmd_list.pipeline_barrier_image_transition({
            .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
            .src_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
            .dst_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .image_id = bloom_image
        });

        cmd_list.pipeline_barrier_image_transition({
            .dst_access = daxa::AccessConsts::READ,
            .src_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
            .image_id = mip_chain[0].texture
        });

        cmd_list.begin_renderpass({
            .color_attachments = {
                {
                    .image_view = bloom_image.default_view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<f32, 4>{0.0f, 0.0f, 0.0f, 1.0f},
                },
            },
            .render_area = {.x = 0, .y = 0, .width = static_cast<u32>(size_x), .height = static_cast<u32>(size_y)},
        });
        cmd_list.set_pipeline(*up_sample_pipeline);
        cmd_list.push_constant(BloomPush {
            .src_resolution = { static_cast<f32>(mip_chain[0].int_size.x), static_cast<f32>(mip_chain[0].int_size.y) },
            .filter_radius = filter_radius,
            .image_view_id = mip_chain[0].texture.default_view(),
            .sampler_id = sampler_id
        });
        cmd_list.draw({ .vertex_count = 3 });
        cmd_list.end_renderpass();


        cmd_list.pipeline_barrier_image_transition({
            .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
            .src_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
            .image_id = bloom_image
        });


        // compose render and bloom image
        cmd_list.pipeline_barrier_image_transition({
            .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .image_id = swapchain_image
        });

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { 
                daxa::RenderAttachmentInfo {
                    .image_view = swapchain_image.default_view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f},
                }, 
            },
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*composition_pipeline);
        cmd_list.push_constant(CompositionPush {
            .render_image_view_id = render_image.default_view(),
            .bloom_image_view_id = bloom_image.default_view(),
            .sampler_id = sampler_id,
            .bloom_strength = bloom_strength
        });
        cmd_list.draw({ .vertex_count = 3});
        cmd_list.end_renderpass();

        imgui_renderer.record_commands(ImGui::GetDrawData(), cmd_list, swapchain_image, size_x, size_y);

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
                        .aspect = daxa::ImageAspectFlagBits::COLOR,
                        .size = { static_cast<u32>(mip_int_size.x), static_cast<u32>(mip_int_size.y), 1 },
                        .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_READ_ONLY,
                    });

                    mip_chain.push_back(std::move(mip));
                }
            }
            ImGui::DragFloat("filter radius", &filter_radius, 0.01f, 0.01f, 0.5f);
            ImGui::DragFloat("bloom strength", &bloom_strength, 0.10f, 0.01f, 5.0f);
            ImGui::End();

            ImGui::Render();

            render();
        }
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
                    .aspect = daxa::ImageAspectFlagBits::COLOR,
                    .size = { static_cast<u32>(mip_int_size.x), static_cast<u32>(mip_int_size.y), 1 },
                    .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_READ_ONLY,
                });

                mip_chain.push_back(std::move(mip));
            }

            device.destroy_image(render_image);
            render_image = device.create_image({
                .format = swapchain.get_format(),
                .aspect = daxa::ImageAspectFlagBits::COLOR,
                .size = { size_x, size_y, 1 },
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_READ_ONLY,
            });

            device.destroy_image(bloom_image);
            bloom_image = device.create_image({
                .format = swapchain.get_format(),
                .aspect = daxa::ImageAspectFlagBits::COLOR,
                .size = { size_x, size_y, 1 },
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_READ_ONLY,
            });
        }
    }
};

auto main() -> i32 {
    BloomApp app;
    app.update();
    return 0;
}