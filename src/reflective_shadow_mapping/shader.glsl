#include "shared.inl"
#include "samples.glsl"

DAXA_DECL_PUSH_CONSTANT(DrawPush, push)

#define MATERIAL deref(push.materials[push.material_index])

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;
layout(location = 1) out f32vec4 out_position_shadow;
layout(location = 2) out f32vec3 out_normal;
layout(location = 3) out f32vec3 out_position;

void main() {
    out_uv = deref(push.vertices[gl_VertexIndex]).uv;
    out_position_shadow = deref(push.light_buffer).projection_matrix * deref(push.light_buffer).view_matrix * deref(push.model_buffer).model_matrix * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
    out_normal = normalize(f32mat3x3(deref(push.model_buffer).normal_matrix) * deref(push.vertices[gl_VertexIndex]).normal);
    out_position = (deref(push.model_buffer).model_matrix * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0)).xyz;
    gl_Position = push.mvp * deref(push.model_buffer).model_matrix * f32vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;
layout(location = 1) in f32vec4 in_position_shadow;
layout(location = 2) in f32vec3 in_normal;
layout(location = 3) in f32vec3 in_position;

layout(location = 0) out f32vec4 color;

f32 calculate_shadow(daxa_ImageViewId shadow_image, daxa_SamplerId shadow_sampler, f32vec4 shadow_coord, f32vec2 off, f32 bias) {
    f32vec3 proj_coord = f32vec3(shadow_coord.xy + off, shadow_coord.z - bias);
	return texture(daxa_sampler2DShadow(shadow_image, shadow_sampler), proj_coord.xyz).r;
}

f32 shadow_pcf(daxa_ImageViewId shadow_image, daxa_SamplerId shadow_sampler, f32vec4 shadow_coord, f32 bias) {
    i32vec2 tex_dim = textureSize(daxa_sampler2DShadow(shadow_image, shadow_sampler), 0);
	f32 scale = 0.25;
	f32 dx = scale * 1.0 / f32(tex_dim.x);
	f32 dy = scale * 1.0 / f32(tex_dim.y);

	f32 shadow_factor = 0.0;
	i32 count = 0;
	i32 range = push.pcf_range;
	
	for (i32 x = -range; x <= range; x++) {
		for (i32 y = -range; y <= range; y++) {
			shadow_factor += calculate_shadow(shadow_image, shadow_sampler, shadow_coord, f32vec2(dx*x, dy*y), bias);
			count++;
		}
	
	}
	return (shadow_factor / count);
}

const uint N_SAMPLES = 151;

f32vec3 get_world_position_from_depth(f32vec2 uv, f32 depth) {
    f32vec4 clipSpacePosition = f32vec4(uv * 2.0 - 1.0, depth, 1.0);
    f32vec4 viewSpacePosition = deref(push.light_buffer).inverse_projection_matrix * clipSpacePosition;

    viewSpacePosition /= viewSpacePosition.w;
    f32vec4 worldSpacePosition = deref(push.light_buffer).inverse_view_matrix * viewSpacePosition;

    return worldSpacePosition.xyz;
}

f32vec3 indirectLighting( f32vec2 uvFrag, f32vec3 n, f32vec3 x ) {
	f32vec3 rsmShading = f32vec3(0);
	for( i32 i = 0; i < N_SAMPLES; i++ ) {
		f32vec2 uv = uvFrag + push.gi_radius * RSMSamplePositions[i];
		f32vec3 flux = texture(daxa_sampler2D(deref(push.light_buffer).shadow_flux_image, deref(push.light_buffer).shadow_sampler), uv).rgb;
        f32vec3 x_p = get_world_position_from_depth(uv, texture(daxa_sampler2D(deref(push.light_buffer).shadow_depth_image, deref(push.light_buffer).image_sampler), uv).r);
		f32vec3 n_p = texture(daxa_sampler2D(deref(push.light_buffer).shadow_normal_image, deref(push.light_buffer).shadow_sampler), uv).xyz;

		f32vec3 r = x - x_p;	
		f32 d2 = dot( r, r );			
		f32vec3 E_p = flux * ( max( 0.0, dot( n_p, r ) ) * max( 0.0, dot( n, -r ) ) );
		E_p *= RSMSamplePositions[i].x * RSMSamplePositions[i].x / ( d2 * d2 );	

		rsmShading += E_p;	
	}

	return rsmShading * push.gi_intensity;		
}

void main() {
    f32vec4 shadow_coord = in_position_shadow / in_position_shadow.w;
    shadow_coord.xy = shadow_coord.xy * 0.5 + 0.5;

    f32vec3 albedo = sample_texture(MATERIAL.albedo_image, in_uv).rgb;
    f32vec3 ambient = f32vec3(push.shadow_intensity);

#if USE_PCF == 0
    f32vec3 direct = f32vec3(calculate_shadow(
        deref(push.light_buffer).shadow_depth_image, 
        deref(push.light_buffer).shadow_sampler, 
        shadow_coord, 
        f32vec2(0.0, 0.0), 
        push.bias));
#else 
    f32vec3 direct = f32vec3(shadow_pcf(
        deref(push.light_buffer).shadow_depth_image, 
        deref(push.light_buffer).shadow_sampler, 
        shadow_coord, 
        push.bias));
#endif

#if APPLY_GI == 1
    f32vec3 indirect = indirectLighting(shadow_coord.xy, in_normal, in_position);
#else
    f32vec3 indirect = f32vec3(0.0);
#endif

    color = f32vec4((direct * max(0.0, dot(in_normal, -deref(push.light_buffer).light_direction)) + indirect + ambient) * albedo, 1.0);
}

#endif