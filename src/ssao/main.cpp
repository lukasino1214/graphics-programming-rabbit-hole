#include "../app.hpp"
#include "../camera.hpp"

#include <daxa/utils/pipeline_manager.hpp>
#include <glm/glm.hpp>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

#include <daxa/utils/imgui.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>

#include "shared.inl"

#include "../model.hpp"

struct GBufferGatherTask {
    struct Uses {
        daxa::ImageColorAttachment<> albedo_target = {};
        daxa::ImageColorAttachment<> normal_target = {};
        daxa::ImageDepthAttachment<> depth_target = {};
    } uses = {};

    std::string_view name = "g buffer gather";
    RasterPipelineHolder* pipeline = {};
    Model* model = {};
    daxa::BufferId camera_buffer = {};
    daxa::BufferId object_buffer = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        daxa::Device device = ti.get_device();

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
                .camera_info = device.get_device_address(camera_buffer),
                .object_info = device.get_device_address(object_buffer),
                .vertices = device.get_device_address(model->vertex_buffer),
                .materials = device.get_device_address(model->material_buffer),
                .material_index = primitive.material_index
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

struct SSAOGenerationTask {
    struct Uses {
        daxa::ImageColorAttachment<> ssao_target = {};
        daxa::ImageShaderRead<> normal_target = {};
        daxa::ImageShaderRead<> depth_target = {};
    } uses = {};

    std::string_view name = "ssao generatation";
    RasterPipelineHolder* pipeline = {};
    daxa::BufferId camera_buffer = {};
    daxa::SamplerId sampler_id = {};
    f32* scale = {};
    f32* bias = {};
    f32* radius = {};
    i32* kernel_size = {};
    Texture* noise_texture = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        daxa::Device device = ti.get_device();

        u32 size_x = ti.get_device().info_image(uses.normal_target.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.normal_target.image()).size.y;

        cmd_list.begin_renderpass({
            .color_attachments = {{
                    .image_view = uses.ssao_target.view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<f32, 4>{1.0f, 0.0f, 0.0f, 1.0f},
            }},
            .render_area = {.x = 0, .y = 0, .width = static_cast<u32>(static_cast<f32>(size_x) * *scale), .height = static_cast<u32>(static_cast<f32>(size_y) * *scale)},
        });
        cmd_list.set_pipeline(*pipeline->pipeline);
        cmd_list.push_constant(SSAOGenerationPush {
            .normal = uses.normal_target.view(),
            .depth = uses.depth_target.view(),
            .sampler_id = sampler_id,
            .camera_info = device.get_device_address(camera_buffer),
            .bias = *bias,
            .radius = *radius,
            .kernel_size = *kernel_size,
            .noise_texture = noise_texture->get_texture_id()
        });
        cmd_list.draw({ .vertex_count = 3 });
        cmd_list.end_renderpass();
    }
};

struct SSAOBlurTask {
    struct Uses {
        daxa::ImageColorAttachment<> ssao_blur_target = {};
        daxa::ImageShaderRead<> ssao_target = {};
    } uses = {};

    std::string_view name = "ssao blur";
    RasterPipelineHolder* pipeline = {};
    daxa::SamplerId sampler_id = {};
    bool* apply_blur = {};
    f32* scale = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();

        u32 size_x = ti.get_device().info_image(uses.ssao_target.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.ssao_target.image()).size.y;

        if(*apply_blur) {
            cmd_list.begin_renderpass({
                .color_attachments = {
                    {
                        .image_view = uses.ssao_blur_target.view(),
                        .load_op = daxa::AttachmentLoadOp::CLEAR,
                        .clear_value = std::array<f32, 4>{0.0f, 0.0f, 0.0f, 1.0f},
                    },
                },
                .render_area = {.x = 0, .y = 0, .width = static_cast<u32>(size_x), .height = static_cast<u32>(size_y)},
            });
            cmd_list.set_pipeline(*pipeline->pipeline);
            cmd_list.push_constant(SSAOBlurPush {
                .ssao = uses.ssao_target.view(),
                .sampler_id = sampler_id,
            });
            cmd_list.draw({ .vertex_count = 3 });
            cmd_list.end_renderpass();
        }
    }
};

struct CompositionTask {
    struct Uses {
        daxa::ImageColorAttachment<> render_target = {};
        daxa::ImageShaderRead<> albedo_target = {};
        daxa::ImageShaderRead<> normal_target = {};
        daxa::ImageShaderRead<> depth_target = {};
        daxa::ImageShaderRead<> ssao_target = {};
        daxa::ImageShaderRead<> ssao_blur_target = {};
    } uses = {};

    std::string_view name = "composition";
    RasterPipelineHolder* pipeline = {};
    daxa::SamplerId sampler_id = {};
    daxa::BufferId camera_buffer = {};
    daxa::ImGuiRenderer imgui_renderer = {};

