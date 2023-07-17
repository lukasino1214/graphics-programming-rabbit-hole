#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(SSAOBlurPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;

void main() {
    out_uv = f32vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = f32vec4(out_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;

layout(location = 0) out f32 out_ssao;

#define SIGMA 10.0
#define BSIGMA 0.1
#define MSIZE 15

float normpdf(in float x, in float sigma) {
	return 0.39894*exp(-0.5*x*x/(sigma*sigma))/sigma;
}

float normpdf3(in vec3 v, in float sigma) {
	return 0.39894*exp(-0.5*dot(v,v)/(sigma*sigma))/sigma;
}

float linearize_depth(float d,float zNear,float zFar) {
    return zNear * zFar / (zFar + d * (zNear - zFar));
}

void main() {
#if BLUR_MODE == 0
    const i32 blur_range = 2;
	i32 n = 0;
	f32vec2 texel_size = 1.0 / f32vec2(textureSize(daxa_sampler2D(push.ssao, push.sampler_id), 0));
	f32 result = 0.0;
	for (i32 x = -blur_range; x < blur_range; x++) {
		for (i32 y = -blur_range; y < blur_range; y++) {
			f32vec2 offset = f32vec2(f32(x), f32(y)) * texel_size;
            result += texture(daxa_sampler2D(push.ssao, push.sampler_id), in_uv + offset).r;
			n++;
		}
	}
	out_ssao = result / f32(n);
#elif BLUR_MODE == 1
	float d = linearize_depth(texture(daxa_sampler2D(push.ssao, push.sampler_id), in_uv).r, 0.1, 1000.0);

	//declare stuff
	const int kSize = (MSIZE-1)/2;
	float kernel[MSIZE];
	float final_colour = 0.0;
	
	//create the 1-D kernel
	float Z = 0.0;
	for (int j = 0; j <= kSize; ++j) {
		kernel[kSize+j] = kernel[kSize-j] = normpdf(float(j), SIGMA);
	}
	
	float cc;
	float dd;
	float factor;
	float bZ = 1.0/normpdf(0.0, BSIGMA);
	const vec2 texelSize = 1.0 / vec2(textureSize(daxa_sampler2D(push.ssao, push.sampler_id), 0));
	//read out the texels
	for (int i=-kSize; i <= kSize; ++i)
	{
		for (int j=-kSize; j <= kSize; ++j)
		{
			cc = texture(daxa_sampler2D(push.ssao, push.sampler_id), in_uv + texelSize * vec2(float(i),float(j))).r;
			dd = linearize_depth(texture(daxa_sampler2D(push.ssao, push.sampler_id), in_uv + texelSize * vec2(float(i),float(j))).r, 0.1, 1000.0);
			float diff = abs(dd-d);
			factor = normpdf(pow(diff, 0.35), BSIGMA)*bZ*kernel[kSize+i];
			Z += factor;
			final_colour += factor*cc;
		}
	}
	
	out_ssao = final_colour/Z;

#endif
}

#endif