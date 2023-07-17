#include "../app.hpp"
#include "../camera.hpp"

#include <glm/glm.hpp>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

#include "shared.inl"

struct BasicForwardApp : public App {
    daxa::BufferId vertex_buffer;
    std::shared_ptr<daxa::RasterPipeline> raster_pipeline;
    daxa::ImageId depth_image;

    ControlledCamera3D camera;

    f64 current_frame = glfwGetTime();
    f64 last_frame = current_frame;
    f64 delta_time;
    bool paused = false;

    BasicForwardApp() : App("Basic Forward Example") {
        std::vector<Vertex> vertices = {
            {{-0.5f, -0.5f, -0.5f}},
            {{0.5f, -0.5f, -0.5f}},
            {{0.5f,  0.5f, -0.5f}},
            {{0.5f,  0.5f, -0.5f}},
            {{-0.5f,  0.5f, -0.5f}},
            {{-0.5f, -0.5f, -0.5f}},

            {{-0.5f, -0.5f,  0.5f}},
            {{0.5f, -0.5f,  0.5f}},
            {{0.5f,  0.5f,  0.5f}},
            {{0.5f,  0.5f,  0.5f}},
            {{-0.5f,  0.5f,  0.5f}},
            {{-0.5f, -0.5f,  0.5f}},

            {{-0.5f,  0.5f,  0.5f}},
            {{-0.5f,  0.5f, -0.5f}},
            {{-0.5f, -0.5f, -0.5f}},
            {{-0.5f, -0.5f, -0.5f}},
            {{-0.5f, -0.5f,  0.5f}},
            {{-0.5f,  0.5f,  0.5f}},

            {{0.5f,  0.5f,  0.5f}},
            {{0.5f,  0.5f, -0.5f}},
            {{0.5f, -0.5f, -0.5f}},
            {{0.5f, -0.5f, -0.5f}},
            {{0.5f, -0.5f,  0.5f}},
            {{0.5f,  0.5f,  0.5f}},

            {{-0.5f, -0.5f, -0.5f}},
            {{0.5f, -0.5f, -0.5f}},
            {{0.5f, -0.5f,  0.5f}},
            {{0.5f, -0.5f,  0.5f}},
            {{-0.5f, -0.5f,  0.5f}},
            {{-0.5f, -0.5f, -0.5f}},

            {{-0.5f,  0.5f, -0.5f}},
            {{0.5f,  0.5f, -0.5f}},
            {{0.5f,  0.5f,  0.5f}},
            {{0.5f,  0.5f,  0.5f}},
            {{-0.5f,  0.5f,  0.5f}},
            {{-0.5f,  0.5f, -0.5f}},
        };

        vertex_buffer = device.create_buffer({
            .size = static_cast<u32>(vertices.size() * sizeof(Vertex)),
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
            .name = "vertex buffer"
        });

        daxa::BufferId vertex_staging_buffer = device.create_buffer({
            .size = static_cast<u32>(vertices.size() * sizeof(Vertex)),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "staging vertex buffer"
        });

        {
            auto ptr = device.get_host_address_as<Vertex>(vertex_staging_buffer);
            std::memcpy(ptr, vertices.data(), vertices.size() * sizeof(Vertex));
        }


        auto cmd_list = device.create_command_list({.name = "upload command list"});

        cmd_list.copy_buffer_to_buffer( daxa::BufferCopyInfo {
            .src_buffer = vertex_staging_buffer,
            .src_offset = 0,
            .dst_buffer = vertex_buffer,
            .dst_offset = 0,
            .size = static_cast<u32>(vertices.size() * sizeof(Vertex))
        });

        cmd_list.complete();
        device.submit_commands({
            .command_lists = {std::move(cmd_list)},
        });
        device.wait_idle();
        device.destroy_buffer(vertex_staging_buffer);

        raster_pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/basic_forward/shader.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/basic_forward/shader.glsl" }, },
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
    }

    ~BasicForwardApp() {
        device.destroy_buffer(vertex_buffer);
        device.destroy_image(depth_image);
    }

    void render() {
        auto swapchain_image = swapchain.acquire_next_image();
        if(swapchain_image.is_empty()) { return; }

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

        glm::mat4 model = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 0.0f});

        glm::mat4 mvp = camera.camera.get_vp() * model;

        cmd_list.push_constant(DrawPush {
            .mvp = *reinterpret_cast<f32mat4x4*>(&mvp),
            .vertices = device.get_device_address(vertex_buffer)
        });
        cmd_list.draw({ .vertex_count = 36});
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
    BasicForwardApp app;
    app.update();
    return 0;
}