    bool* apply_blur = {};
    f32* ssao_strength = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        daxa::Device device= ti.get_device();

        u32 size_x = ti.get_device().info_image(uses.normal_target.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.normal_target.image()).size.y;

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = uses.render_target.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = std::array<f32, 4>{0.0f, 0.0f, 0.0f, 0.0f},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*pipeline->pipeline);
        cmd_list.push_constant(CompositionPush {
            .albedo_image = uses.albedo_target.view(),
            .normal_image = uses.normal_target.view(),
            .depth_image = uses.depth_target.view(),
            .ssao_image = *apply_blur ? uses.ssao_blur_target.view() : uses.ssao_target.view(),
            .sampler_id = sampler_id,
            .camera_info = device.get_device_address(camera_buffer),
            .ssao_strength = *ssao_strength
        });
        cmd_list.draw({ .vertex_count = 3 });
        cmd_list.end_renderpass();

        imgui_renderer.record_commands(ImGui::GetDrawData(), cmd_list, uses.render_target.image(), size_x, size_y);
    }
};

struct SSAOApp : public App {
    std::unique_ptr<Model> model = {};
    RasterPipelineHolder g_buffer_gather_pipeline = {};
    RasterPipelineHolder composition_pipeline = {};
    RasterPipelineHolder ssao_generation_pipeline = {};
    RasterPipelineHolder ssao_blur_pipeline = {};

    daxa::ImageId depth_image = {};
    daxa::TaskImage task_depth_image = {};
    daxa::ImageId albedo_image = {};
    daxa::TaskImage task_albedo_image = {};
    daxa::ImageId normal_image = {};
    daxa::TaskImage task_normal_image = {};
    daxa::ImageId ssao_image = {};
    daxa::TaskImage task_ssao_image = {};
    daxa::ImageId ssao_blur_image = {};
    daxa::TaskImage task_ssao_blur_image = {};
    daxa::SamplerId sampler_id = {};

    ControlledCamera3D camera;
    daxa::BufferId camera_buffer = {};
    daxa::BufferId object_buffer = {};

    std::unique_ptr<Texture> noise_texture = {};

    f64 current_frame = glfwGetTime();
    f64 last_frame = current_frame;
    f64 delta_time;
    bool paused = false;

    f32 scale = 0.5f;
    bool apply_blur = true;
    f32 bias = 0.025f;
    f32 radius = 0.3f;
    i32 kernel_size = 26;
    bool use_biliteral_blur = false;
    bool use_blue_noise = false;
    f32 ssao_strength = 2.0f;
    bool debug_ssao = false;

    daxa::ImGuiRenderer imgui_renderer;

    daxa::TaskImage task_swapchain_image = {};
    daxa::TaskGraph render_task_graph = {};

