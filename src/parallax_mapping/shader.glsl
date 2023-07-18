#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(DrawPush, push)

#define MATERIAL deref(push.materials[push.material_index])

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;
layout (location = 1) out f32vec3 out_tangent_light_position;
layout (location = 2) out f32vec3 out_tangent_camera_position;
layout (location = 3) out f32vec3 out_tangent_frag_position;

void main() {
    out_uv = deref(push.vertices[gl_VertexIndex]).uv;
    out_tangent_frag_position = vec3(deref(push.object_info).model_matrix * vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0));   
    
    gl_Position = deref(push.camera_info).projection_matrix * deref(push.camera_info).view_matrix * deref(push.object_info).model_matrix * vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
    
    vec3 N = normalize(mat3(deref(push.object_info).normal_matrix) * deref(push.vertices[gl_VertexIndex]).normal);
	vec3 T = normalize(mat3(deref(push.object_info).normal_matrix) * deref(push.vertices[gl_VertexIndex]).tangent.xyz);
	vec3 B = normalize(cross(N, T));
	mat3 TBN = transpose(mat3(T, B, N));

	out_tangent_light_position = TBN * push.light_position;
	out_tangent_camera_position  = TBN * deref(push.camera_info).position * -1.0;
	out_tangent_frag_position  = TBN * out_tangent_frag_position;
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;
layout (location = 1) in f32vec3 in_tangent_light_position;
layout (location = 2) in f32vec3 in_tangent_camera_position;
layout (location = 3) in f32vec3 in_tangent_frag_position;

layout(location = 0) out f32vec4 out_color;

f32vec2 offset_limiting(f32vec2 uv, f32vec3 view_direction) {
	f32 height = 1.0 - sample_texture(push.heightmap_texture, uv).r;
	f32vec2 p = view_direction.xy * (height * (push.height_scale * 0.5) + push.parallax_bias) / view_direction.z;
	return uv - p;  
}

f32vec2 steep_parallax_mapping(f32vec2 uv, f32vec3 view_direction) {
	f32 layer_depth = 1.0 / push.layers;
	f32 current_layer_depth = 0.0;
	f32vec2 delta_uv = view_direction.xy * push.height_scale / (view_direction.z * push.layers);
	f32vec2 current_uv = uv;
	f32 height = 1.0 - sample_texture(push.heightmap_texture, current_uv).r;
	for (int i = 0; i < push.layers; i++) {
		current_layer_depth += layer_depth;
		current_uv -= delta_uv;
		height = 1.0 - sample_texture(push.heightmap_texture, current_uv).r;
		if (height < current_layer_depth) { break; }
	}
	return current_uv;
}

f32vec2 parallax_occlusion_mapping(f32vec2 uv, f32vec3 view_direction) {
	f32 layer_depth = 1.0 / push.layers;
	f32 current_layer_depth = 0.0;
	f32vec2 delta_uv = view_direction.xy * push.height_scale / (view_direction.z * push.layers);
	f32vec2 current_uv = uv;
	f32 height = 1.0 - sample_texture(push.heightmap_texture, current_uv).r;
	for (int i = 0; i < push.layers; i++) {
		current_layer_depth += layer_depth;
		current_uv -= delta_uv;
		height = 1.0 - sample_texture(push.heightmap_texture, current_uv).r;
		if (height < current_layer_depth) { break; }
	}
	f32vec2 previous_uv = current_uv + delta_uv;
	f32 next_depth = height - current_layer_depth;
	f32 previous_depth = 1.0 - sample_texture(push.heightmap_texture, previous_uv).r - current_layer_depth + layer_depth;
	return mix(current_uv, previous_uv, next_depth / (next_depth - previous_depth));
}

