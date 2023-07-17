#include "../app.hpp"
#include "../camera.hpp"

#include <daxa/types.hpp>
#include <glm/glm.hpp>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include "shared.inl"

#include "../model.hpp"

#include <daxa/utils/imgui.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>

struct DirectionalShadowApp : public App {
    std::unique_ptr<Model> model;
    std::shared_ptr<daxa::RasterPipeline> raster_pipeline;
    std::shared_ptr<daxa::RasterPipeline> shadow_pipeline;
    daxa::ImageId depth_image;

    daxa::ImageId shadow_image;
    daxa::SamplerId shadow_sampler;
    daxa::BufferId light_buffer;

    glm::mat4 light_matrix;

    ControlledCamera3D camera;

    f64 current_frame = glfwGetTime();
    f64 last_frame = current_frame;
    f64 delta_time;
    bool paused = false;

    bool use_pcf = false;
    glm::vec3 direction = { -6.9, 0.0f, 0.0f };
    float bias = 0.0001f;
    int pcf_range = 1;
    float shadow_intensity = 0.1f;

    daxa::ImGuiRenderer imgui_renderer;

    DirectionalShadowApp() : App("Directional shadow Example") {
        raster_pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/directional_shadow/shader.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/directional_shadow/shader.glsl" }, },
                .compile_options = {
                    .defines = { { .name = "USE_PCF", .value = "0" } }
                } 
            },
            .color_attachments = {{ .format = swapchain.get_format() }},
            .depth_test = {
                .depth_attachment_format = daxa::Format::D32_SFLOAT,
                .enable_depth_test = true,
                .enable_depth_write = true,
            },
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::FRONT_BIT
            },
            .push_constant_size = sizeof(DrawPush),
        }).value();

        shadow_pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/directional_shadow/shadow.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/directional_shadow/shadow.glsl" }, },
            },
            .depth_test = {
                .depth_attachment_format = daxa::Format::D32_SFLOAT,
                .enable_depth_test = true,
                .enable_depth_write = true,
            },
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::FRONT_BIT,
                .depth_bias_enable = true,
                .depth_bias_constant_factor = 1.25f, 
                .depth_bias_clamp = 0.0f,
                .depth_bias_slope_factor = 1.75f
            },
            .push_constant_size = sizeof(ShadowPush),
        }).value();

        depth_image = device.create_image({
            .format = daxa::Format::D32_SFLOAT,
            .aspect = daxa::ImageAspectFlagBits::DEPTH,
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT,
            .name = "depth buffer"
        });

        shadow_image = device.create_image({
            .format = daxa::Format::D32_SFLOAT,
            .aspect = daxa::ImageAspectFlagBits::DEPTH,
            .size = {1024, 1024, 1},
            .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_READ_ONLY,
            .name = "shadow image"
        });

        shadow_sampler = device.create_sampler(daxa::SamplerInfo {
            .magnification_filter = daxa::Filter::LINEAR,
            .minification_filter = daxa::Filter::LINEAR,
            .mipmap_filter = daxa::Filter::LINEAR,
            .address_mode_u = daxa::SamplerAddressMode::CLAMP_TO_BORDER,
            .address_mode_v = daxa::SamplerAddressMode::CLAMP_TO_BORDER,
            .address_mode_w = daxa::SamplerAddressMode::CLAMP_TO_BORDER,
            .mip_lod_bias = 0.0f,
            .enable_anisotropy = true,
            .max_anisotropy = 16.0f,
            .enable_compare = true,
            .compare_op = daxa::CompareOp::LESS,
            .min_lod = 0.0f,
            .max_lod = 1.0f,
            .border_color = daxa::BorderColor::FLOAT_OPAQUE_WHITE,
            .enable_unnormalized_coordinates = false,
        });

        light_buffer = device.create_buffer(daxa::BufferInfo {
            .size = sizeof(LightInfo),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "light buffer"
        });

        camera.camera.resize(size_x, size_y);

        model = std::make_unique<Model>(device, "assets/Sponza/glTF/Sponza.gltf");

        ImGui::CreateContext();
        ImGui_ImplGlfw_InitForVulkan(glfw_window_ptr, true);
        imgui_renderer =  daxa::ImGuiRenderer({
            .device = device,
            .format = swapchain.get_format(),
        });
    }

    ~DirectionalShadowApp() {
        device.destroy_image(depth_image);
        device.destroy_image(shadow_image);
        device.destroy_sampler(shadow_sampler);
        device.destroy_buffer(light_buffer);
    }

    void render() {
        auto swapchain_image = swapchain.acquire_next_image();
        if(swapchain_image.is_empty()) { return; }

        auto cmd_list = device.create_command_list({.name = "render command list"});

        glm::vec3 light_position(-4.0f, 55.0f, -4.0f);
        glm::mat4 light_projection = glm::ortho(-128.0f, 128.0f, -128.0f, 128.0f, -128.0f, 128.0f);

        glm::vec3 dir = { 0.0f, -1.0f, 0.0f };
        dir = glm::rotateX(dir, glm::radians(direction.x));
        dir = glm::rotateY(dir, glm::radians(direction.y));
        dir = glm::rotateZ(dir, glm::radians(direction.z));

        glm::mat4 light_view = glm::lookAt(light_position, light_position + dir, glm::vec3(0.0, -1.0, 0.0));

        glm::mat4 model_mat = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 0.0f}) * glm::scale(glm::mat4{1.0f}, glm::vec3{0.01f, 0.01f, 0.01f});

        light_matrix = light_projection * light_view * model_mat;

        {
            auto* ptr = device.get_host_address_as<LightInfo>(light_buffer);
            ptr->light_matrix = *reinterpret_cast<f32mat4x4*>(&light_matrix);
            ptr->shadow_image = shadow_image.default_view();
            ptr->shadow_sampler = shadow_sampler;
        }

        cmd_list.pipeline_barrier_image_transition({
            .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .image_id = swapchain_image
        });

        cmd_list.pipeline_barrier_image_transition({
            .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .image_slice = {.image_aspect = daxa::ImageAspectFlagBits::DEPTH },
            .image_id = depth_image
        });

        cmd_list.pipeline_barrier_image_transition({
            .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .image_slice = {.image_aspect = daxa::ImageAspectFlagBits::DEPTH },
            .image_id = shadow_image
        });

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .depth_attachment = {{
                .image_view = shadow_image.default_view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = daxa::DepthValue{1.0f, 0},
            }},
            .render_area = {.x = 0, .y = 0, .width = 1024, .height = 1024},
        });
        cmd_list.set_pipeline(*shadow_pipeline);

        glm::mat4 shadow_mvp = light_matrix;

        for(auto& primitive : model->primitives) {
            cmd_list.push_constant(ShadowPush {
                .mvp = *reinterpret_cast<f32mat4x4*>(&shadow_mvp),
                .vertices = device.get_device_address(model->vertex_buffer)
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
            .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
            .src_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
            .image_slice = {.image_aspect = daxa::ImageAspectFlagBits::DEPTH },
            .image_id = shadow_image
        });

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = {swapchain_image},
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = std::array<float, 4>{0.2f, 0.4f, 1.0f, 1.0f},
            }},
            .depth_attachment = {{
                .image_view = depth_image.default_view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = daxa::DepthValue{1.0f, 0},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*raster_pipeline);

        glm::mat4 mvp = camera.camera.get_vp() * model_mat;

        for(auto& primitive : model->primitives) {
            cmd_list.push_constant(DrawPush {
                .mvp = *reinterpret_cast<f32mat4x4*>(&mvp),
                .vertices = device.get_device_address(model->vertex_buffer),
                .materials = device.get_device_address(model->material_buffer),
                .material_index = primitive.material_index,
                .light_buffer = device.get_device_address(light_buffer),
                .bias = bias,
                .pcf_range = pcf_range,
                .shadow_intensity = shadow_intensity
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

        imgui_renderer.record_commands(ImGui::GetDrawData(), cmd_list, swapchain_image, size_x, size_y);

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

            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGui::Begin("directional shadow settings");
            ImGui::DragFloat3("direction", &direction.x);
            ImGui::DragFloat("bias", &bias, 0.0001f, 0.0000001f, 0.1f);
            if(ImGui::Checkbox("use pcf", &use_pcf)) {
                daxa::ShaderDefine pcf_mode = { .name = "USE_PCF", .value = "0" };
                if(use_pcf) {
                    pcf_mode.value = "1";
                }

                raster_pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
                    .vertex_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/directional_shadow/shader.glsl" }, },
                    },
                    .fragment_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/directional_shadow/shader.glsl" }, },
                        .compile_options = {
                            .defines = { pcf_mode }
                        } 
                    },
                    .color_attachments = {{ .format = swapchain.get_format() }},
                    .depth_test = {
                        .depth_attachment_format = daxa::Format::D32_SFLOAT,
                        .enable_depth_test = true,
                        .enable_depth_write = true,
                    },
                    .raster = {
                        .face_culling = daxa::FaceCullFlagBits::FRONT_BIT
                    },
                    .push_constant_size = sizeof(DrawPush),
                }).value();
            }
            ImGui::DragInt("pcf range", &pcf_range, 1.0f, 1.0f, 6.0f);
            ImGui::DragFloat("shadow intensity", &shadow_intensity, 0.05f, 0.0001f, 1.0f);
            ImGui::End();

            ImGui::Render();

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
    DirectionalShadowApp app;
    app.update();
    return 0;
}