    SSAOApp() : App("SSAO Example") {
        g_buffer_gather_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/ssao/g_buffer_gather.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/ssao/g_buffer_gather.glsl" }, },
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
            .name = "g_buffer_gather_pipeline"
        }).value();

        composition_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/ssao/composition.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/ssao/composition.glsl" }, },
                .compile_options = {
                    .defines = { { .name = "DEBUG_SSAO", .value = "0" } }
                } 
            },
            .color_attachments = {{ .format = swapchain.get_format() }},
            .raster = {
                .face_culling = daxa::FaceCullFlagBits::NONE
            },
            .push_constant_size = sizeof(CompositionPush),
            .name = "composition_pipeline"
        }).value();

        ssao_generation_pipeline.pipeline = pipeline_manager.add_raster_pipeline({
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/ssao/ssao_generation.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/ssao/ssao_generation.glsl" }, },
                .compile_options = {
                    .defines = { { .name = "USE_NOISE_TEXTURE", .value = "0" } }
                } 
            },
            .color_attachments = { { .format = daxa::Format::R8_UNORM, } },
            .raster = { .face_culling = daxa::FaceCullFlagBits::NONE },
            .push_constant_size = sizeof(SSAOGenerationPush),
            .name = "ssao_generation_pipeline"
        }).value();

        ssao_blur_pipeline.pipeline = pipeline_manager.add_raster_pipeline({
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/ssao/ssao_blur.glsl" }, },
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/ssao/ssao_blur.glsl" }, },
                .compile_options = {
                    .defines = { { .name = "BLUR_MODE", .value = "0" } }
                } 
            },
            .color_attachments = { { .format = daxa::Format::R8_UNORM, } },
            .raster = { .face_culling = daxa::FaceCullFlagBits::NONE },
            .push_constant_size = sizeof(SSAOBlurPush),
            .name = "ssao_blur_pipeline"
        }).value();

        depth_image = device.create_image({
            .format = daxa::Format::D32_SFLOAT,
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
            .name = "depth_image"
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
            .name = "albedo_image"
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
            .name = "normal_image"
        });

        task_normal_image = daxa::TaskImage { daxa::TaskImageInfo {
            .initial_images = {.images = std::span{&normal_image, 1}},
            .swapchain_image = false,
            .name = "task normal image"
        }};

        ssao_image = device.create_image({
            .format = daxa::Format::R8_UNORM,
            .size = { static_cast<u32>(static_cast<f32>(size_x) * scale), static_cast<u32>(static_cast<f32>(size_y) * scale), 1 },
            .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
            .name = "ssao_image"
        });

        task_ssao_image = daxa::TaskImage { daxa::TaskImageInfo {
            .initial_images = {.images = std::span{&ssao_image, 1}},
            .swapchain_image = false,
            .name = "task ssao image"
        }};

        ssao_blur_image = device.create_image({
            .format = daxa::Format::R8_UNORM,
            .size = { static_cast<u32>(static_cast<f32>(size_x) * scale), static_cast<u32>(static_cast<f32>(size_y) * scale), 1 },
            .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
            .name = "ssao_blur_image"
        });

        task_ssao_blur_image = daxa::TaskImage { daxa::TaskImageInfo {
            .initial_images = {.images = std::span{&ssao_blur_image, 1}},
            .swapchain_image = false,
            .name = "task ssao blur image"
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

        camera_buffer = device.create_buffer({
            .size = sizeof(CameraInfo),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "camera info buffer"
        });

        object_buffer = device.create_buffer({
            .size = sizeof(ObjectInfo),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "object info buffer"
        });

        camera.camera.resize(size_x, size_y);


        model = std::make_unique<Model>(device, "assets/Sponza/glTF/Sponza.gltf");
        noise_texture = std::make_unique<Texture>(device, "src/ssao/blue_noise.png", Texture::Type::UNORM);
    
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
        render_task_graph.use_persistent_image(task_albedo_image);
        render_task_graph.use_persistent_image(task_normal_image);
        render_task_graph.use_persistent_image(task_depth_image);
        render_task_graph.use_persistent_image(task_ssao_image);
        render_task_graph.use_persistent_image(task_ssao_blur_image);

        render_task_graph.add_task(GBufferGatherTask {
            .uses = {
                .albedo_target = task_albedo_image,
                .normal_target = task_normal_image,
                .depth_target = task_depth_image
            },
            .pipeline = &g_buffer_gather_pipeline,
            .model = model.get(),
            .camera_buffer = camera_buffer,
            .object_buffer = object_buffer
        });

        render_task_graph.add_task(SSAOGenerationTask {
            .uses = {
                .ssao_target = task_ssao_image,
                .normal_target = task_normal_image,
                .depth_target = task_depth_image
            },
            .pipeline = &ssao_generation_pipeline,
            .camera_buffer = camera_buffer,
            .sampler_id = sampler_id,
            .scale = &scale,
            .bias= &bias,
            .radius = &radius,
            .kernel_size = &kernel_size,
            .noise_texture = noise_texture.get()
        });

        render_task_graph.add_task(SSAOBlurTask {
            .uses = {
                .ssao_blur_target = task_ssao_blur_image,
                .ssao_target = task_ssao_image
            },
            .pipeline = &ssao_blur_pipeline,
            .sampler_id = sampler_id,
            .apply_blur = &apply_blur,
            .scale = &scale
        });

        render_task_graph.add_task(CompositionTask {
            .uses = {
                .render_target = task_swapchain_image,
                .albedo_target = task_albedo_image,
                .normal_target = task_normal_image,
                .depth_target = task_depth_image,
                .ssao_target = task_ssao_image,
                .ssao_blur_target = task_ssao_blur_image,
            },
            .pipeline = &composition_pipeline,
            .sampler_id = sampler_id,
            .camera_buffer = camera_buffer,
            .imgui_renderer = imgui_renderer,
            .apply_blur = &apply_blur,
            .ssao_strength = &ssao_strength
        });

        render_task_graph.submit({});
        render_task_graph.present({});
        render_task_graph.complete({});
    }

    ~SSAOApp() {
        device.destroy_image(depth_image);
        device.destroy_image(albedo_image);
        device.destroy_image(normal_image);
        device.destroy_image(ssao_image);
        device.destroy_image(ssao_blur_image);
        device.destroy_sampler(sampler_id);
        device.destroy_buffer(camera_buffer);
        device.destroy_buffer(object_buffer);
    }

    void render() {
        auto swapchain_image = swapchain.acquire_next_image();
        task_swapchain_image.set_images({.images = std::span{&swapchain_image, 1}});
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
        camera_ptr->position = *reinterpret_cast<f32vec3*>(&camera.position);

        glm::mat4 model_matrix = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 0.0f}) * glm::scale(glm::mat4{1.0f}, glm::vec3{0.01f, 0.01f, 0.01f});
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

            camera.update(delta_time);

            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGui::Begin("ssao settings");

            ImGui::DragFloat("bias", &bias, 0.01f, 0.01f, 1.0f);
            ImGui::DragFloat("radius", &radius, 0.01f, 0.01f, 1.0f);
            ImGui::DragInt("kernel size", &kernel_size, 1.0f, 1.0f, 26.0f);
            ImGui::DragFloat("ssao strength", &ssao_strength, 0.01f, 0.01f, 16.0f);
            ImGui::Checkbox("Apply blur", &apply_blur);
            if(ImGui::Checkbox("Use bilateral blur", &use_biliteral_blur)) {
                daxa::ShaderDefine blur_mode = { .name = "BLUR_MODE", .value = "0" };
                if(use_biliteral_blur) {
                    blur_mode.value = "1";
                }

                ssao_blur_pipeline.pipeline = pipeline_manager.add_raster_pipeline({
                    .vertex_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/ssao/ssao_blur.glsl" }, },
                    },
                    .fragment_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/ssao/ssao_blur.glsl" }, },
                        .compile_options = {
                            .defines = { blur_mode }
                        } 
                    },
                    .color_attachments = { { .format = daxa::Format::R8_UNORM, } },
                    .raster = { .face_culling = daxa::FaceCullFlagBits::NONE },
                    .push_constant_size = sizeof(SSAOBlurPush),
                    .name = "ssao_blur_pipeline"
                }).value();
            }

            if(ImGui::Checkbox("Debug SSAO", &debug_ssao)) {
                daxa::ShaderDefine debug_mode = { .name = "DEBUG_SSAO", .value = "0" };
                if(debug_ssao) {
                    debug_mode.value = "1";
                }

                composition_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
                    .vertex_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/ssao/composition.glsl" }, },
                    },
                    .fragment_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/ssao/composition.glsl" }, },
                        .compile_options = {
                            .defines = { debug_mode }
                        } 
                    },
                    .color_attachments = {{ .format = swapchain.get_format() }},
                    .raster = {
                        .face_culling = daxa::FaceCullFlagBits::NONE
                    },
                    .push_constant_size = sizeof(CompositionPush),
                    .name = "composition_pipeline"
                }).value();
            }

            if(ImGui::Checkbox("Use noise texture", &use_blue_noise)) {
                daxa::ShaderDefine noise_mode = { .name = "USE_NOISE_TEXTURE", .value = "0" };
                if(debug_ssao) {
                    noise_mode.value = "1";
                }

                ssao_generation_pipeline.pipeline = pipeline_manager.add_raster_pipeline({
                    .vertex_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/ssao/ssao_generation.glsl" }, },
                    },
                    .fragment_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/ssao/ssao_generation.glsl" }, },
                        .compile_options = {
                            .defines = { noise_mode }
                        } 
                    },
                    .color_attachments = { { .format = daxa::Format::R8_UNORM, } },
                    .raster = { .face_culling = daxa::FaceCullFlagBits::NONE },
                    .push_constant_size = sizeof(SSAOGenerationPush),
                    .name = "ssao_generation_pipeline"
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
                .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                .name = "depth_image"
            });
            task_depth_image.set_images({.images = std::span{&depth_image, 1}});

            device.destroy_image(albedo_image);
            albedo_image = device.create_image({
                .format = swapchain.get_format(),
                .size = { size_x, size_y, 1 },
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                .name = "albedo_image"
            });
            task_albedo_image.set_images({.images = std::span{&albedo_image, 1}});

            device.destroy_image(normal_image);
            normal_image = device.create_image({
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .size = { size_x, size_y, 1 },
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                .name = "normal_image"
            });
            task_normal_image.set_images({.images = std::span{&normal_image, 1}});
            
            device.destroy_image(ssao_image);
            ssao_image = device.create_image({
                .format = daxa::Format::R8_UNORM,
                .size = { static_cast<u32>(static_cast<f32>(size_x) * scale), static_cast<u32>(static_cast<f32>(size_y) * scale), 1 },
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                .name = "ssao_image"
            });
            task_ssao_image.set_images({.images = std::span{&ssao_image, 1}});

            device.destroy_image(ssao_blur_image);
            ssao_blur_image = device.create_image({
                .format = daxa::Format::R8_UNORM,
                .size = { static_cast<u32>(static_cast<f32>(size_x) * scale), static_cast<u32>(static_cast<f32>(size_y) * scale), 1 },
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                .name = "ssao_blur_image"
            });
            task_ssao_blur_image.set_images({.images = std::span{&ssao_blur_image, 1}});

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
    SSAOApp app;
    app.update();
    return 0;
}