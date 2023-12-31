#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(BloomPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;

void main() {
    out_uv = f32vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = f32vec4(out_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;
layout (location = 0) out f32vec4 out_emissive;

void main() {
	f32 x = push.filter_radius;
	f32 y = push.filter_radius;

	// Take 9 samples around current texel:
	// a - b - c
	// d - e - f
	// g - h - i
	// === ('e' is the current texel) ===
	f32vec3 a = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x - x, in_uv.y + y)).rgb;
	f32vec3 b = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x,     in_uv.y + y)).rgb;
	f32vec3 c = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x + x, in_uv.y + y)).rgb;

	f32vec3 d = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x - x, in_uv.y)).rgb;
	f32vec3 e = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x,     in_uv.y)).rgb;
	f32vec3 f = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x + x, in_uv.y)).rgb;

	f32vec3 g = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x - x, in_uv.y - y)).rgb;
	f32vec3 h = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x,     in_uv.y - y)).rgb;
	f32vec3 i = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x + x, in_uv.y - y)).rgb;

	// Apply weighted distribution, by using a 3x3 tent filter:
	//  1   | 1 2 1 |
	// -- * | 2 4 2 |
	// 16   | 1 2 1 |
	out_emissive.rgb = e*4.0;
	out_emissive.rgb += (b+d+f+h)*2.0;
	out_emissive.rgb += (a+c+g+i);
	out_emissive.rgb *= 1.0 / 16.0;
}
#endif