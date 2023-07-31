#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(DrawPush, push)

#define MATERIAL deref(push.materials[push.material_index])

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;
layout(location = 1) out f32vec3 out_normal;
layout(location = 2) out f32vec3 out_position;
#if USE_DERIVATIVES == 0
layout(location = 3) out f32vec3 out_tangent;
layout(location = 4) out f32vec3 out_bittangent;
#endif

void main() {
    out_uv = deref(push.vertices[gl_VertexIndex]).uv;
    out_normal = normalize(f32mat3x3(deref(push.object_info).normal_matrix) * deref(push.vertices[gl_VertexIndex]).normal);
    out_position = (deref(push.object_info).model_matrix * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0)).xyz;
#if USE_DERIVATIVES == 0
    out_tangent = normalize(f32mat3x3(deref(push.object_info).normal_matrix) * deref(push.vertices[gl_VertexIndex]).tangent.xyz);
    out_bittangent = normalize(f32mat3x3(deref(push.object_info).normal_matrix) * (cross(deref(push.vertices[gl_VertexIndex]).normal, deref(push.vertices[gl_VertexIndex]).tangent.xyz) * deref(push.vertices[gl_VertexIndex]).tangent.w));
#endif
    gl_Position = deref(push.camera_info).projection_matrix * deref(push.camera_info).view_matrix * deref(push.object_info).model_matrix * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;
layout(location = 1) in f32vec3 in_normal;
layout(location = 2) in f32vec3 in_position;
#if USE_DERIVATIVES == 0
layout(location = 3) in f32vec3 in_tangent;
layout(location = 4) in f32vec3 in_bittangent;
#endif

layout(location = 0) out f32vec4 out_color;

void main() {
    f32vec3 color = sample_texture(MATERIAL.albedo_image, in_uv).rgb;

#if USE_NORMAL_MAPPING == 1
#if USE_DERIVATIVES == 1
    f32vec3 tangent_normal = sample_texture(MATERIAL.normal_image, in_uv).xyz * 2.0 - 1.0;

    f32vec3 Q1  = dFdx(in_position);
    f32vec3 Q2  = dFdy(in_position);
    f32vec2 st1 = dFdx(in_uv);
    f32vec2 st2 = dFdy(in_uv);

    f32vec3 N = normalize(in_normal);

    f32vec3 T = -normalize(Q1*st2.t - Q2*st1.t);
    f32vec3 B = normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    f32vec3 normal = normalize(TBN * tangent_normal);
#else
    f32vec3 tangent_normal = sample_texture(MATERIAL.normal_image, in_uv).xyz * 2.0 - 1.0;

    f32vec3 T = normalize(in_tangent);
    f32vec3 B = normalize(in_bittangent);
    f32vec3 N = normalize(in_normal);

    mat3 TBN = mat3(T, B, N);

    f32vec3 normal = normalize(TBN * tangent_normal);
#endif
#else
    f32vec3 normal = in_normal;
#endif

    f32vec3 light_direction = normalize(push.light_position - in_position);
    f32vec3 diffuse = color * max(dot(light_direction, normal), 0.0);

    f32vec3 view_direction = normalize(deref(push.camera_info).position - in_position);
    f32vec3 reflect_direction = reflect(-light_direction, normal);
    f32vec3 halfway_direction = normalize(light_direction + view_direction);
    f32vec3 specular = f32vec3(0.2) * pow(max(dot(normal, halfway_direction), 0.0), 32.0);

#if DEBUG_NORMAL == 0
    out_color = f32vec4(0.1 * color + diffuse + specular, 1.0);
#else
    out_color = f32vec4(normal * 0.5 + 0.5, 1.0);
#endif
}

#endif