#include "../app.hpp"
#include "../camera.hpp"

#include <glm/glm.hpp>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

#include "shared.inl"

struct UploadVertexTask {
    struct Uses {
        daxa::BufferTransferWrite vertex_buffer = {};
    } uses = {};

    std::string_view name = "upload vertices";

    void callback(daxa::TaskInterface ti) {
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

        daxa::CommandList cmd_list = ti.get_command_list();

        auto staging_buffer_id = ti.get_device().create_buffer(daxa::BufferInfo {
            .size = static_cast<u32>(vertices.size() * sizeof(Vertex)),
            .allocate_info = daxa::AutoAllocInfo{daxa::MemoryFlagBits::HOST_ACCESS_RANDOM},
            .name = "staging buffer"
        });

        cmd_list.destroy_buffer_deferred(staging_buffer_id);

        auto ptr = ti.get_device().get_host_address_as<Vertex>(staging_buffer_id);
        std::memcpy(ptr, vertices.data(), vertices.size() * sizeof(Vertex));

        cmd_list.copy_buffer_to_buffer({
            .src_buffer = staging_buffer_id,
            .dst_buffer = uses.vertex_buffer.buffer(),
            .size = static_cast<u32>(vertices.size() * sizeof(Vertex)),
        });
    }
};

struct RenderTask {
    struct Uses {
        daxa::BufferVertexShaderRead vertex_buffer = {};
        daxa::ImageColorAttachment<> render_target = {};
        daxa::ImageDepthAttachment<> depth_image = {};
    } uses = {};

    std::string_view name = "render";
    RasterPipelineHolder* pipeline = {};
    ControlledCamera3D* camera = {};

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
            .depth_attachment = {{
                .image_view = uses.depth_image.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = daxa::DepthValue{1.0f, 0},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*pipeline->pipeline);

        glm::mat4 model = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 0.0f});
        glm::mat4 mvp = camera->camera.get_vp() * model;

        cmd_list.push_constant(DrawPush {
            .mvp = *reinterpret_cast<f32mat4x4*>(&mvp),
            .vertices = ti.get_device().get_device_address(uses.vertex_buffer.buffer())
        });
        cmd_list.draw({ .vertex_count = 36});
        cmd_list.end_renderpass();
    }
};

struct BasicForwardApp : public App {
    daxa::BufferId vertex_buffer = {};
    daxa::TaskBuffer task_vertex_buffer = {};
    RasterPipelineHolder raster_pipeline = {};
    daxa::ImageId depth_image = {};
    daxa::TaskImage task_depth_image = {};
    daxa::TaskImage task_swapchain_image = {};
    daxa::TaskGraph render_task_graph = {};

    ControlledCamera3D camera;

    f64 current_frame = glfwGetTime();
    f64 last_frame = current_frame;
    f64 delta_time;
    bool paused = false;

    BasicForwardApp() : App("Basic Forward Example") {
        vertex_buffer = device.create_buffer({
            .size = static_cast<u32>(36 * sizeof(Vertex)),
            .allocate_info = daxa::AutoAllocInfo{daxa::MemoryFlagBits::DEDICATED_MEMORY},
            .name = "vertex buffer"
        });

        raster_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
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
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT,
        });

        task_depth_image = daxa::TaskImage { daxa::TaskImageInfo {
            .initial_images = {.images = std::span{&depth_image, 1}},
            .swapchain_image = false,
            .name = "task render image"
        }};

        task_vertex_buffer = daxa::TaskBuffer({
            .initial_buffers = {.buffers = std::span{&vertex_buffer, 1}},
            .name = "task buffer",
        });

        auto upload_task_graph = daxa::TaskGraph({
            .device = device,
            .name = "upload task graph",
        });

        upload_task_graph.use_persistent_buffer(task_vertex_buffer);

        upload_task_graph.add_task(UploadVertexTask{
            .uses = {
                .vertex_buffer = task_vertex_buffer,
            },
        });

        upload_task_graph.submit({});
        upload_task_graph.complete({});
        upload_task_graph.execute({});

        task_swapchain_image = daxa::TaskImage{{.swapchain_image = true, .name = "swapchain image"}};

        render_task_graph = daxa::TaskGraph({
            .device = device,
            .swapchain = swapchain,
            .name = "render task graph" 
        });

        render_task_graph.use_persistent_buffer(task_vertex_buffer);
        render_task_graph.use_persistent_image(task_swapchain_image);
        render_task_graph.use_persistent_image(task_depth_image);

        render_task_graph.add_task(RenderTask {
            .uses = {
                .vertex_buffer = task_vertex_buffer,
                .render_target = task_swapchain_image,
                .depth_image = task_depth_image
            },
            .pipeline = &raster_pipeline,
            .camera = &camera
        });

        render_task_graph.submit({});
        render_task_graph.present({});
        render_task_graph.complete({});

        camera.camera.resize(size_x, size_y);
    }

    ~BasicForwardApp() {
        device.wait_idle();
        device.collect_garbage();
        device.destroy_buffer(vertex_buffer);
        device.destroy_image(depth_image);
    }

    void render() {
        auto swapchain_image = swapchain.acquire_next_image();
        task_swapchain_image.set_images({.images = std::span{&swapchain_image, 1}});
        if(swapchain_image.is_empty()) { return; }

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
    BasicForwardApp app;
    app.update();
    return 0;
}