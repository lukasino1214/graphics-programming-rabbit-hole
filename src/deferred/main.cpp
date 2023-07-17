#include "../app.hpp"
#include "../camera.hpp"

#include <glm/glm.hpp>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

#include "shared.inl"

#include "../model.hpp"

struct DeferredApp : public App {
    std::unique_ptr<Model> model;
    std::shared_ptr<daxa::RasterPipeline> g_buffer_gather_pipeline;
    std::shared_ptr<daxa::RasterPipeline> composition_pipeline;
    daxa::ImageId depth_image;
    daxa::ImageId albedo_image;
    daxa::ImageId normal_image;
    daxa::SamplerId sampler_id;

    ControlledCamera3D camera;

    f64 current_frame = glfwGetTime();
    f64 last_frame = current_frame;
    f64 delta_time;
    bool paused = false;

    DeferredApp() : App("Deferred Example") {
        g_buffer_gather_pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/deferred/g_buffer_gather.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/deferred/g_buffer_gather.glsl" }, },
            },
            .color_attachments = {
                { .format = swapchain.get_format() }, // albedo image
                { .format = daxa::Format::R16G16B16A16_SFLOAT } // normal image
            },
            .depth_test = {
                .depth_attachment_format = daxa::Format::D32_SFLOAT,
                .enable_depth_test = true,
                .enable_depth_write = true,
            },
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::FRONT_BIT
            },
            .push_constant_size = sizeof(GBufferGatherPush),
        }).value();

        composition_pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/deferred/composition.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/deferred/composition.glsl" }, },
            },
            .color_attachments = {{ .format = swapchain.get_format() }},
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::NONE
            },
            .push_constant_size = sizeof(CompositionPush),
        }).value();

        depth_image = device.create_image({
            .format = daxa::Format::D32_SFLOAT,
            .aspect = daxa::ImageAspectFlagBits::DEPTH,
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT,
        });

        albedo_image = device.create_image({
            .format = swapchain.get_format(),
            .aspect = daxa::ImageAspectFlagBits::COLOR,
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_READ_ONLY,
        });

        normal_image = device.create_image({
            .format = daxa::Format::R16G16B16A16_SFLOAT,
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
            .max_lod = 1.0f,
            .enable_unnormalized_coordinates = false,
        });

        camera.camera.resize(size_x, size_y);

        model = std::make_unique<Model>(device, "assets/Sponza/glTF/Sponza.gltf");
    }

    ~DeferredApp() {
        device.destroy_image(depth_image);
        device.destroy_image(albedo_image);
        device.destroy_image(normal_image);
        device.destroy_sampler(sampler_id);
    }

    void render() {
        auto swapchain_image = swapchain.acquire_next_image();
        if(swapchain_image.is_empty()) { return; }

        auto cmd_list = device.create_command_list({.name = "render command list"});

        cmd_list.pipeline_barrier_image_transition({
            .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .image_id = swapchain_image
        });

        cmd_list.pipeline_barrier_image_transition({
            .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .image_slice = {.image_aspect = daxa::ImageAspectFlagBits::DEPTH },
            .image_id = depth_image
        });

        cmd_list.pipeline_barrier_image_transition({
            .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .image_id = albedo_image
        });

        cmd_list.pipeline_barrier_image_transition({
            .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .image_id = normal_image
        });

        // g buffer gather
        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { 
                daxa::RenderAttachmentInfo {
                    .image_view = albedo_image.default_view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<float, 4>{0.2f, 0.4f, 1.0f, 1.0f},
                },
                daxa::RenderAttachmentInfo {
                    .image_view = normal_image.default_view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
                },
            },
            .depth_attachment = {{
                .image_view = depth_image.default_view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = daxa::DepthValue{1.0f, 0},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*g_buffer_gather_pipeline);

        glm::mat4 model_mat = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 0.0f}) * glm::scale(glm::mat4{1.0f}, glm::vec3{0.01f, 0.01f, 0.01f});

        glm::mat4 mvp = camera.camera.get_vp() * model_mat;

        for(auto& primitive : model->primitives) {
            cmd_list.push_constant(GBufferGatherPush {
                .mvp = *reinterpret_cast<f32mat4x4*>(&mvp),
                .vertices = device.get_device_address(model->vertex_buffer),
                .materials = device.get_device_address(model->material_buffer),
                .material_index = primitive.material_index
            });

            if(primitive.index_count > 0) {
                cmd_list.set_index_buffer(model->index_buffer, 0);
                cmd_list.draw_indexed({
                    .index_count = primitive.index_count,
                    .instance_count = 1,
                    .first_index = primitive.first_index,
                    .vertex_offset = static_cast<i32>(primitive.first_vertex),
                    .first_instance = 0,
                });
            } else {
                cmd_list.draw({
                    .vertex_count = primitive.vertex_count,
                    .instance_count = 1,
                    .first_vertex = primitive.first_vertex,
                    .first_instance = 0
                });
            }
        }

        cmd_list.end_renderpass();

        cmd_list.pipeline_barrier_image_transition({
            .src_access = daxa::AccessConsts::FRAGMENT_SHADER_WRITE,
            .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_READ,
            .src_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
            .image_id = albedo_image
        });

        cmd_list.pipeline_barrier_image_transition({
            .src_access = daxa::AccessConsts::FRAGMENT_SHADER_WRITE,
            .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_READ,
            .src_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
            .image_id = normal_image
        });

        cmd_list.pipeline_barrier_image_transition({
            .src_access = daxa::AccessConsts::FRAGMENT_SHADER_WRITE,
            .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_READ,
            .src_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
            .image_id = depth_image
        });

        // composition
        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = swapchain_image.default_view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*composition_pipeline);
        cmd_list.push_constant(CompositionPush {
            .albedo_image = albedo_image.default_view(),
            .normal_image = normal_image.default_view(),
            .depth_image = depth_image.default_view(),
            .sampler_id = sampler_id
        });
        cmd_list.draw({ .vertex_count = 3 });
        cmd_list.end_renderpass();

        cmd_list.pipeline_barrier_image_transition({
            .src_access = daxa::AccessConsts::ALL_GRAPHICS_READ_WRITE,
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
            current_frame = glfwGetTime();
            delta_time = current_frame - last_frame;
            last_frame = current_frame;

            camera.camera.set_pos(camera.pos);
            camera.camera.set_rot(camera.rot.x, camera.rot.y);
            camera.update(delta_time);


            glfwPollEvents();
            render();
        }
    }

    void resize(u32 x, u32 y) override {
        minimized = (x == 0 || y == 0);
        if (!minimized) {
            swapchain.resize();
            size_x = swapchain.get_surface_extent().x;
            size_y = swapchain.get_surface_extent().y;
        
            device.destroy_image(depth_image);
            depth_image = device.create_image({
                .format = daxa::Format::D32_SFLOAT,
                .aspect = daxa::ImageAspectFlagBits::DEPTH,
                .size = { size_x, size_y, 1 },
                .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT,
            });

            device.destroy_image(albedo_image);
            albedo_image = device.create_image({
                .format = swapchain.get_format(),
                .aspect = daxa::ImageAspectFlagBits::COLOR,
                .size = { size_x, size_y, 1 },
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_READ_ONLY,
            });

            device.destroy_image(normal_image);
            normal_image = device.create_image({
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .aspect = daxa::ImageAspectFlagBits::COLOR,
                .size = { size_x, size_y, 1 },
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_READ_ONLY,
            });

            camera.camera.resize(size_x, size_y);
        }
    }

    void on_mouse_move(f32 x, f32 y) override {
        if (!paused) {
            f32 center_x = static_cast<f32>(size_x / 2);
            f32 center_y = static_cast<f32>(size_y / 2);
            auto offset = glm::vec2{x - center_x, center_y - y};
            camera.on_mouse_move(offset.x, offset.y);
            glfwSetCursorPos(glfw_window_ptr, static_cast<f64>(center_x), static_cast<f64>(center_y));
        }
    }

    void on_key(int key, int action) override {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            toggle_pause();
        }

        if (!paused) {
            camera.on_key(key, action);
        }
    }

    void toggle_pause() {
        glfwSetCursorPos(glfw_window_ptr, static_cast<f64>(size_x / 2), static_cast<f64>(size_y / 2));
        glfwSetInputMode(glfw_window_ptr, GLFW_CURSOR, paused ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        glfwSetInputMode(glfw_window_ptr, GLFW_RAW_MOUSE_MOTION, paused);
        paused = !paused;
    }
};

auto main() -> i32 {
    DeferredApp app;
    app.update();
    return 0;
}