f32vec2 relief_parallax_mapping(f32vec2 uv, f32vec3 view_direction) {
    f32 layer_depth = 1.0 / push.layers;
	f32 current_layer_depth = 0.0;
	f32vec2 delta_uv = view_direction.xy * push.height_scale / (view_direction.z * push.layers);
	f32vec2 current_uv = uv;
	f32 height = 1.0 - sample_texture(push.heightmap_texture, current_uv).r;
	for (int i = 0; i < push.layers; i++) {
		current_layer_depth += layer_depth;
		current_uv -= delta_uv;
		height = 1.0 - sample_texture(push.heightmap_texture, current_uv).r;
		if (height < current_layer_depth) {
            current_uv += delta_uv;
            current_layer_depth -= layer_depth;
            height = 1.0 - sample_texture(push.heightmap_texture, current_uv).r - current_layer_depth + layer_depth;
			break;
		}
	}

    for (int i = 0; i < push.layers * 0.5; i++) {
        delta_uv *= 0.5;
        layer_depth *= 0.5;
		height = 1.0 - sample_texture(push.heightmap_texture, current_uv).r;

		if (height > current_layer_depth) {
			current_layer_depth += layer_depth;
		    current_uv -= delta_uv;
		} else {
            current_layer_depth -= layer_depth;
		    current_uv += delta_uv;
        }
	}

    return current_uv;
}

f32vec2 contact_refinement_parallax_mapping(f32vec2 uv, f32vec3 view_direction) {
    f32 layer_depth = 1.0 / push.layers;
	f32 current_layer_depth = 0.0;
	f32vec2 delta_uv = view_direction.xy * push.height_scale / (view_direction.z * push.layers);
	f32vec2 current_uv = uv;
	f32 height = 1.0 - sample_texture(push.heightmap_texture, current_uv).r;
	for (int i = 0; i < push.layers; i++) {
		current_layer_depth += layer_depth;
		current_uv -= delta_uv;
		height = 1.0 - sample_texture(push.heightmap_texture, current_uv).r;
		if (height < current_layer_depth) {
            current_uv += delta_uv;
            current_layer_depth -= layer_depth;
            height = 1.0 - sample_texture(push.heightmap_texture, current_uv).r - current_layer_depth + layer_depth;
			break;
		}
	}

    for (int i = 2; i < push.layers * 0.5 + 2.0; i++) {
        delta_uv /= float(i);
        layer_depth /= float(i);
		height = 1.0 - sample_texture(push.heightmap_texture, current_uv).r;

		if (height > current_layer_depth) {
			current_layer_depth += layer_depth;
		    current_uv -= delta_uv;
		} else {
            current_layer_depth -= layer_depth;
		    current_uv += delta_uv;
        }
	}

    return current_uv;
}

void main() {
    f32vec2 uv = in_uv;
#if MAPPING_MODE == 0
    out_color = f32vec4(sample_texture(MATERIAL.albedo_image, uv).rgb, 1.0);
#else
    f32vec3 tangent_view_direction = normalize(in_tangent_camera_position - in_tangent_frag_position);
#endif

#if MAPPING_MODE == 2
    uv = offset_limiting(uv, tangent_view_direction);
#elif MAPPING_MODE == 3
    uv = steep_parallax_mapping(uv, tangent_view_direction);
#elif MAPPING_MODE == 4
    uv = parallax_occlusion_mapping(uv, tangent_view_direction);
#elif MAPPING_MODE == 5
    uv = relief_parallax_mapping(uv, tangent_view_direction);
#elif MAPPING_MODE == 6
    uv = contact_refinement_parallax_mapping(uv, tangent_view_direction);
#endif

#if MAPPING_MODE != 0
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { discard; }

    f32vec3 color = sample_texture(MATERIAL.albedo_image, uv).rgb;

    f32vec3 tangent_normal = sample_texture(MATERIAL.normal_image, uv).xyz * 2.0 - 1.0;
    f32vec3 normal = normalize(tangent_normal);

    f32vec3 light_direction = normalize(in_tangent_light_position - in_tangent_frag_position);
    f32vec3 diffuse = color * max(dot(light_direction, normal), 0.0);

    f32vec3 view_direction = tangent_view_direction;
    f32vec3 reflect_direction = reflect(-light_direction, normal);
    f32vec3 halfway_direction = normalize(light_direction + view_direction);
    f32vec3 specular = f32vec3(0.2) * pow(max(dot(normal, halfway_direction), 0.0), 32.0);


    out_color = f32vec4(0.1 * color + diffuse + specular, 1.0);
#endif
}

#endif