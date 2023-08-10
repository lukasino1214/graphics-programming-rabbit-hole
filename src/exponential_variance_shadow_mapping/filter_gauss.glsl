#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(GaussPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX
layout(location = 0) out f32vec2 out_uv;

void main() {
    out_uv = f32vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = f32vec4(out_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;
layout(location = 0) out f32vec4 out_color;
void main() {
    f32vec4 color = f32vec4(0.0);
	
	color += texture(daxa_sampler2D(push.src_image, push.image_sampler), in_uv + (f32vec2(-3.0) * push.blur_scale.xy)).xyzw * ( 1.0 / 64.0 );
	color += texture(daxa_sampler2D(push.src_image, push.image_sampler), in_uv + (f32vec2(-2.0) * push.blur_scale.xy)).xyzw * ( 6.0 / 64.0 );
	color += texture(daxa_sampler2D(push.src_image, push.image_sampler), in_uv + (f32vec2(-1.0) * push.blur_scale.xy)).xyzw * (15.0 / 64.0 );
	color += texture(daxa_sampler2D(push.src_image, push.image_sampler), in_uv + (f32vec2(+0.0) * push.blur_scale.xy)).xyzw * (20.0 / 64.0 );
	color += texture(daxa_sampler2D(push.src_image, push.image_sampler), in_uv + (f32vec2(+1.0) * push.blur_scale.xy)).xyzw * (15.0 / 64.0 );
	color += texture(daxa_sampler2D(push.src_image, push.image_sampler), in_uv + (f32vec2(+2.0) * push.blur_scale.xy)).xyzw * ( 6.0 / 64.0 );
	color += texture(daxa_sampler2D(push.src_image, push.image_sampler), in_uv + (f32vec2(+3.0) * push.blur_scale.xy)).xyzw * ( 1.0 / 64.0 );

	out_color = f32vec4(color);
}
#endif