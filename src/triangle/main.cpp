#include "../app.hpp"
#include <glm/glm.hpp>

#include "shared.inl"

struct TriangleApp : public App {

    daxa::BufferId vertex_buffer;
    std::shared_ptr<daxa::RasterPipeline> raster_pipeline;

    TriangleApp() : App("Triangle Example") {
        vertex_buffer = device.create_buffer(daxa::BufferInfo {
            .size = 3 * sizeof(Vertex),
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
            .name = "vertex buffer"
        });

        daxa::BufferId staging_buffer = device.create_buffer({
            .size = 3 * sizeof(Vertex),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "staging vertex buffer"
        });

        auto ptr = device.get_host_address_as<Vertex>(staging_buffer);
        *ptr++ = { { 0.0f, -0.5f }, { 1.0f, 0.0f, 0.0f }};
        *ptr++ = { { 0.5f, 0.5f }, { 0.0f, 1.0f, 0.0f }};
        *ptr++ = { { -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f }};

        auto cmd_list = device.create_command_list(daxa::CommandListInfo{
            .name = "upload command list"
        });
        cmd_list.destroy_buffer_deferred(staging_buffer);

        cmd_list.copy_buffer_to_buffer( daxa::BufferCopyInfo {
            .src_buffer = staging_buffer,
            .src_offset = 0,
            .dst_buffer = vertex_buffer,
            .dst_offset = 0,
            .size = 3 * sizeof(Vertex)
        });

        cmd_list.complete();
        device.submit_commands(daxa::CommandSubmitInfo {
            .command_lists = {std::move(cmd_list)},
        });

        raster_pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
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
        device.destroy_buffer(vertex_buffer);
    }

    void render() {
        auto swapchain_image = swapchain.acquire_next_image();
        if(swapchain_image.is_empty()) { return; }

        auto cmd_list = device.create_command_list({
            .name = "render command list"
        });

        cmd_list.pipeline_barrier_image_transition(daxa::ImageBarrierInfo {
            .dst_access = daxa::AccessConsts::TRANSFER_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .image_id = swapchain_image
        });

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = swapchain_image.default_view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = std::array<float, 4>{0.2f, 0.4f, 1.0f, 1.0f},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*raster_pipeline);
        cmd_list.push_constant(DrawPush{
            .vertices = device.get_device_address(vertex_buffer)
        });
        cmd_list.draw({.vertex_count = 3});
        cmd_list.end_renderpass();

        cmd_list.pipeline_barrier_image_transition(daxa::ImageBarrierInfo {
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
            render();
        }
    }
};

auto main() -> i32 {
    TriangleApp app;
    app.update();
    return 0;
}