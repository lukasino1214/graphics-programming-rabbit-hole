#include "../app.hpp"
#include <glm/glm.hpp>

#include "shared.inl"

struct RenderTask {
    struct Uses {
        daxa::ImageComputeShaderWrite<> render_image = {};
    } uses = {};

    std::string_view name = "render";

    ComputePipelineHolder* pipeline = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        u32 size_x = ti.get_device().info_image(uses.render_image.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.render_image.image()).size.y;

        cmd_list.set_pipeline(*pipeline->pipeline);
        cmd_list.push_constant(ComputeDraw{
            .image = uses.render_image.view(),
            .frame_dim = { size_x, size_y }
        });
        cmd_list.dispatch((size_x + 7) / 8, (size_y + 7) / 8);
    }
};

struct BlitToSwapChain {
    struct Uses {
        daxa::ImageTransferRead<> render_image = {};
        daxa::ImageTransferWrite<> swapchain_image = {};
    } uses = {};

    std::string_view name = "blit render image to spawchain image";

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        u32 size_x = ti.get_device().info_image(uses.render_image.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.render_image.image()).size.y;

        cmd_list.blit_image_to_image({
            .src_image = uses.render_image.image(),
            .src_image_layout = daxa::ImageLayout::TRANSFER_SRC_OPTIMAL,
            .dst_image = uses.swapchain_image.image(),
            .dst_image_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .src_offsets = {{{0, 0, 0}, {static_cast<i32>(size_x), static_cast<i32>(size_y), 1}}},
            .dst_offsets = {{{0, 0, 0}, {static_cast<i32>(size_x), static_cast<i32>(size_y), 1}}},
        });
    }
};

struct ComputeTriangleApp : public App {
    ComputePipelineHolder compute_pipeline;
    daxa::ImageId render_image;

    daxa::TaskImage task_render_image = {};
    daxa::TaskImage task_swapchain_image = {};
    daxa::TaskGraph render_task_graph = {};

    ComputeTriangleApp() : App("Compute Triangle Example") {
        compute_pipeline.pipeline = pipeline_manager.add_compute_pipeline(daxa::ComputePipelineCompileInfo {
            .shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/compute_triangle/shader.glsl" }, },
            },
            .push_constant_size = sizeof(ComputeDraw),
            .name = "compute pipeline"
        }).value();

        render_image = device.create_image(daxa::ImageInfo{
            .format = daxa::Format::R8G8B8A8_UNORM,
            .size = {size_x, size_y, 1},
            .usage = daxa::ImageUsageFlagBits::SHADER_STORAGE | daxa::ImageUsageFlagBits::TRANSFER_SRC,
            .name = "render_image",
        });

        task_render_image = daxa::TaskImage { daxa::TaskImageInfo {
            .initial_images = {.images = std::span{&render_image, 1}},
            .swapchain_image = false,
            .name = "task render image"
        }};  

        task_swapchain_image = daxa::TaskImage{{.swapchain_image = true, .name = "swapchain image"}};

        render_task_graph = daxa::TaskGraph({
            .device = device,
            .swapchain = swapchain,
            .name = "render task graph" 
        });

        render_task_graph.use_persistent_image(task_render_image);
        render_task_graph.use_persistent_image(task_swapchain_image);

        render_task_graph.add_task(RenderTask {
            .uses = {
                .render_image = task_render_image,
            },
            .pipeline = &compute_pipeline,
        });

        render_task_graph.add_task(BlitToSwapChain {
            .uses = {
                .render_image = task_render_image,
                .swapchain_image = task_swapchain_image,
            },
        });

        render_task_graph.submit({});
        render_task_graph.present({});
        render_task_graph.complete({});
    }

    ~ComputeTriangleApp() {
        device.wait_idle();
        device.collect_garbage();
        device.destroy_image(render_image);
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
                .usage = daxa::ImageUsageFlagBits::SHADER_STORAGE | daxa::ImageUsageFlagBits::TRANSFER_SRC,
                .name = "render_image",
            });
            task_render_image.set_images({.images = std::span{&render_image, 1}});
        }
    }

};

auto main() -> i32 {
    ComputeTriangleApp app;
    app.update();
    return 0;
}