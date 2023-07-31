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
layout(location = 0) out f32vec4 out_emissive;

f32vec3 PowVec3(f32vec3 v, f32 p) {
    return f32vec3(pow(v.x, p), pow(v.y, p), pow(v.z, p));
}

const f32 invGamma = 1.0 / 2.2;
f32vec3 ToSRGB(f32vec3 v)   { return PowVec3(v, invGamma); }

f32 sRGBToLuma(f32vec3 col) {
    //return dot(col, f32vec3(0.2126f, 0.7152f, 0.0722f));
	return dot(col, f32vec3(0.299f, 0.587f, 0.114f));
}

f32 KarisAverage(f32vec3 col) {
	// Formula is 1 / (1 + luma)
	f32 luma = sRGBToLuma(ToSRGB(col)) * 0.25f;
	return 1.0f / (1.0f + luma);
}

void main() {
    f32vec2 srcTexelSize = 1.0 / push.src_resolution;
	f32 x = srcTexelSize.x;
	f32 y = srcTexelSize.y;

	// Take 13 samples around current texel:
	// a - b - c
	// - j - k -
	// d - e - f
	// - l - m -
	// g - h - i
	// === ('e' is the current texel) ===
	f32vec3 a = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x - 2*x, in_uv.y + 2*y)).rgb;
	f32vec3 b = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x,       in_uv.y + 2*y)).rgb;
	f32vec3 c = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x + 2*x, in_uv.y + 2*y)).rgb;

	f32vec3 d = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x - 2*x, in_uv.y)).rgb;
	f32vec3 e = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x,       in_uv.y)).rgb;
	f32vec3 f = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x + 2*x, in_uv.y)).rgb;

	f32vec3 g = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x - 2*x, in_uv.y - 2*y)).rgb;
	f32vec3 h = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x,       in_uv.y - 2*y)).rgb;
	f32vec3 i = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x + 2*x, in_uv.y - 2*y)).rgb;

	f32vec3 j = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x - x, in_uv.y + y)).rgb;
	f32vec3 k = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x + x, in_uv.y + y)).rgb;
	f32vec3 l = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x - x, in_uv.y - y)).rgb;
	f32vec3 m = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), f32vec2(in_uv.x + x, in_uv.y - y)).rgb;


    out_emissive.rgb = e*0.125;                // ok
    out_emissive.rgb += (a+c+g+i)*0.03125;     // ok
    out_emissive.rgb += (b+d+f+h)*0.0625;      // ok
    out_emissive.rgb += (j+k+l+m)*0.125;       // ok
}

#endif