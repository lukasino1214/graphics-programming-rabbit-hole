#pragma once

#include <daxa/daxa.hpp>
#include <daxa/utils/pipeline_manager.hpp>
#include <daxa/utils/task_graph.hpp>
using namespace daxa::types;
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_NATIVE_INCLUDE_NONE
using HWND = void*;
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#include <GLFW/glfw3native.h>

struct RasterPipelineHolder {
    std::shared_ptr<daxa::RasterPipeline> pipeline = {};
};

struct ComputePipelineHolder {
    std::shared_ptr<daxa::ComputePipeline> pipeline = {};
};

struct App {
    App(const std::string_view& name) {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfw_window_ptr =
            glfwCreateWindow(static_cast<i32>(size_x), static_cast<i32>(size_y),
                             name.data(), nullptr, nullptr);
        glfwSetWindowUserPointer(glfw_window_ptr, this);
        glfwSetWindowSizeCallback(
            glfw_window_ptr, [](GLFWwindow* window_ptr, i32 sx, i32 sy) {
                auto& app =
                    *reinterpret_cast<App*>(glfwGetWindowUserPointer(window_ptr));
                app.resize(static_cast<u32>(sx), static_cast<u32>(sy));
            });
        glfwSetCursorPosCallback(
            glfw_window_ptr, [](GLFWwindow* window_ptr, f64 x, f64 y) {
                auto& app =
                    *reinterpret_cast<App*>(glfwGetWindowUserPointer(window_ptr));
                app.on_mouse_move(static_cast<f32>(x), static_cast<f32>(y));
            });
        glfwSetScrollCallback(
            glfw_window_ptr, [](GLFWwindow* window_ptr, f64 x, f64 y) {
                auto& app =
                    *reinterpret_cast<App*>(glfwGetWindowUserPointer(window_ptr));
                app.on_mouse_scroll(static_cast<f32>(x), static_cast<f32>(y));
            });
        glfwSetMouseButtonCallback(
            glfw_window_ptr, [](GLFWwindow* window_ptr, i32 key, i32 action, i32) {
                auto& app =
                    *reinterpret_cast<App*>(glfwGetWindowUserPointer(window_ptr));
                app.on_mouse_button(key, action);
            });
        glfwSetKeyCallback(glfw_window_ptr, [](GLFWwindow* window_ptr, i32 key, i32,
                                               i32 action, i32) {
            auto& app =
                *reinterpret_cast<App*>(glfwGetWindowUserPointer(window_ptr));
            app.on_key(key, action);
        });

        this->instance = daxa::create_instance(daxa::InstanceInfo{
            .enable_validation = true
        });

        this->device = instance.create_device(daxa::DeviceInfo {
            .enable_buffer_device_address_capture_replay = true,
            .name = "my device"
        });

        this->swapchain = device.create_swapchain({
            .native_window = get_native_handle(glfw_window_ptr),
            .present_mode = daxa::PresentMode::IMMEDIATE,
            .image_usage = daxa::ImageUsageFlagBits::TRANSFER_DST,
            .name = "swapchain"
        });

        this->pipeline_manager = daxa::PipelineManager({
            .device = device,
            .shader_compile_options = {
                .root_paths = {
                    DAXA_SHADER_INCLUDE_DIR,
                    "./",
                },
                .language = daxa::ShaderLanguage::GLSL,
                .enable_debug_info = true,
            },
            .name = "pipeline_manager",
        });
    }

    ~App() {
        glfwDestroyWindow(glfw_window_ptr);
        glfwTerminate();
    }

    auto get_native_platform() -> daxa::NativeWindowPlatform {
        switch (glfwGetPlatform()) {
            case GLFW_PLATFORM_WIN32:
                return daxa::NativeWindowPlatform::WIN32_API;
            case GLFW_PLATFORM_X11:
                return daxa::NativeWindowPlatform::XLIB_API;
            case GLFW_PLATFORM_WAYLAND:
                return daxa::NativeWindowPlatform::WAYLAND_API;
            default:
                return daxa::NativeWindowPlatform::UNKNOWN;
        }
    }

    auto get_native_handle(GLFWwindow* glfw_window_ptr)
        -> daxa::NativeWindowHandle {
#if defined(_WIN32)
        return glfwGetWin32Window(glfw_window_ptr);
#elif defined(__linux__)
        switch (get_native_platform()) {
            case daxa::NativeWindowPlatform::WAYLAND_API:
                return reinterpret_cast<daxa::NativeWindowHandle>(
                    glfwGetWaylandWindow(glfw_window_ptr));
            case daxa::NativeWindowPlatform::XLIB_API:
            default:
                return reinterpret_cast<daxa::NativeWindowHandle>(
                    glfwGetX11Window(glfw_window_ptr));
        }
#endif
    }

    virtual void resize(u32 x, u32 y) {
        minimized = (x == 0 || y == 0);
        if (!minimized) {
            swapchain.resize();
            size_x = swapchain.get_surface_extent().x;
            size_y = swapchain.get_surface_extent().y;
        }
    }

    virtual void on_mouse_move(f32 x, f32 y) {}
    virtual void on_mouse_scroll(f32 x, f32 y) {}
    virtual void on_mouse_button(i32 key, i32 action) {}
    virtual void on_key(i32 key, i32 action) {}

    GLFWwindow* glfw_window_ptr = {};
    u32 size_x = 800, size_y = 600;
    bool minimized = false;

    daxa::Instance instance;
    daxa::Device device;
    daxa::Swapchain swapchain;
    daxa::PipelineManager pipeline_manager;
};
