#include "../app.hpp"
#include "../camera.hpp"

#include <glm/glm.hpp>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

#include "shared.inl"

#include "../model.hpp"

#include <random>

#include <daxa/utils/imgui.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>

struct GeneratePointLightsTask {
    struct Uses {
        daxa::BufferTransferWrite point_light_buffer = {};
    } uses = {};

    std::string_view name = "generate point light";

    void callback(daxa::TaskInterface ti) {
        const glm::vec3 LIGHT_MIN_BOUNDS = glm::vec3(-120.0f, -20.0f, -120.0f);
        const glm::vec3 LIGHT_MAX_BOUNDS = glm::vec3(120.0f, 80.0f, 120.0f);

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0, 1);

        auto random_position = [&](std::uniform_real_distribution<> dis, std::mt19937 gen) -> f32vec3 {
            f32vec3 position;
            for (i32 i = 0; i < 3; i++) {
                f32 min = LIGHT_MIN_BOUNDS[i];
                f32 max = LIGHT_MAX_BOUNDS[i];
                position[i] = (GLfloat)dis(gen) * (max - min) + min;
            }

            return position;
        };

        std::vector<PointLight> point_lights = {};
        point_lights.resize(NUM_LIGHTS);

        for (i32 i = 0; i < NUM_LIGHTS; i++) {
            PointLight &light = point_lights[i];        
            light.position = random_position(dis, gen);
            light.color = f32vec3{static_cast<f32>(dis(gen)), static_cast<f32>(dis(gen)), static_cast<f32>(dis(gen))};
            light.radius = 8.0f;
            // light.position = { 0.0f,0.0f, 0.0f };
            // light.color = { 1.0f, 0.0f, 0.0f };
            // light.radius = 30.0f;
        }

        daxa::CommandList cmd_list = ti.get_command_list();
        daxa::Device device = ti.get_device();

        daxa::BufferId staging_point_light_buffer = device.create_buffer(daxa::BufferInfo {
            .size = sizeof(PointLight) * NUM_LIGHTS,
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "staging point light buffer"
        });

        cmd_list.destroy_buffer_deferred(staging_point_light_buffer);

        auto* ptr = device.get_host_address_as<PointLight>(staging_point_light_buffer);
        std::memcpy(ptr, point_lights.data(), NUM_LIGHTS * sizeof(PointLight));

        cmd_list.copy_buffer_to_buffer(daxa::BufferCopyInfo {
            .src_buffer = staging_point_light_buffer,
            .src_offset = 0,
            .dst_buffer = uses.point_light_buffer.buffer(),
            .dst_offset = 0,
            .size = sizeof(PointLight) * NUM_LIGHTS,
        });
    }
};

struct UpdateBuffersTask {
    struct Uses {
        daxa::BufferTransferWrite camera_buffer = {};
        daxa::BufferTransferWrite object_buffer = {};
    } uses = {};

    std::string_view name = "update buffers";
    ControlledCamera3D* camera = {};

    void callback(daxa::TaskInterface ti) {
        daxa::Device device = ti.get_device();

        glm::mat4 projection = camera->camera.proj_mat;
        glm::mat4 view = camera->camera.get_view();

        glm::mat4 temp_inverse_projection_mat = glm::inverse(projection);
        glm::mat4 temp_inverse_view_mat = glm::inverse(view);

        auto* camera_ptr = device.get_host_address_as<CameraInfo>(uses.camera_buffer.buffer());
        camera_ptr->projection_matrix = *reinterpret_cast<f32mat4x4*>(&projection);
        camera_ptr->inverse_projection_matrix = *reinterpret_cast<f32mat4x4*>(&temp_inverse_projection_mat);
        camera_ptr->view_matrix = *reinterpret_cast<f32mat4x4*>(&view);
        camera_ptr->inverse_view_matrix = *reinterpret_cast<f32mat4x4*>(&temp_inverse_view_mat);
        camera_ptr->position = *reinterpret_cast<f32vec3*>(&camera->position);

        glm::mat4 model_matrix = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 0.0f}) * glm::scale(glm::mat4{1.0f}, glm::vec3{0.1f, 0.1f, 0.1f});
        glm::mat4 normal_matrix = glm::transpose(glm::inverse(model_matrix));

        auto* object_ptr = device.get_host_address_as<ObjectInfo>(uses.object_buffer.buffer());
        object_ptr->model_matrix = *reinterpret_cast<f32mat4x4*>(&model_matrix);
        object_ptr->normal_matrix = *reinterpret_cast<f32mat4x4*>(&normal_matrix);
    }
};

