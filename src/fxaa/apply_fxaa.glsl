#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(FXAAPush, push)

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
#if USE_FXAA == 0
    out_color = f32vec4(texture(daxa_sampler2D(push.image, push.image_sampler), in_uv).rgb, 1.0);
#else
    f32vec2 u_texelStep = 1.0 / f32vec2(textureSize(daxa_sampler2D(push.image, push.image_sampler), 0));
    f32 u_lumaThreshold = push.luma_threshold;
    f32 u_mulReduce = 1.0 / push.mul_reduce;
    f32 u_minReduce = 1.0 / push.min_reduce;
    f32 u_maxSpan = push.max_span;

    f32vec3 rgbM = texture(daxa_sampler2D(push.image, push.image_sampler), in_uv).rgb;
	f32vec3 rgbNW = textureOffset(daxa_sampler2D(push.image, push.image_sampler), in_uv, ivec2(-1, 1)).rgb;
    f32vec3 rgbNE = textureOffset(daxa_sampler2D(push.image, push.image_sampler), in_uv, ivec2(1, 1)).rgb;
    f32vec3 rgbSW = textureOffset(daxa_sampler2D(push.image, push.image_sampler), in_uv, ivec2(-1, -1)).rgb;
    f32vec3 rgbSE = textureOffset(daxa_sampler2D(push.image, push.image_sampler), in_uv, ivec2(1, -1)).rgb;

	const f32vec3 toLuma = f32vec3(0.299, 0.587, 0.114);

	f32 lumaNW = dot(rgbNW, toLuma);
	f32 lumaNE = dot(rgbNE, toLuma);
	f32 lumaSW = dot(rgbSW, toLuma);
	f32 lumaSE = dot(rgbSE, toLuma);
	f32 lumaM = dot(rgbM, toLuma);

	f32 lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	f32 lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
	
	if (lumaMax - lumaMin <= lumaMax * u_lumaThreshold) {
		out_color = f32vec4(rgbM, 1.0);
		return;
	}  
	
	f32vec2 samplingDirection;	
	samplingDirection.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    samplingDirection.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    f32 samplingDirectionReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * u_mulReduce, u_minReduce);

	f32 minSamplingDirectionFactor = 1.0 / (min(abs(samplingDirection.x), abs(samplingDirection.y)) + samplingDirectionReduce);

    samplingDirection = clamp(samplingDirection * minSamplingDirectionFactor, f32vec2(-u_maxSpan), f32vec2(u_maxSpan)) * u_texelStep;

	f32vec3 rgbSampleNeg = texture(daxa_sampler2D(push.image, push.image_sampler), in_uv + samplingDirection * (1.0/3.0 - 0.5)).rgb;
	f32vec3 rgbSamplePos = texture(daxa_sampler2D(push.image, push.image_sampler), in_uv + samplingDirection * (2.0/3.0 - 0.5)).rgb;

	f32vec3 rgbTwoTab = (rgbSamplePos + rgbSampleNeg) * 0.5;  

	f32vec3 rgbSampleNegOuter = texture(daxa_sampler2D(push.image, push.image_sampler), in_uv + samplingDirection * (0.0/3.0 - 0.5)).rgb;
	f32vec3 rgbSamplePosOuter = texture(daxa_sampler2D(push.image, push.image_sampler), in_uv + samplingDirection * (3.0/3.0 - 0.5)).rgb;
	
	f32vec3 rgbFourTab = (rgbSamplePosOuter + rgbSampleNegOuter) * 0.25 + rgbTwoTab * 0.5;   

	f32 lumaFourTab = dot(rgbFourTab, toLuma);

	if (lumaFourTab < lumaMin || lumaFourTab > lumaMax) {
		out_color = f32vec4(rgbTwoTab, 1.0); 
	}
	else {
		out_color = f32vec4(rgbFourTab, 1.0);
	}
#endif
}

#endif