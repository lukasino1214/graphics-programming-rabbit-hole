#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

struct TextureId {
    daxa_ImageViewId image_id;
    daxa_SamplerId sampler_id;
};

#define sample_texture(tex, uv) texture(daxa_sampler2D(tex.image_id, tex.sampler_id), uv)

struct Material {
    TextureId albedo_image;
    i32 has_albedo_image;
    TextureId mettalic_roughness_image;
    i32 has_mettalic_roughness_image;
    TextureId normal_image;
    i32 has_normal_image;
    TextureId occlusion_image;
    i32 has_occlusion_image;
    TextureId emissive_image;
    i32 has_emissive_image;
};

DAXA_DECL_BUFFER_PTR(Material)

struct Primitive {
    u32 first_index;
    u32 first_vertex;
    u32 index_count;
    u32 vertex_count;
    u32 material_index;
};

struct Vertex {
    f32vec3 position;
    f32vec3 normal;
    f32vec2 uv;
    f32vec4 tangent;
};

DAXA_DECL_BUFFER_PTR(Vertex)