#include "../app.hpp"
#include "../camera.hpp"

#include <glm/glm.hpp>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

#include "shared.inl"

#include <daxa/utils/imgui.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>

#include "../model.hpp"

struct NormalMappingApp : public App {
    std::unique_ptr<Model> model;
    std::shared_ptr<daxa::RasterPipeline> raster_pipeline;
    daxa::ImageId depth_image;

    daxa::BufferId camera_buffer;
    daxa::BufferId object_buffer;

    ControlledCamera3D camera;

    glm::vec3 light_position = { 0.5f, -1.0f, 0.3f };

    f64 current_frame = glfwGetTime();
    f64 last_frame = current_frame;
    f64 delta_time;
    bool paused = false;

    bool use_normal_mapping = true;
    bool use_derivatives = false;
    bool debug_normal = false;

    daxa::ImGuiRenderer imgui_renderer;

    NormalMappingApp() : App("Normal Mapping Example") {
        raster_pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/normal_mapping/shader.glsl" }, },
                .compile_options = {
                    .defines = { 
                        { .name = "USE_NORMAL_MAPPING", .value = "1" },
                        { .name = "USE_DERIVATIVES", .value = "0" },
                        { .name = "DEBUG_NORMAL", .value = "0" }
                    }
                } 
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/normal_mapping/shader.glsl" }, },
                .compile_options = {
                    .defines = { 
                        { .name = "USE_NORMAL_MAPPING", .value = "1" },
                        { .name = "USE_DERIVATIVES", .value = "0" },
                        { .name = "DEBUG_NORMAL", .value = "0" }
                    }
                } 
            },
            .color_attachments = {{ .format = swapchain.get_format() }},
            .depth_test = {
                .depth_attachment_format = daxa::Format::D32_SFLOAT,
                .enable_depth_test = true,
                .enable_depth_write = true,
            },
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::NONE
            },
            .push_constant_size = sizeof(DrawPush),
        }).value();

        depth_image = device.create_image({
            .format = daxa::Format::D32_SFLOAT,
            .aspect = daxa::ImageAspectFlagBits::DEPTH,
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT,
        });

        camera.camera.resize(size_x, size_y);

        model = std::make_unique<Model>(device, "assets/stone_wall/stone_wall.gltf");

        camera_buffer = device.create_buffer(daxa::BufferInfo {
            .size = sizeof(CameraInfo),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "camera buffer"
        });

        object_buffer = device.create_buffer(daxa::BufferInfo {
            .size = sizeof(ObjectInfo),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "object buffer"
        });

        ImGui::CreateContext();
        ImGui_ImplGlfw_InitForVulkan(glfw_window_ptr, true);
        imgui_renderer =  daxa::ImGuiRenderer({
            .device = device,
            .format = swapchain.get_format(),
        });
    }

    ~NormalMappingApp() {
        device.destroy_image(depth_image);
        device.destroy_buffer(camera_buffer);
        device.destroy_buffer(object_buffer);
    }

    void render() {
        auto swapchain_image = swapchain.acquire_next_image();
        if(swapchain_image.is_empty()) { return; }

        auto cmd_list = device.create_command_list({.name = "render command list"});

        glm::mat4 projection = camera.camera.proj_mat;
        glm::mat4 view = camera.camera.get_view();

        glm::mat4 temp_inverse_projection_mat = glm::inverse(projection);
        glm::mat4 temp_inverse_view_mat = glm::inverse(view);

        auto* camera_ptr = device.get_host_address_as<CameraInfo>(camera_buffer);
        camera_ptr->projection_matrix = *reinterpret_cast<f32mat4x4*>(&projection);
        camera_ptr->inverse_projection_matrix = *reinterpret_cast<f32mat4x4*>(&temp_inverse_projection_mat);
        camera_ptr->view_matrix = *reinterpret_cast<f32mat4x4*>(&view);
        camera_ptr->inverse_view_matrix = *reinterpret_cast<f32mat4x4*>(&temp_inverse_view_mat);
        camera_ptr->position = *reinterpret_cast<f32vec3*>(&camera.pos);

        glm::mat4 model_matrix = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 0.0f}) * glm::scale(glm::mat4{1.0f}, glm::vec3{1.0f, 1.0f, 1.0f}) * glm::rotate(glm::mat4{1.0}, glm::radians(180.0f), glm::vec3{1.0f, 0.0f, 0.0f});
        glm::mat4 normal_matrix = glm::transpose(glm::inverse(model_matrix));

        auto* object_ptr = device.get_host_address_as<ObjectInfo>(object_buffer);
        object_ptr->model_matrix = *reinterpret_cast<f32mat4x4*>(&model_matrix);
        object_ptr->normal_matrix = *reinterpret_cast<f32mat4x4*>(&normal_matrix);

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

        for(auto& primitive : model->primitives) {
            cmd_list.push_constant(DrawPush {
                .camera_info = device.get_device_address(camera_buffer),
                .object_info = device.get_device_address(object_buffer),
                .vertices = device.get_device_address(model->vertex_buffer),
                .materials = device.get_device_address(model->material_buffer),
                .material_index = primitive.material_index,
                .light_position = *reinterpret_cast<f32vec3*>(&light_position)
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

            ImGui::Begin("Normal Mapping Settings");
            ImGui::DragFloat3("light position", &light_position.x, 0.05f, -1.5f, 1.5f);
            if(ImGui::Checkbox("use normal mapping", &use_normal_mapping) || ImGui::Checkbox("use derivatives", &use_derivatives) || ImGui::Checkbox("debug normal", &debug_normal)) {
                daxa::ShaderDefine normal_mapping = { .name = "USE_NORMAL_MAPPING", .value = "0" };
                if(use_normal_mapping) {
                    normal_mapping.value = "1";
                }

                daxa::ShaderDefine derivatives = { .name = "USE_DERIVATIVES", .value = "0" };
                if(use_derivatives) {
                    derivatives.value = "1";
                }

                daxa::ShaderDefine normal_debug = { .name = "DEBUG_NORMAL", .value = "0" };
                if(debug_normal) {
                    normal_debug.value = "1";
                }
                
                raster_pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
                    .vertex_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/normal_mapping/shader.glsl" }, },
                        .compile_options = {
                            .defines = { 
                                normal_mapping,
                                derivatives,
                                normal_debug
                            }
                        } 
                    },
                    .fragment_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/normal_mapping/shader.glsl" }, },
                        .compile_options = {
                            .defines = { 
                                normal_mapping,
                                derivatives,
                                normal_debug
                            }
                        } 
                    },
                    .color_attachments = {{ .format = swapchain.get_format() }},
                    .depth_test = {
                        .depth_attachment_format = daxa::Format::D32_SFLOAT,
                        .enable_depth_test = true,
                        .enable_depth_write = true,
                    },
                    .raster = {
                        .face_culling = daxa::FaceCullFlagBits::NONE
                    },
                    .push_constant_size = sizeof(DrawPush),
                }).value();
            }
            
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
    NormalMappingApp app;
    app.update();
    return 0;
}