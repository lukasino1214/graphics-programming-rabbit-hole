#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(DrawPush, push)

#define MATERIAL deref(push.materials[push.material_index])

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;
layout(location = 1) out f32vec3 out_position;
layout(location = 2) out f32vec3 out_normal;

void main() {
    out_uv = deref(push.vertices[gl_VertexIndex]).uv;
    out_position = (deref(push.object_info).model_matrix * vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0)).xyz;
    out_normal = normalize(f32mat3x3(deref(push.object_info).normal_matrix) * deref(push.vertices[gl_VertexIndex]).normal);
    gl_Position = deref(push.camera_info).projection_matrix * deref(push.camera_info).view_matrix * deref(push.object_info).model_matrix * vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;
layout(location = 1) in f32vec3 in_position;
layout(location = 2) in f32vec3 in_normal;

layout(location = 0) out f32vec4 out_color;

void main() {
    f32vec3 color = sample_texture(MATERIAL.albedo_image, in_uv).rgb;

    ivec2 tile_id = ivec2(gl_FragCoord.xy / TILE_SIZE);
    uint tile_index = tile_id.y * push.tile_nums.x + tile_id.x;
    uint offset = tile_index * NUM_LIGHTS;

    vec3 camera_position = deref(push.camera_info).position;

    vec3 normal = normalize(in_normal);

    color *= 0.1;

    float count = 0.0;
    // for (int i = 0; i < NUM_LIGHTS; i++) {
    //     PointLight light = deref(push.point_light_buffer[i]);
    for (int i = 0; i < NUM_LIGHTS && deref(push.visible_point_light_indices[offset+i]).index != -1; i++) {
        PointLight light = deref(push.point_light_buffer[deref(push.visible_point_light_indices[offset + i]).index]);
//		vec3 light_dir = normalize(light.position - in_position);
//        float lambertian = max(dot(light_dir, normal), 0.0);
//
//        if(lambertian > 0.0)
//        {
//            float light_distance = distance(light.position, in_position);
//            if (light_distance > light.radius)
//            {
//                continue;
//            }
//
//            vec3 viewDir = normalize(camera_positon - in_position);
//            vec3 halfDir = normalize(light_dir + viewDir);
//            float specAngle = max(dot(halfDir, normal), 0.0);
//            float specular = pow(specAngle, 32.0);  // TODO?: spec color & power in g-buffer?
//
//            float att = clamp(1.0 - light_distance * light_distance / (light.radius * light.radius), 0.0, 1.0);
//            illuminance += att * (lambertian * color + specular);
//        }

        count += 1.0;
        f32vec3 light_dir = normalize(light.position - in_position);

        f32 distance = length(light.position.xyz - in_position);
        if (distance > light.radius) { continue; }

        f32 attenuation = 1.0f / (distance * distance);

        f32vec3 view_dir = normalize(camera_position - in_position);
        f32vec3 halfway_dir = normalize(light_dir + view_dir);

        f32 diffuse = max(dot(normal, light_dir), 0);
        f32 normal_half = acos(dot(halfway_dir, normal));
        f32 exponent = normal_half * 1.0;
        exponent = -(exponent * exponent);
        color += color * light.color * (diffuse + exp(exponent)) * attenuation * light.radius;
	}

    out_color = f32vec4(color, 1.0);
}

#endif