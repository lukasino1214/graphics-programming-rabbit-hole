#include "../app.hpp"
#include <glm/glm.hpp>

#include "shared.inl"

struct BasicComputeApp : public App {
    std::shared_ptr<daxa::ComputePipeline> compute_pipeline;
    daxa::ImageId render_image;

    BasicComputeApp() : App("Basic Compute Example") {
        compute_pipeline = pipeline_manager.add_compute_pipeline(daxa::ComputePipelineCompileInfo {
            .shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/basic_compute/shader.glsl" }, },
            },
            .push_constant_size = sizeof(ComputeDraw),
            .name = "compute pipeline"
        }).value();

        render_image = device.create_image(daxa::ImageInfo{
            .format = daxa::Format::R8G8B8A8_UNORM,
            .size = {size_x, size_y, 1},
            .usage = daxa::ImageUsageFlagBits::SHADER_READ_WRITE | daxa::ImageUsageFlagBits::TRANSFER_SRC,
            .name = "render_image",
        });
    }

    ~BasicComputeApp() {
        device.destroy_image(render_image);
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
            .dst_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .image_id = render_image
        });

        cmd_list.set_pipeline(*compute_pipeline);
        cmd_list.push_constant(ComputeDraw{
            .image = render_image.default_view(),
            .frame_dim = { size_x, size_y }
        });
        cmd_list.dispatch(size_x, size_y);

        cmd_list.pipeline_barrier_image_transition(daxa::ImageBarrierInfo {
            .dst_access = daxa::AccessConsts::ALL_GRAPHICS_READ_WRITE,
            .src_layout = daxa::ImageLayout::ATTACHMENT_OPTIMAL,
            .dst_layout = daxa::ImageLayout::TRANSFER_SRC_OPTIMAL,
            .image_id = render_image
        });

        cmd_list.pipeline_barrier_image_transition(daxa::ImageBarrierInfo {
            .dst_access = daxa::AccessConsts::ALL_GRAPHICS_READ_WRITE,
            .src_layout = daxa::ImageLayout::UNDEFINED,
            .dst_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .image_id = swapchain_image
        });

        cmd_list.blit_image_to_image({
            .src_image = render_image,
            .src_image_layout = daxa::ImageLayout::TRANSFER_SRC_OPTIMAL,
            .dst_image = swapchain_image,
            .dst_image_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .src_offsets = {{{0, 0, 0}, {static_cast<i32>(size_x), static_cast<i32>(size_y), 1}}},
            .dst_offsets = {{{0, 0, 0}, {static_cast<i32>(size_x), static_cast<i32>(size_y), 1}}},
        });

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

    void resize(u32 x, u32 y) override {
        minimized = (x == 0 || y == 0);
        if (!minimized) {
            swapchain.resize();
            size_x = swapchain.get_surface_extent().x;
            size_y = swapchain.get_surface_extent().y;
        
            device.destroy_image(render_image);
            render_image = device.create_image(daxa::ImageInfo{
                .format = daxa::Format::R8G8B8A8_UNORM,
                .size = {size_x, size_y, 1},
                .usage = daxa::ImageUsageFlagBits::SHADER_READ_WRITE | daxa::ImageUsageFlagBits::TRANSFER_SRC,
                .name = "render_image",
            });
        }
    }
};

auto main() -> i32 {
    BasicComputeApp app;
    app.update();
    return 0;
}