#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(DrawPush, push)

#define MATERIAL deref(push.materials[push.material_index])

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;
layout(location = 1) out f32vec3 out_position;
layout(location = 2) out f32vec3 out_normal;

void main() {
    out_uv = deref(push.vertices[gl_VertexIndex]).uv;
    out_position= f32vec3(deref(push.object_info).model_matrix * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0));
    out_normal = f32mat3x3(deref(push.object_info).normal_matrix) * deref(push.vertices[gl_VertexIndex]).normal;
    gl_Position = deref(push.camera_info).projection_matrix * deref(push.camera_info).view_matrix * deref(push.object_info).model_matrix * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;
layout(location = 1) in f32vec3 in_position;
layout(location = 2) in f32vec3 in_normal;

layout(location = 0) out f32vec4 out_color;

void main() {
    f32vec3 color = sample_texture(MATERIAL.albedo_image, in_uv).rgb;
    color *= 0.1;

    f32vec3 camera_position = deref(push.camera_info).position;
    f32vec3 normal = normalize(in_normal);
#if CULL_LIGHTS == 0
    for(uint i = 0; i < NUM_LIGHTS; i++) {
        PointLight light = deref(push.point_light_buffer[i]);

        f32vec3 light_dir = normalize(light.position - in_position);
        f32 distance = length(light.position - in_position);
        f32 attenuation = 1.0f / (distance * distance);

        f32vec3 view_dir = normalize(camera_position - in_position);
        f32vec3 halfway_dir = normalize(light_dir + view_dir);

        f32 diffuse = max(dot(normal, light_dir), 0);
        f32 normal_half = acos(dot(halfway_dir, normal));
        f32 exponent = normal_half * 1.0;
        exponent = -(exponent * exponent);
        color += color * light.color * ((diffuse + exp(exponent))) * attenuation * light.radius;
    }
#elif CULL_LIGHTS == 1
    ivec2 tile_id = ivec2(gl_FragCoord.xy / TILE_SIZE);
    uint tile_index = tile_id.y * push.tile_nums.x + tile_id.x;

    // u32 start = deref(push.point_light_grid_buffer[tile_index]).start;
    // u32 count = deref(push.point_light_grid_buffer[tile_index]).count; 
    uint tile_offset = tile_index * NUM_LIGHTS;
    uint count = deref(push.point_light_grid_buffer[tile_index]).count;
    for(uint i = 0; i < count; i++) {
        //uint index = deref(push.point_light_index_buffer[start + i]).index;
        uint index = deref(push.point_light_index_buffer[tile_offset + i]).index;
        PointLight light = deref(push.point_light_buffer[index]);

        f32vec3 light_dir = normalize(light.position - in_position);
        f32 distance = length(light.position - in_position);
        f32 attenuation = 1.0f / (distance * distance);

        f32vec3 view_dir = normalize(camera_position - in_position);
        f32vec3 halfway_dir = normalize(light_dir + view_dir);

        f32 diffuse = max(dot(normal, light_dir), 0);
        f32 normal_half = acos(dot(halfway_dir, normal));
        f32 exponent = normal_half * 1.0;
        exponent = -(exponent * exponent);
        color += color * light.color * ((diffuse + exp(exponent))) * attenuation * light.radius;
    }

        // for(uint i = 0; i < NUM_LIGHTS && deref(push.point_light_index_buffer[tile_offset + i]).index != -1; i++) {
        //     //uint index = deref(push.point_light_index_buffer[start + i]).index;
        //     uint index = deref(push.point_light_index_buffer[tile_offset + i]).index;
        //     PointLight light = deref(push.point_light_buffer[index]);

        //     f32vec3 light_dir = normalize(light.position - in_position);
        //     f32 distance = length(light.position - in_position);
        //     f32 attenuation = 1.0f / (distance * distance);

        //     f32vec3 view_dir = normalize(camera_position - in_position);
        //     f32vec3 halfway_dir = normalize(light_dir + view_dir);

        //     f32 diffuse = max(dot(normal, light_dir), 0);
        //     f32 normal_half = acos(dot(halfway_dir, normal));
        //     f32 exponent = normal_half * 1.0;
        //     exponent = -(exponent * exponent);
        //     color += color * light.color * ((diffuse + exp(exponent))) * attenuation * light.radius;
        // }
#endif

    out_color = f32vec4(color, 1.0);
}

#endif