#include "../app.hpp"
#include <glm/glm.hpp>

#include "shared.inl"

struct UploadVertexTask {
    struct Uses {
        daxa::BufferTransferWrite vertex_buffer = {};
    } uses = {};

    std::string_view name = "upload vertices";

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();

        auto staging_buffer_id = ti.get_device().create_buffer(daxa::BufferInfo {
            .size = 3 * sizeof(Vertex),
            .allocate_info = daxa::AutoAllocInfo{daxa::MemoryFlagBits::HOST_ACCESS_RANDOM},
            .name = "staging buffer"
        });

        cmd_list.destroy_buffer_deferred(staging_buffer_id);

        auto ptr = ti.get_device().get_host_address_as<Vertex>(staging_buffer_id);
        *ptr++ = { { 0.0f, -0.5f }, { 1.0f, 0.0f, 0.0f }};
        *ptr++ = { { 0.5f, 0.5f }, { 0.0f, 1.0f, 0.0f }};
        *ptr++ = { { -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f }};

        cmd_list.copy_buffer_to_buffer({
            .src_buffer = staging_buffer_id,
            .dst_buffer = uses.vertex_buffer.buffer(),
            .size = 3 * sizeof(Vertex),
        });
    }
};

struct RenderTask {
    struct Uses {
        daxa::BufferVertexShaderRead vertex_buffer = {};
        daxa::ImageColorAttachment<> color_target = {};
    } uses = {};

    std::string_view name = "render";
    RasterPipelineHolder* pipeline = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        u32 size_x = ti.get_device().info_image(uses.color_target.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.color_target.image()).size.y;
        
        cmd_list.begin_renderpass({
            .color_attachments = {
                {
                    .image_view = uses.color_target.view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<daxa::f32, 4>{0.2f, 0.4f, 1.0f, 1.0f},
                },
            },
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });

        cmd_list.set_pipeline(*pipeline->pipeline);
        cmd_list.push_constant(DrawPush {
            .vertices = ti.get_device().get_device_address(uses.vertex_buffer.buffer()),
        });

        cmd_list.draw({.vertex_count = 3});
        cmd_list.end_renderpass();
    }
};

struct TriangleApp : public App {
    daxa::BufferId vertex_buffer = {};
    RasterPipelineHolder raster_pipeline = {};

    daxa::TaskBuffer task_buffer = {};
    daxa::TaskImage task_swapchain_image = {};
    daxa::TaskGraph render_task_graph = {};

    TriangleApp() : App("Triangle Example") {
        vertex_buffer = device.create_buffer(daxa::BufferInfo {
            .size = 3 * sizeof(Vertex),
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
            .name = "vertex buffer"
        });

        task_buffer = daxa::TaskBuffer({
            .initial_buffers = {.buffers = std::span{&vertex_buffer, 1}},
            .name = "task buffer",
        });

        auto upload_task_graph = daxa::TaskGraph({
            .device = device,
            .name = "upload task graph",
        });

        upload_task_graph.use_persistent_buffer(task_buffer);

        upload_task_graph.add_task(UploadVertexTask{
            .uses = {
                .vertex_buffer = task_buffer,
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

        render_task_graph.use_persistent_buffer(task_buffer);
        render_task_graph.use_persistent_image(task_swapchain_image);

        render_task_graph.add_task(RenderTask {
            .uses = {
                .vertex_buffer = task_buffer,
                .color_target = task_swapchain_image,
            },
            .pipeline = &raster_pipeline,
        });

        render_task_graph.submit({});
        render_task_graph.present({});
        render_task_graph.complete({});

        raster_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/triangle/shader.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/triangle/shader.glsl" }, },
            },
            .color_attachments = {{ .format = swapchain.get_format() }},
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::NONE
            },
            .push_constant_size = sizeof(DrawPush),
        }).value();
    }

    ~TriangleApp() {
        device.wait_idle();
        device.collect_garbage();
        device.destroy_buffer(vertex_buffer);
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
    TriangleApp app;
    app.update();
    return 0;
}