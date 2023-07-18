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

struct RenderTask {
    struct Uses {
        daxa::ImageColorAttachment<> render_target = {};
        daxa::ImageDepthAttachment<> depth_target = {};
    } uses = {};

    std::string_view name = "render";
    RasterPipelineHolder* pipeline = {};
    std::unique_ptr<Model>* model = {};
    daxa::ImGuiRenderer imgui_renderer = {};
    daxa::BufferId camera_buffer = {};
    daxa::BufferId object_buffer = {};
    Texture* heightmap_texture = {};
    glm::vec3* light_position = {};
    f32* height_scale = {};
    f32* parallax_bias = {};
    f32* layers = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        daxa::Device device = ti.get_device();

        u32 size_x = ti.get_device().info_image(uses.render_target.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.render_target.image()).size.y;

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = uses.render_target.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = std::array<float, 4>{0.2f, 0.4f, 1.0f, 1.0f},
            }},
            .depth_attachment = {{
                .image_view = uses.depth_target.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = daxa::DepthValue{1.0f, 0},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*pipeline->pipeline);

        for(auto& primitive : (*model)->primitives) {
            cmd_list.push_constant(DrawPush {
                .camera_info = device.get_device_address(camera_buffer),
                .object_info = device.get_device_address(object_buffer),
                .vertices = device.get_device_address((*model)->vertex_buffer),
                .materials = device.get_device_address((*model)->material_buffer),
                .material_index = primitive.material_index,
                .heightmap_texture = heightmap_texture->get_texture_id(),
                .light_position = *reinterpret_cast<f32vec3*>(light_position),
                .height_scale = *height_scale,
                .parallax_bias = *parallax_bias,
                .layers = *layers
            });


            if(primitive.index_count > 0) {
                cmd_list.set_index_buffer((*model)->index_buffer, 0);
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

        imgui_renderer.record_commands(ImGui::GetDrawData(), cmd_list, uses.render_target.image(), size_x, size_y);
    }
};

struct ParallaxMappingApp : public App {
    std::unique_ptr<Model> model = {};
    RasterPipelineHolder raster_pipeline = {};
    daxa::ImageId depth_image = {};
    daxa::TaskImage task_depth_image = {};

    daxa::BufferId camera_buffer = {};
    daxa::BufferId object_buffer = {};

    daxa::TaskImage task_swapchain_image = {};
    daxa::TaskGraph render_task_graph = {};

    std::unique_ptr<Texture> heightmap_texture = {};

    ControlledCamera3D camera;

    f64 current_frame = glfwGetTime();
    f64 last_frame = current_frame;
    f64 delta_time;
    bool paused = false;

    glm::vec3 light_position = { 0.5f, 1.0f, 0.3f };
    f32 height_scale = 0.1f;
    f32 parallax_bias = 0.1f;
    f32 layers = 16.0f;

    i32 model_index = 0;
    std::vector<const char*> models = { "parallax cube", "brick wall" };

    i32 mapping_mode = 0;
    std::vector<const char*> mapping_modes = {
        "Color only",
		"Normal Mapping",
		"Offset Limiting / Parallax Mapping",
		"Steep Parallax Mapping",
		"Parallax Occlusion Mapping",
        "Relief Parallax Mapping",
        "Contact Refinement Parallax Mapping"
    };

    daxa::ImGuiRenderer imgui_renderer;

    ParallaxMappingApp() : App("Parallax Mapping Example") {
        raster_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/parallax_mapping/shader.glsl" }, },
                .compile_options = {
                    .defines = { 
                        { .name = "MAPPING_MODE", .value = "0" },
                    }
                } 
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/parallax_mapping/shader.glsl" }, },
                .compile_options = {
                    .defines = { 
                        { .name = "MAPPING_MODE", .value = "0" },
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
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT,
        });

        task_depth_image = daxa::TaskImage { daxa::TaskImageInfo {
            .initial_images = {.images = std::span{&depth_image, 1}},
            .swapchain_image = false,
            .name = "task depth image"
        }};

        camera.camera.resize(size_x, size_y);

        model = std::make_unique<Model>(device, "assets/parallax_cube/parallax_cube.gltf");
        heightmap_texture = std::make_unique<Texture>(device, "assets/parallax_cube/parallax_cube_heightmap.png", Texture::Type::UNORM);
        
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

        render_task_graph = daxa::TaskGraph({
            .device = device,
            .swapchain = swapchain,
            .name = "render task graph" 
        });

        task_swapchain_image = daxa::TaskImage{{.swapchain_image = true, .name = "swapchain image"}};

        render_task_graph.use_persistent_image(task_depth_image);
        render_task_graph.use_persistent_image(task_swapchain_image);

        render_task_graph.add_task(RenderTask {
            .uses = {
                .render_target = task_swapchain_image,
                .depth_target = task_depth_image
            },
            .pipeline = &raster_pipeline,
            .model = &model,
            .imgui_renderer = imgui_renderer,
            .camera_buffer = camera_buffer,
            .object_buffer = object_buffer,
            .heightmap_texture = heightmap_texture.get(),
            .light_position = &light_position,
            .height_scale = &height_scale,
            .parallax_bias = &parallax_bias,
            .layers = &layers
        });

        render_task_graph.submit({});
        render_task_graph.present({});
        render_task_graph.complete({});
    }

    ~ParallaxMappingApp() {
        device.destroy_image(depth_image);
        device.destroy_buffer(camera_buffer);
        device.destroy_buffer(object_buffer);
    }

    void render() {
        auto swapchain_image = swapchain.acquire_next_image();
        task_swapchain_image.set_images({.images = std::span{&swapchain_image, 1}});
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

        glm::mat4 model_matrix = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 0.0f}) * glm::scale(glm::mat4{1.0f}, glm::vec3{1.0f, 1.0f, 1.0f}) * glm::rotate(glm::mat4{1.0}, glm::radians(90.0f), glm::vec3{1.0f, 0.0f, 0.0f});
        glm::mat4 normal_matrix = glm::transpose(glm::inverse(model_matrix));

        auto* object_ptr = device.get_host_address_as<ObjectInfo>(object_buffer);
        object_ptr->model_matrix = *reinterpret_cast<f32mat4x4*>(&model_matrix);
        object_ptr->normal_matrix = *reinterpret_cast<f32mat4x4*>(&normal_matrix);

        render_task_graph.execute({});
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

            ImGui::Begin("Parallax Mapping Settings");
            ImGui::DragFloat3("light position", &light_position.x, 0.05f, -1.5f, 1.5f);
            ImGui::DragFloat("height scale", &height_scale, 0.05f, 0.001f, 1.0f);
            ImGui::DragFloat("parallax bias", &parallax_bias, 0.01f, -0.25f, 0.25f);
            ImGui::DragFloat("layers", &layers, 1.0f, 0.0f, 64.0f);
            
            if(ImGui::Combo("model", &model_index, models.data(), static_cast<u32>(models.size()), static_cast<u32>(models.size()))) {
                if(model_index == 0) {
                    model = std::make_unique<Model>(device, "assets/parallax_cube/parallax_cube.gltf");
                    heightmap_texture = std::make_unique<Texture>(device, "assets/parallax_cube/parallax_cube_heightmap.png", Texture::Type::UNORM);
                }

                if(model_index == 1) {
                    model = std::make_unique<Model>(device, "assets/brick_wall/brick_wall.gltf");
                    heightmap_texture = std::make_unique<Texture>(device, "assets/brick_wall/brick_wall_heightmap.png", Texture::Type::UNORM);
                }
            }

            if(ImGui::Combo("mapping mode", &mapping_mode, mapping_modes.data(), static_cast<u32>(mapping_modes.size()), static_cast<u32>(mapping_modes.size()))) {
                daxa::ShaderDefine mapping_mode_define = { .name = "MAPPING_MODE", .value = std::to_string(mapping_mode) };
                
                raster_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
                    .vertex_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/parallax_mapping/shader.glsl" }, },
                        .compile_options = {
                            .defines = { mapping_mode_define }
                        } 
                    },
                    .fragment_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/parallax_mapping/shader.glsl" }, },
                        .compile_options = {
                            .defines = { mapping_mode_define }
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
                .size = { size_x, size_y, 1 },
                .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT,
            });
            task_depth_image.set_images({.images = std::span{&depth_image, 1}});

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
    ParallaxMappingApp app;
    app.update();
    return 0;
}