struct ComputeFrustumsTask {
    struct Uses {
        daxa::BufferComputeShaderWrite frustums_buffer = {};
        daxa::BufferComputeShaderRead camera_buffer = {};
    } uses = {};

    std::string_view name = "compute frustum";
    ComputePipelineHolder* pipeline = {};

    u32* size_x = {};
    u32* size_y = {};
    u32* work_groups_x = {};
    u32* work_groups_y = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        daxa::Device device = ti.get_device();

        cmd_list.set_pipeline(*pipeline->pipeline);
        cmd_list.push_constant(ComputeFrustumsPush {
            .camera_info = device.get_device_address(uses.camera_buffer.buffer()),
            .frustum_buffer = device.get_device_address(uses.frustums_buffer.buffer()),
            .viewport_size = { static_cast<i32>(*size_x), static_cast<i32>(*size_y) },
            .tile_nums = { static_cast<i32>(*work_groups_x), static_cast<i32>(*work_groups_y) }
        });
        cmd_list.dispatch(std::ceil(static_cast<f32>(*work_groups_x) / static_cast<f32>(TILE_SIZE)), std::ceil(static_cast<f32>(*work_groups_y) / static_cast<f32>(TILE_SIZE)));
    }
};

struct ComputeLightListTask {
    struct Uses {
        daxa::ImageShaderRead<> depth_image = {};
        daxa::BufferComputeShaderRead frustums_buffer = {};
        daxa::BufferComputeShaderRead camera_buffer = {};
        daxa::BufferComputeShaderRead point_light_buffer = {};
        daxa::BufferComputeShaderWrite point_light_index_buffer = {};
        daxa::BufferComputeShaderWrite point_light_grid_buffer = {};
    } uses = {};

    std::string_view name = "compute light list";
    ComputePipelineHolder* pipeline = {};

    u32* size_x = {};
    u32* size_y = {};
    u32* work_groups_x = {};
    u32* work_groups_y = {};
    daxa::SamplerId depth_sampler = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        daxa::Device device = ti.get_device();

        cmd_list.set_pipeline(*pipeline->pipeline);
        cmd_list.push_constant(ComputeLightListPush {
            .depth_image = uses.depth_image.view(),
            .depth_sampler = depth_sampler,
            .camera_info = device.get_device_address(uses.camera_buffer.buffer()),
            .frustum_buffer = device.get_device_address(uses.frustums_buffer.buffer()),
            .point_light_buffer = device.get_device_address(uses.point_light_buffer.buffer()),
            .point_light_index_buffer = device.get_device_address(uses.point_light_index_buffer.buffer()),
            .point_light_grid_buffer = device.get_device_address(uses.point_light_grid_buffer.buffer()),
            .viewport_size = { static_cast<i32>(*size_x), static_cast<i32>(*size_y) },
            .tile_nums = { static_cast<i32>(*work_groups_x), static_cast<i32>(*work_groups_y) }
        });
        cmd_list.dispatch(*work_groups_x, *work_groups_y);
    }
};

struct DepthPrepassTask {
    struct Uses {
        daxa::ImageDepthAttachment<> depth_target = {};
        daxa::BufferVertexShaderRead camera_buffer = {};
        daxa::BufferVertexShaderRead object_buffer = {};
    } uses = {};

    std::string_view name = "depth prepass";
    RasterPipelineHolder* pipeline = {};
    Model* model = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        daxa::Device device = ti.get_device();

