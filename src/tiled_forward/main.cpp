#include "../app.hpp"
#include "../camera.hpp"

#include <glm/glm.hpp>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

#include "shared.inl"

#include "../model.hpp"

#include <random>

struct ForwardApp : public App {
    std::unique_ptr<Model> model;
    std::shared_ptr<daxa::RasterPipeline> depth_prepass_pipeline;
    std::shared_ptr<daxa::ComputePipeline> culling_pipeline;
    std::shared_ptr<daxa::RasterPipeline> raster_pipeline;
    daxa::ImageId depth_image;
    daxa::SamplerId depth_sampler;

    daxa::BufferId camera_buffer;
    daxa::BufferId object_buffer;
    daxa::BufferId point_light_buffer;
    daxa::BufferId visible_point_light_indices_buffer;

    ControlledCamera3D camera;

    f64 current_frame = glfwGetTime();
    f64 last_frame = current_frame;
    f64 delta_time;
    bool paused = false;

    u32 work_groups_x = {};
    u32 work_groups_y = {};
    u32 number_of_tiles = {};

    ForwardApp() : App("Forward Example") {
        depth_prepass_pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/tiled_forward/depth_prepass.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/tiled_forward/depth_prepass.glsl" }, },
            },
            .depth_test = {
                .depth_attachment_format = daxa::Format::D32_SFLOAT,
                .enable_depth_test = true,
                .enable_depth_write = true,
            },
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::FRONT_BIT
            },
            .push_constant_size = sizeof(DepthPrepassPush),
            .name = "depth prepass pipeline"
        }).value();

        culling_pipeline = pipeline_manager.add_compute_pipeline(daxa::ComputePipelineCompileInfo {
            .shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/tiled_forward/light_culling.glsl" }, },
            },
            .push_constant_size = sizeof(CullingPush),
            .name = "culling pipeline"
        }).value();

        raster_pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/tiled_forward/raster.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/tiled_forward/raster.glsl" }, },
            },
            .color_attachments = {{ .format = swapchain.get_format() }},
            .depth_test = {
                .depth_attachment_format = daxa::Format::D32_SFLOAT,
                .enable_depth_test = true,
                .enable_depth_write = false,
            },
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::FRONT_BIT
            },
            .push_constant_size = sizeof(DrawPush),
            .name = "raster pipeline"
        }).value();

        depth_image = device.create_image({
            .format = daxa::Format::D32_SFLOAT,
            .aspect = daxa::ImageAspectFlagBits::DEPTH,
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_READ_ONLY,
        });

        camera.camera.resize(size_x, size_y);

        model = std::make_unique<Model>(device, "assets/Sponza/glTF/Sponza.gltf");

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

        work_groups_x = (size_x + (size_x % TILE_SIZE)) / TILE_SIZE;
        work_groups_y = (size_y + (size_y % TILE_SIZE)) / TILE_SIZE;
        number_of_tiles = work_groups_x * work_groups_y;

        daxa::BufferId staging_point_light_buffer = device.create_buffer(daxa::BufferInfo {
            .size = sizeof(PointLight) * NUM_LIGHTS,
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "staging point light buffer"
        });

        point_light_buffer = device.create_buffer(daxa::BufferInfo {
            .size = sizeof(PointLight) * NUM_LIGHTS,
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
            .name = "point light buffer"
        });

        visible_point_light_indices_buffer = device.create_buffer(daxa::BufferInfo {
            .size = static_cast<u32>(sizeof(PointLight) * NUM_LIGHTS * number_of_tiles),
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
            .name = "visible point light buffer"
        });

        const glm::vec3 LIGHT_MIN_BOUNDS = glm::vec3(-20.0f, -20.0f, -20.0f);
        const glm::vec3 LIGHT_MAX_BOUNDS = glm::vec3(20.0f, 20.0f, 20.0f);

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0, 1);

        auto random_position = [&](std::uniform_real_distribution<> dis, std::mt19937 gen) -> f32vec3 {
            f32vec3 position;
            for (int i = 0; i < 3; i++) {
                float min = LIGHT_MIN_BOUNDS[i];
                float max = LIGHT_MAX_BOUNDS[i];
                position[i] = (GLfloat)dis(gen) * (max - min) + min;
            }

            return position;
        };

        std::vector<PointLight> point_lights = {};
        point_lights.resize(NUM_LIGHTS);

        for (int i = 0; i < NUM_LIGHTS; i++) {
            PointLight &light = point_lights[i];        
            light.position = random_position(dis, gen);
            light.color = f32vec3{static_cast<float>(1.0f + dis(gen)), static_cast<float>(1.0f + dis(gen)), static_cast<float>(1.0f + dis(gen))};
            light.radius = 8.0f;
            // light.position = { 0.0f,0.0f, 0.0f };
            // light.color = { 1.0f, 0.0f, 0.0f };
            // light.radius = 30.0f;
        }

        daxa::CommandList cmd_list = device.create_command_list({ .name = "upload cmd list"});

        auto* ptr = device.get_host_address_as<PointLight>(staging_point_light_buffer);
        std::memcpy(ptr, point_lights.data(), NUM_LIGHTS * sizeof(PointLight));

        cmd_list.copy_buffer_to_buffer(daxa::BufferCopyInfo {
            .src_buffer = staging_point_light_buffer,
            .src_offset = 0,
            .dst_buffer = point_light_buffer,
            .dst_offset = 0,
            .size = sizeof(PointLight) * NUM_LIGHTS,
        });

        cmd_list.destroy_buffer_deferred(staging_point_light_buffer);
        cmd_list.complete();
        device.submit_commands(daxa::CommandSubmitInfo {
            .command_lists = { cmd_list }
        });

        depth_sampler = device.create_sampler({
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
            .max_lod = 1.0f,
            .enable_unnormalized_coordinates = false,
        });
    }

    ~ForwardApp() {
        device.destroy_image(depth_image);
        device.destroy_buffer(camera_buffer);
        device.destroy_buffer(object_buffer);
        device.destroy_buffer(point_light_buffer);
        device.destroy_buffer(visible_point_light_indices_buffer);
        device.destroy_sampler(depth_sampler);
    }

    void render() {
        auto swapchain_image = swapchain.acquire_next_image();
        if(swapchain_image.is_empty()) { return; }

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

        glm::mat4 model_matrix = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 0.0f}) * glm::scale(glm::mat4{1.0f}, glm::vec3{0.1f, 0.1f, 0.1f});
        glm::mat4 normal_matrix = glm::transpose(glm::inverse(model_matrix));

        auto* object_ptr = device.get_host_address_as<ObjectInfo>(object_buffer);
        object_ptr->model_matrix = *reinterpret_cast<f32mat4x4*>(&model_matrix);
        object_ptr->normal_matrix = *reinterpret_cast<f32mat4x4*>(&normal_matrix);

        auto cmd_list = device.create_command_list({.name = "render command list"});

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
            .depth_attachment = {{
                .image_view = depth_image.default_view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = daxa::DepthValue{1.0f, 0},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*depth_prepass_pipeline);
        
        for(auto& primitive : model->primitives) {
            cmd_list.push_constant(DepthPrepassPush {
                .camera_info = device.get_device_address(camera_buffer),
                .object_info = device.get_device_address(object_buffer),
                .vertices = device.get_device_address(model->vertex_buffer),
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
            .dst_access = daxa::AccessConsts::COMPUTE_SHADER_READ,
            .src_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
            .image_slice = {.image_aspect = daxa::ImageAspectFlagBits::DEPTH },
            .image_id = depth_image
        });

        cmd_list.set_pipeline(*culling_pipeline);
        cmd_list.push_constant(CullingPush {
            .depth_image = depth_image.default_view(),
            .depth_sampler = depth_sampler,
            .point_light_buffer = device.get_device_address(point_light_buffer),
            .visible_point_light_indices = device.get_device_address(visible_point_light_indices_buffer),
            .camera_info = device.get_device_address(camera_buffer),
            .viewport_size = { static_cast<i32>(size_x), static_cast<i32>(size_y) },
            .tile_nums = { static_cast<i32>(work_groups_x), static_cast<i32>(work_groups_y) }
        });
        cmd_list.dispatch(work_groups_x, work_groups_y);

        cmd_list.pipeline_barrier(daxa::MemoryBarrierInfo {
            .src_access = daxa::AccessConsts::COMPUTE_SHADER_READ_WRITE,
            .dst_access = daxa::AccessConsts::FRAGMENT_SHADER_READ_WRITE
        });

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = swapchain_image.default_view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = std::array<float, 4>{0.2f, 0.4f, 1.0f, 1.0f},
            }},
            .depth_attachment = {{
                .image_view = depth_image.default_view(),
                .load_op = daxa::AttachmentLoadOp::LOAD,
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
                .point_light_buffer = device.get_device_address(point_light_buffer),
                .visible_point_light_indices = device.get_device_address(visible_point_light_indices_buffer),
                .material_index = primitive.material_index,
                .tile_nums = { static_cast<i32>(work_groups_x), static_cast<i32>(work_groups_y) }
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

            // std::cout << delta_time << std::endl;

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
                .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_READ_ONLY,
            });

            work_groups_x = (size_x + (size_x % 16)) / 16;
            work_groups_y = (size_y + (size_y % 16)) / 16;
            number_of_tiles = work_groups_x * work_groups_y;

            device.destroy_buffer(visible_point_light_indices_buffer);
            visible_point_light_indices_buffer = device.create_buffer(daxa::BufferInfo {
                .size = static_cast<u32>(sizeof(PointLight) * NUM_LIGHTS * number_of_tiles),
                .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
                .name = "visible point light buffer"
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
    ForwardApp app;
    app.update();
    return 0;
}