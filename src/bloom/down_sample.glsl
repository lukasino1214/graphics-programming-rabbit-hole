#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(BloomPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out vec2 out_uv;

void main() {
    out_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(out_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_emissive;

vec3 PowVec3(vec3 v, float p) {
    return vec3(pow(v.x, p), pow(v.y, p), pow(v.z, p));
}

const float invGamma = 1.0 / 2.2;
vec3 ToSRGB(vec3 v)   { return PowVec3(v, invGamma); }

float sRGBToLuma(vec3 col) {
    //return dot(col, vec3(0.2126f, 0.7152f, 0.0722f));
	return dot(col, vec3(0.299f, 0.587f, 0.114f));
}

float KarisAverage(vec3 col) {
	// Formula is 1 / (1 + luma)
	float luma = sRGBToLuma(ToSRGB(col)) * 0.25f;
	return 1.0f / (1.0f + luma);
}

void main() {
    vec2 srcTexelSize = 1.0 / push.src_resolution;
	float x = srcTexelSize.x;
	float y = srcTexelSize.y;

	// Take 13 samples around current texel:
	// a - b - c
	// - j - k -
	// d - e - f
	// - l - m -
	// g - h - i
	// === ('e' is the current texel) ===
	vec3 a = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), vec2(in_uv.x - 2*x, in_uv.y + 2*y)).rgb;
	vec3 b = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), vec2(in_uv.x,       in_uv.y + 2*y)).rgb;
	vec3 c = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), vec2(in_uv.x + 2*x, in_uv.y + 2*y)).rgb;

	vec3 d = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), vec2(in_uv.x - 2*x, in_uv.y)).rgb;
	vec3 e = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), vec2(in_uv.x,       in_uv.y)).rgb;
	vec3 f = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), vec2(in_uv.x + 2*x, in_uv.y)).rgb;

	vec3 g = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), vec2(in_uv.x - 2*x, in_uv.y - 2*y)).rgb;
	vec3 h = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), vec2(in_uv.x,       in_uv.y - 2*y)).rgb;
	vec3 i = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), vec2(in_uv.x + 2*x, in_uv.y - 2*y)).rgb;

	vec3 j = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), vec2(in_uv.x - x, in_uv.y + y)).rgb;
	vec3 k = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), vec2(in_uv.x + x, in_uv.y + y)).rgb;
	vec3 l = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), vec2(in_uv.x - x, in_uv.y - y)).rgb;
	vec3 m = texture(daxa_sampler2D(push.image_view_id, push.sampler_id), vec2(in_uv.x + x, in_uv.y - y)).rgb;


    out_emissive.rgb = e*0.125;                // ok
    out_emissive.rgb += (a+c+g+i)*0.03125;     // ok
    out_emissive.rgb += (b+d+f+h)*0.0625;      // ok
    out_emissive.rgb += (j+k+l+m)*0.125;       // ok
}

#endif