        u32 size_x = ti.get_device().info_image(uses.depth_target.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.depth_target.image()).size.y;

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .depth_attachment = {{
                .image_view = uses.depth_target.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = daxa::DepthValue{1.0f, 0},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*pipeline->pipeline);

        for(auto& primitive : model->primitives) {
            cmd_list.push_constant(DepthPrepassPush {
                .camera_info = device.get_device_address(uses.camera_buffer.buffer()),
                .object_info = device.get_device_address(uses.object_buffer.buffer()),
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
    }
};

struct RenderTask {
    struct Uses {
        daxa::ImageColorAttachment<> render_target = {};
        daxa::ImageDepthAttachmentRead<> depth_target = {};
        daxa::BufferVertexShaderRead camera_buffer = {};
        daxa::BufferVertexShaderRead object_buffer = {};
        daxa::BufferFragmentShaderRead point_light_buffer = {};
        daxa::BufferFragmentShaderRead point_light_index_buffer = {};
        daxa::BufferFragmentShaderRead point_light_grid_buffer = {};
    } uses = {};

    std::string_view name = "render";
    RasterPipelineHolder* pipeline = {};
    Model* model = {};
    daxa::ImGuiRenderer imgui_renderer = {};

    void callback(daxa::TaskInterface ti) {
        daxa::CommandList cmd_list = ti.get_command_list();
        daxa::Device device = ti.get_device();

        u32 size_x = ti.get_device().info_image(uses.render_target.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.render_target.image()).size.y;

        cmd_list.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = uses.render_target.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = std::array<f32, 4>{0.2f, 0.4f, 1.0f, 1.0f},
            }},
            .depth_attachment = {{
                .image_view = uses.depth_target.view(),
                .load_op = daxa::AttachmentLoadOp::LOAD,
                .clear_value = daxa::DepthValue{1.0f, 0},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });
        cmd_list.set_pipeline(*pipeline->pipeline);

        for(auto& primitive : model->primitives) {
            cmd_list.push_constant(DrawPush {
                .camera_info = device.get_device_address(uses.camera_buffer.buffer()),
                .object_info = device.get_device_address(uses.object_buffer.buffer()),
                .vertices = device.get_device_address(model->vertex_buffer),
                .materials = device.get_device_address(model->material_buffer),
                .point_light_buffer = device.get_device_address(uses.point_light_buffer.buffer()),
                .point_light_index_buffer = device.get_device_address(uses.point_light_index_buffer.buffer()),
                .point_light_grid_buffer = device.get_device_address(uses.point_light_grid_buffer.buffer()),
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

        imgui_renderer.record_commands(ImGui::GetDrawData(), cmd_list, uses.render_target.image(), size_x, size_y);
    }
};

struct TiledForwardApp : public App {
    std::unique_ptr<Model> model = {};
    ComputePipelineHolder compute_frustum_pipeline = {};
    ComputePipelineHolder compute_light_list_pipeline = {};
    RasterPipelineHolder depth_prepass_pipeline = {};
    RasterPipelineHolder raster_pipeline = {};
    daxa::ImageId depth_image = {};
    daxa::TaskImage task_depth_image = {};

    daxa::BufferId camera_buffer = {};
    daxa::TaskBuffer task_camera_buffer = {};

    daxa::BufferId object_buffer = {};
    daxa::TaskBuffer task_object_buffer = {};

    daxa::BufferId frustums_buffer = {};
    daxa::TaskBuffer task_frustums_buffer = {};

    daxa::BufferId point_light_buffer = {};
    daxa::TaskBuffer task_point_light_buffer = {};

    daxa::BufferId point_light_index_buffer = {};
    daxa::TaskBuffer task_point_light_index_buffer = {};

    daxa::BufferId point_light_grid_buffer = {};
    daxa::TaskBuffer task_point_light_grid_buffer = {};

    daxa::TaskImage task_swapchain_image = {};
    daxa::TaskGraph render_task_graph = {};

    ControlledCamera3D camera;

    daxa::SamplerId depth_sampler = {};

    u32 work_groups_x = {};
    u32 work_groups_y = {};
    u32 number_of_tiles = {};

    f64 current_frame = glfwGetTime();
    f64 last_frame = current_frame;
    f64 delta_time;
    bool paused = false;

    daxa::ImGuiRenderer imgui_renderer;

    bool cull_lights = false;

    TiledForwardApp() : App("Tiled Forward Example") {
        compute_frustum_pipeline.pipeline = pipeline_manager.add_compute_pipeline(daxa::ComputePipelineCompileInfo {
            .shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/tiled_forward/compute_frustum_grid.glsl" }, },
            },
            .push_constant_size = sizeof(ComputeFrustumsPush),
            .name = "compute frustum pipeline"
        }).value();

        compute_light_list_pipeline.pipeline = pipeline_manager.add_compute_pipeline(daxa::ComputePipelineCompileInfo {
            .shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/tiled_forward/compute_light_list.glsl" }, },
            },
            .push_constant_size = sizeof(ComputeLightListPush),
            .name = "compute light list pipeline"
        }).value();

        depth_prepass_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
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

        raster_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
            .vertex_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/tiled_forward/raster.glsl" }, },
                .compile_options = {
                    .defines = { 
                        { .name = "CULL_LIGHTS", .value = "0" },
                    }
                } 
            },
            .fragment_shader_info = daxa::ShaderCompileInfo {
                .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/tiled_forward/raster.glsl" }, },
                .compile_options = {
                    .defines = { 
                        { .name = "CULL_LIGHTS", .value = "0" },
                    }
                } 
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

        ImGui::CreateContext();
        ImGui_ImplGlfw_InitForVulkan(glfw_window_ptr, true);
        imgui_renderer =  daxa::ImGuiRenderer({
            .device = device,
            .format = swapchain.get_format(),
        });

        depth_image = device.create_image({
            .format = daxa::Format::D32_SFLOAT,
            .size = { size_x, size_y, 1 },
            .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
        });

        task_depth_image = daxa::TaskImage { daxa::TaskImageInfo {
            .initial_images = {.images = std::span{&depth_image, 1}},
            .swapchain_image = false,
            .name = "task depth image"
        }};

        camera_buffer = device.create_buffer(daxa::BufferInfo {
            .size = sizeof(CameraInfo),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "camera buffer"
        });

        task_camera_buffer = daxa::TaskBuffer({
            .initial_buffers = {.buffers = std::span{&camera_buffer, 1}},
            .name = "task camera buffer",
        });

        object_buffer = device.create_buffer(daxa::BufferInfo {
            .size = sizeof(ObjectInfo),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "object buffer"
        });

        task_object_buffer = daxa::TaskBuffer({
            .initial_buffers = {.buffers = std::span{&object_buffer, 1}},
            .name = "task object buffer",
        });

        point_light_buffer = device.create_buffer(daxa::BufferInfo {
            .size = sizeof(PointLight) * NUM_LIGHTS,
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
            .name = "point light buffer"
        });

        task_point_light_buffer = daxa::TaskBuffer({
            .initial_buffers = {.buffers = std::span{&point_light_buffer, 1}},
            .name = "task point light buffer",
        });

        // work_groups_x = (size_x - 1) / TILE_SIZE + 1;
        // work_groups_y = (size_y - 1) / TILE_SIZE + 1;
        work_groups_x = std::ceil(static_cast<f32>(size_x) / static_cast<f32>(TILE_SIZE));
        work_groups_y = std::ceil(static_cast<f32>(size_y) / static_cast<f32>(TILE_SIZE));
        number_of_tiles = work_groups_x * work_groups_y;

        frustums_buffer = device.create_buffer(daxa::BufferInfo {
            .size = static_cast<u32>(sizeof(Frustum) * number_of_tiles),
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
            .name = "frustums buffer"
        });

        task_frustums_buffer = daxa::TaskBuffer({
            .initial_buffers = {.buffers = std::span{&frustums_buffer, 1}},
            .name = "task frustums buffer",
        });

        point_light_index_buffer = device.create_buffer(daxa::BufferInfo {
            .size = static_cast<u32>(sizeof(u32) * NUM_LIGHTS * number_of_tiles),
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
            .name = "point light index buffer"
        });

        task_point_light_index_buffer = daxa::TaskBuffer({
            .initial_buffers = {.buffers = std::span{&point_light_index_buffer, 1}},
            .name = "task point light index buffer",
        });

        point_light_grid_buffer = device.create_buffer(daxa::BufferInfo {
            .size = static_cast<u32>(sizeof(u32) * number_of_tiles),
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
            .name = "point light grid buffer"
        });

        task_point_light_grid_buffer = daxa::TaskBuffer({
            .initial_buffers = {.buffers = std::span{&point_light_grid_buffer, 1}},
            .name = "task point light grid buffer",
        });

        auto upload_task_graph = daxa::TaskGraph({
            .device = device,
            .name = "upload task graph",
        });

        upload_task_graph.use_persistent_buffer(task_point_light_buffer);

        upload_task_graph.add_task(GeneratePointLightsTask{
            .uses = {
                .point_light_buffer = task_point_light_buffer,
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

        render_task_graph.use_persistent_image(task_swapchain_image);
        render_task_graph.use_persistent_image(task_depth_image);
        render_task_graph.use_persistent_buffer(task_camera_buffer);
        render_task_graph.use_persistent_buffer(task_object_buffer);
        render_task_graph.use_persistent_buffer(task_frustums_buffer);
        render_task_graph.use_persistent_buffer(task_point_light_buffer);
        render_task_graph.use_persistent_buffer(task_point_light_index_buffer);
        render_task_graph.use_persistent_buffer(task_point_light_grid_buffer);

        model = std::make_unique<Model>(device, "assets/Sponza/glTF/Sponza.gltf");

        render_task_graph.add_task(UpdateBuffersTask {
            .uses = {
                .camera_buffer = task_camera_buffer,
                .object_buffer = task_object_buffer
            },
            .camera = &camera
        });

        render_task_graph.add_task(ComputeFrustumsTask {
            .uses = {
                .frustums_buffer = task_frustums_buffer,
                .camera_buffer = task_camera_buffer
            },
            .pipeline = &compute_frustum_pipeline,
            .size_x = &size_x,
            .size_y = &size_y,
            .work_groups_x = &work_groups_x,
            .work_groups_y = &work_groups_y
        });

        depth_sampler = device.create_sampler({
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

        render_task_graph.add_task(ComputeLightListTask {
            .uses = {
                .depth_image = task_depth_image,
                .frustums_buffer = task_frustums_buffer,
                .camera_buffer = task_camera_buffer,
                .point_light_buffer = task_point_light_buffer,
                .point_light_index_buffer = task_point_light_index_buffer,
                .point_light_grid_buffer = task_point_light_grid_buffer
            },
            .pipeline = &compute_light_list_pipeline,
            .size_x = &size_x,
            .size_y = &size_y,
            .work_groups_x = &work_groups_x,
            .work_groups_y = &work_groups_y,
            .depth_sampler = depth_sampler
        });

        render_task_graph.add_task(DepthPrepassTask {
            .uses = {
                .depth_target = task_depth_image,
                .camera_buffer = task_camera_buffer,
                .object_buffer = task_object_buffer
            },
            .pipeline = &depth_prepass_pipeline,
            .model = model.get(),
        });

        render_task_graph.add_task(RenderTask {
            .uses = {
                .render_target = task_swapchain_image,
                .depth_target = task_depth_image,
                .camera_buffer = task_camera_buffer,
                .object_buffer = task_object_buffer,
                .point_light_buffer = task_point_light_buffer,
                .point_light_index_buffer = task_point_light_index_buffer,
                .point_light_grid_buffer = task_point_light_grid_buffer
            },
            .pipeline = &raster_pipeline,
            .model = model.get(),
            .imgui_renderer = imgui_renderer
        });

        render_task_graph.submit({});
        render_task_graph.present({});
        render_task_graph.complete({});

        camera.camera.resize(size_x, size_y);
    }

    ~TiledForwardApp() {
        device.wait_idle();
        device.collect_garbage();
        device.destroy_image(depth_image);
        device.destroy_buffer(camera_buffer);
        device.destroy_buffer(object_buffer);
        device.destroy_buffer(frustums_buffer);
        device.destroy_buffer(point_light_buffer);
        device.destroy_buffer(point_light_index_buffer);
        device.destroy_buffer(point_light_grid_buffer);
        device.destroy_sampler(depth_sampler);
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

            camera.update(delta_time);

            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            ImGui::Begin("Tiled Forward Settings");
            if(ImGui::Checkbox("cull lights", &cull_lights)) {
                daxa::ShaderDefine cull_lights_define = { .name = "CULL_LIGHTS", .value = "0" };
                if(cull_lights) {
                    cull_lights_define.value = "1";
                }

                raster_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
                    .vertex_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/tiled_forward/raster.glsl" }, },
                        .compile_options = {
                            .defines = { 
                                cull_lights_define
                            }
                        } 
                    },
                    .fragment_shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/tiled_forward/raster.glsl" }, },
                        .compile_options = {
                            .defines = { 
                                cull_lights_define
                            }
                        } 
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

                compute_frustum_pipeline.pipeline = pipeline_manager.add_compute_pipeline(daxa::ComputePipelineCompileInfo {
                    .shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/tiled_forward/compute_frustum_grid.glsl" }, },
                    },
                    .push_constant_size = sizeof(ComputeFrustumsPush),
                    .name = "compute frustum pipeline"
                }).value();

                compute_light_list_pipeline.pipeline = pipeline_manager.add_compute_pipeline(daxa::ComputePipelineCompileInfo {
                    .shader_info = daxa::ShaderCompileInfo {
                        .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/tiled_forward/compute_light_list.glsl" }, },
                    },
                    .push_constant_size = sizeof(ComputeLightListPush),
                    .name = "compute light list pipeline"
                }).value();

                depth_prepass_pipeline.pipeline = pipeline_manager.add_raster_pipeline(daxa::RasterPipelineCompileInfo {
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
            });
            task_depth_image.set_images({.images = std::span{&depth_image, 1}});

            work_groups_x = (size_x + (size_x % TILE_SIZE)) / TILE_SIZE;
            work_groups_y = (size_y + (size_y % TILE_SIZE)) / TILE_SIZE;
            number_of_tiles = work_groups_x * work_groups_y;

            device.destroy_buffer(frustums_buffer);
            frustums_buffer = device.create_buffer(daxa::BufferInfo {
                .size = static_cast<u32>(sizeof(Frustum) * number_of_tiles),
                .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
                .name = "frustums buffer"
            });
            task_frustums_buffer.set_buffers({.buffers = std::span{&frustums_buffer, 1}});

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
    TiledForwardApp app;
    app.update();
    return 0;
}
