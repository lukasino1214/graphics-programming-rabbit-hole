#include "../app.hpp"
#include "../camera.hpp"

#include <daxa/types.hpp>
#include <glm/glm.hpp>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include "shared.inl"

#include "../model.hpp"

#include <daxa/utils/imgui.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>

struct RenderShadowTask {
    struct Uses {
        daxa::ImageDepthAttachment<> shadow_target = {};
    } uses = {};

    std::string_view name = "render shadow";
    RasterPipelineHolder* pipeline = {};
    Model* model = {};
    glm::mat4* light_matrix = {};
    daxa::BufferId matrices_buffer = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .depth_attachment = {{
                .image_view = uses.shadow_target.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = daxa::DepthValue{1.0f, 0},
            }},
            .render_area = {.x = 0, .y = 0, .width = 1024, .height = 1024},
        });
        cmd_list.set_pipeline(*pipeline->pipeline);

        glm::mat4 shadow_mvp = *light_matrix;

        for(auto& primitive : model->primitives) {
            cmd_list.push_constant(ShadowPush {
                .mvp = *reinterpret_cast<f32mat4x4*>(&shadow_mvp),
                .vertices = ti.get_device().get_device_address(model->vertex_buffer),
                .matrices = ti.get_device().get_device_address(matrices_buffer)
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
    }
};

struct GBufferGatherTask {
    struct Uses {
        daxa::ImageColorAttachment<> albedo_target = {};
        daxa::ImageColorAttachment<> normal_target = {};
        daxa::ImageDepthAttachment<> depth_target = {};
    } uses = {};

    std::string_view name = "g buffer gather";
    RasterPipelineHolder* pipeline = {};
    Model* model = {};
    ControlledCamera3D* camera = {};
    daxa::BufferId matrices_buffer = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        u32 size_x = ti.get_device().info_image(uses.albedo_target.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.albedo_target.image()).size.y;

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { 
                daxa::RenderAttachmentInfo {
                    .image_view = uses.albedo_target.view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<f32, 4>{0.2f, 0.4f, 1.0f, 1.0f},
                },
                daxa::RenderAttachmentInfo {
                    .image_view = uses.normal_target.view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<f32, 4>{0.0f, 0.0f, 0.0f, 0.0f},
                },
            },
            .depth_attachment = {{
                .image_view = uses.depth_target.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = daxa::DepthValue{1.0f, 0},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*pipeline->pipeline);

        for(auto& primitive : model->primitives) {
            cmd_list.push_constant(GBufferGatherPush {
                .matrices = ti.get_device().get_device_address(matrices_buffer),
                .vertices = ti.get_device().get_device_address(model->vertex_buffer),
                .materials = ti.get_device().get_device_address(model->material_buffer),
                .material_index = primitive.material_index,
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
    }
};

struct CompositionTask {
    struct Uses {
        daxa::ImageColorAttachment<> render_target = {};
        daxa::ImageShaderRead<> albedo_target = {};
        daxa::ImageShaderRead<> normal_target = {};
        daxa::ImageShaderRead<> depth_target = {};
        daxa::ImageShaderRead<> shadow_image = {};
    } uses = {};

    std::string_view name = "render";
    RasterPipelineHolder* pipeline = {};
    ControlledCamera3D* camera = {};
    daxa::BufferId light_buffer = {};
    daxa::BufferId matrices_buffer = {};
    daxa::ImGuiRenderer imgui_renderer = {};
    daxa::SamplerId sampler_id = {};

    f32* bias = {};
    i32* pcf_range = {};
    f32* shadow_intensity = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        u32 size_x = ti.get_device().info_image(uses.render_target.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.render_target.image()).size.y;

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = uses.render_target.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = std::array<f32, 4>{0.2f, 0.4f, 1.0f, 1.0f},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*pipeline->pipeline);

        cmd_list.push_constant(CompositionPush {
            .albedo_image = uses.albedo_target.view(),
            .normal_image = uses.normal_target.view(),
            .depth_image = uses.depth_target.view(),
            .sampler_id = sampler_id,
            .matrices = ti.get_device().get_device_address(matrices_buffer),
            .light_buffer = ti.get_device().get_device_address(light_buffer),
            .bias = *bias,
            .pcf_range = *pcf_range,
            .shadow_intensity = *shadow_intensity,
            .camera_position = *reinterpret_cast<f32vec3*>(&camera->position),
        });

        cmd_list.draw({ .vertex_count = 3 });

        cmd_list.end_renderpass();

        imgui_renderer.record_commands(ImGui::GetDrawData(), cmd_list, uses.render_target.image(), size_x, size_y);
    }
};

struct VolumetricLightingApp : public App {
    std::unique_ptr<Model> model = {};
    RasterPipelineHolder g_buffer_gather_pipeline = {};
    RasterPipelineHolder composition_pipeline = {};
    RasterPipelineHolder shadow_pipeline = {};

    daxa::ImageId depth_image = {};
    daxa::TaskImage task_depth_image = {};
    daxa::ImageId albedo_image = {};
    daxa::TaskImage task_albedo_image = {};
    daxa::ImageId normal_image = {};
    daxa::TaskImage task_normal_image = {};

    daxa::SamplerId sampler_id = {};

    daxa::ImageId shadow_image = {};
    daxa::TaskImage task_shadow_image = {};

    daxa::SamplerId shadow_sampler = {};
    daxa::BufferId light_buffer = {};
    daxa::BufferId matrices_buffer = {};

    glm::mat4 light_matrix = {};

    ControlledCamera3D camera;

    f64 current_frame = glfwGetTime();
    f64 last_frame = current_frame;
    f64 delta_time;
    bool paused = false;

    bool use_pcf = false;
    glm::vec3 direction = { -1.9, 0.0f, 0.0f };
    f32 bias = 0.0001f;
    i32 pcf_range = 1;
    f32 shadow_intensity = 0.1f;

    daxa::ImGuiRenderer imgui_renderer;

    daxa::TaskImage task_swapchain_image = {};
    daxa::TaskGraph render_task_graph = {};

    VolumetricLightingApp() : App("Volumetric Lighting Example") {
        g_buffer_gather_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/volumetric_lighting/g_buffer_gather.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/volumetric_lighting/g_buffer_gather.glsl" }, },
            },
            .color_attachments = {
                { .format = swapchain.get_format() }, // albedo image
                { .format = daxa::Format::R16G16B16A16_SFLOAT } // normal image
            },
            .depth_test = {
                .depth_attachment_format = daxa::Format::D32_SFLOAT,
                .enable_depth_test = true,
                .enable_depth_write = true,
            },
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::FRONT_BIT
            },
            .push_constant_size = sizeof(GBufferGatherPush),
            .name = "g buffer gather pipeline"
        }).value();
        
        composition_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/volumetric_lighting/composition.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/volumetric_lighting/composition.glsl" }, },
                .compile_options = {
                    .defines = { { .name = "USE_PCF", .value = "0" } }
                } 
            },
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::NONE
            },
            .push_constant_size = sizeof(CompositionPush),
            .name = "composition pipeline"
        }).value();

        shadow_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/volumetric_lighting/shadow.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/volumetric_lighting/shadow.glsl" }, },
            },
            .depth_test = {
                .depth_attachment_format = daxa::Format::D32_SFLOAT,
                .enable_depth_test = true,
                .enable_depth_write = true,
            },
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::BACK_BIT,
                .depth_bias_enable = true,
                .depth_bias_constant_factor = 1.25f, 
                .depth_bias_clamp = 0.0f,
                .depth_bias_slope_factor = 1.75f
            },
            .push_constant_size = sizeof(ShadowPush),
            .name = "shadow pipeline"
        }).value();

        depth_image = device.create_image({
            .format = daxa::Format::D32_SFLOAT,
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
            .name = "depth buffer"
        });

        task_depth_image = daxa::TaskImage { daxa::TaskImageInfo {
            .initial_images = {.images = std::span{&depth_image, 1}},
            .swapchain_image = false,
            .name = "task depth image"
        }};

        albedo_image = device.create_image({
            .format = swapchain.get_format(),
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
        });

        task_albedo_image = daxa::TaskImage { daxa::TaskImageInfo {
            .initial_images = {.images = std::span{&albedo_image, 1}},
            .swapchain_image = false,
            .name = "task albedo image"
        }};

        normal_image = device.create_image({
            .format = daxa::Format::R16G16B16A16_SFLOAT,
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
        });

        task_normal_image = daxa::TaskImage { daxa::TaskImageInfo {
            .initial_images = {.images = std::span{&normal_image, 1}},
            .swapchain_image = false,
            .name = "task normal image"
        }};

        sampler_id = device.create_sampler({
            .magnification_filter = daxa::Filter::LINEAR,
            .minification_filter = daxa::Filter::LINEAR,
            .mipmap_filter = daxa::Filter::LINEAR,
            .address_mode_u = daxa::SamplerAddressMode::CLAMP_TO_EDGE,
            .address_mode_v = daxa::SamplerAddressMode::CLAMP_TO_EDGE,
            .address_mode_w = daxa::SamplerAddressMode::CLAMP_TO_EDGE,
            .mip_lod_bias = 0.0f,
            .enable_anisotropy = true,
            .max_anisotropy = 16.0f,
            .enable_compare = false,
            .compare_op = daxa::CompareOp::ALWAYS,
            .min_lod = 0.0f,
            .max_lod = 1.0f,
            .enable_unnormalized_coordinates = false,
        });

        shadow_image = device.create_image({
            .format = daxa::Format::D32_SFLOAT,
            .size = {1024, 1024, 1},
            .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
            .name = "shadow image"
        });

        task_shadow_image = daxa::TaskImage { daxa::TaskImageInfo {
            .initial_images = {.images = std::span{&shadow_image, 1}},
            .swapchain_image = false,
            .name = "task shadow image"
        }};

        shadow_sampler = device.create_sampler(daxa::SamplerInfo {
            .magnification_filter = daxa::Filter::LINEAR,
            .minification_filter = daxa::Filter::LINEAR,
            .mipmap_filter = daxa::Filter::LINEAR,
            .address_mode_u = daxa::SamplerAddressMode::CLAMP_TO_BORDER,
            .address_mode_v = daxa::SamplerAddressMode::CLAMP_TO_BORDER,
            .address_mode_w = daxa::SamplerAddressMode::CLAMP_TO_BORDER,
            .mip_lod_bias = 0.0f,
            .enable_anisotropy = true,
            .max_anisotropy = 16.0f,
            .enable_compare = true,
            .compare_op = daxa::CompareOp::LESS,
            .min_lod = 0.0f,
            .max_lod = 1.0f,
            .border_color = daxa::BorderColor::FLOAT_OPAQUE_WHITE,
            .enable_unnormalized_coordinates = false,
        });

        light_buffer = device.create_buffer(daxa::BufferInfo {
            .size = sizeof(LightInfo),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "light buffer"
        });

        matrices_buffer = device.create_buffer(daxa::BufferInfo {
            .size = sizeof(MatricesBuffer),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "matrices buffer"
        });

        camera.camera.resize(size_x, size_y);

        model = std::make_unique<Model>(device, "assets/Sponza/glTF/Sponza.gltf");

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

        render_task_graph.use_persistent_image(task_swapchain_image);
        render_task_graph.use_persistent_image(task_depth_image);
        render_task_graph.use_persistent_image(task_albedo_image);
        render_task_graph.use_persistent_image(task_normal_image);
        render_task_graph.use_persistent_image(task_shadow_image);

        render_task_graph.add_task(RenderShadowTask {
            .uses = {
                .shadow_target = task_shadow_image
            },
            .pipeline = &shadow_pipeline,
            .model = model.get(),
            .light_matrix = &light_matrix,
            .matrices_buffer = matrices_buffer
        });

        render_task_graph.add_task(GBufferGatherTask {
            .uses = {
                .albedo_target = task_albedo_image,
                .normal_target = task_normal_image,
                .depth_target = task_depth_image
            },
            .pipeline = &g_buffer_gather_pipeline,
            .model = model.get(),
            .camera = &camera,
            .matrices_buffer = matrices_buffer
        });

        render_task_graph.add_task(CompositionTask {
            .uses = {
                .render_target = task_swapchain_image,
                .albedo_target = task_albedo_image,
                .normal_target = task_normal_image,
                .depth_target = task_depth_image,
                .shadow_image = task_shadow_image
            },
            .pipeline = &composition_pipeline,
            .camera = &camera,
            .light_buffer = light_buffer,
            .matrices_buffer = matrices_buffer,
            .imgui_renderer = imgui_renderer,
            .sampler_id = sampler_id,
            .bias = &bias,
            .pcf_range = &pcf_range,
            .shadow_intensity = &shadow_intensity,
        });

        render_task_graph.submit({});
        render_task_graph.present({});
        render_task_graph.complete({});
    }

    ~VolumetricLightingApp() {
        device.destroy_image(depth_image);
        device.destroy_image(albedo_image);
        device.destroy_image(normal_image);
        device.destroy_sampler(sampler_id);
        device.destroy_image(shadow_image);
        device.destroy_sampler(shadow_sampler);
        device.destroy_buffer(light_buffer);
        device.destroy_buffer(matrices_buffer);
    }

    void render() {
        auto swapchain_image = swapchain.acquire_next_image();
        task_swapchain_image.set_images({.images = std::span{&swapchain_image, 1}});
        if(swapchain_image.is_empty()) { return; }

        glm::vec3 light_position(-4.0f, 40.0f, -4.0f);
        glm::mat4 light_projection = glm::ortho(-64.0f, 64.0f, -64.0f, 64.0f, -64.0f, 64.0f);

        glm::vec3 dir = { 0.0f, -1.0f, 0.0f };
        dir = glm::rotateX(dir, glm::radians(direction.x));
        dir = glm::rotateY(dir, glm::radians(direction.y));
        dir = glm::rotateZ(dir, glm::radians(direction.z));

        glm::mat4 light_view = glm::lookAt(light_position, light_position + dir, glm::vec3(0.0, -1.0, 0.0));

        glm::mat4 model_mat = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 0.0f}) * glm::scale(glm::mat4{1.0f}, glm::vec3{0.01f, 0.01f, 0.01f});

        light_matrix = light_projection * light_view;

        {
            auto* ptr = device.get_host_address_as<LightInfo>(light_buffer);
            ptr->light_matrix = *reinterpret_cast<f32mat4x4*>(&light_matrix);
            ptr->shadow_image = shadow_image.default_view();
            ptr->shadow_sampler = shadow_sampler;
            ptr->light_direction = *reinterpret_cast<f32vec3*>(&dir);
        }

        {
            glm::mat4 m = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 0.0f}) * glm::scale(glm::mat4{1.0f}, glm::vec3{0.01f, 0.01f, 0.01f});
            glm::mat4 normal_matrix = glm::transpose(glm::inverse(m));
            glm::mat4 vp = camera.camera.get_vp();
            glm::mat4 mvp = vp * m;

            glm::mat4 temp_inverse_projection_mat = glm::inverse(camera.camera.proj_mat);
            glm::mat4 temp_inverse_view_mat = glm::inverse(camera.camera.view_mat);

            auto* ptr = device.get_host_address_as<MatricesBuffer>(matrices_buffer);
            ptr->mvp = *reinterpret_cast<f32mat4x4*>(&mvp);
            ptr->model_matrix = *reinterpret_cast<f32mat4x4*>(&m);
            ptr->normal_matrix = *reinterpret_cast<f32mat4x4*>(&normal_matrix);
            ptr->inverse_projection_matrix = *reinterpret_cast<f32mat4x4*>(&temp_inverse_projection_mat);
            ptr->inverse_view_matrix = *reinterpret_cast<f32mat4x4*>(&temp_inverse_view_mat);
        }

        render_task_graph.execute({});
    }

    void update() {
        while (!glfwWindowShouldClose(glfw_window_ptr)) {
            current_frame = glfwGetTime();
            delta_time = current_frame - last_frame;
            last_frame = current_frame;

            camera.update(delta_time);

            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGui::Begin("volumetric lighting settings");
            ImGui::DragFloat3("direction", &direction.x);
            ImGui::DragFloat("bias", &bias, 0.0001f, 0.0000001f, 0.1f);
            if(ImGui::Checkbox("use pcf", &use_pcf)) {
                daxa::ShaderDefine pcf_mode = { .name = "USE_PCF", .value = "0" };
                if(use_pcf) {
                    pcf_mode.value = "1";
                }

                composition_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
                    .vertex_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/volumetric_lighting/composition.glsl" }, },
                    },
                    .fragment_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/volumetric_lighting/composition.glsl" }, },
                        .compile_options = {
                            .defines = { pcf_mode }
                        } 
                    },
                    .raster = {
                        .face_culling = daxa::FaceCullFlagBits::NONE
                    },
                    .push_constant_size = sizeof(CompositionPush),
                    .name = "composition pipeline"
                }).value();
            }
            ImGui::DragInt("pcf range", &pcf_range, 1.0f, 1.0f, 6.0f);
            ImGui::DragFloat("shadow intensity", &shadow_intensity, 0.05f, 0.0001f, 1.0f);
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

            device.destroy_image(albedo_image);
            albedo_image = device.create_image({
                .format = swapchain.get_format(),
                .size = { size_x, size_y, 1 },
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
            });
            task_albedo_image.set_images({.images = std::span{&albedo_image, 1}});

            device.destroy_image(normal_image);
            normal_image = device.create_image({
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .size = { size_x, size_y, 1 },
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
            });
            task_normal_image.set_images({.images = std::span{&normal_image, 1}});

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

    void on_key(i32 key, i32 action) override {
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
    VolumetricLightingApp app;
    app.update();
    return 0;
}