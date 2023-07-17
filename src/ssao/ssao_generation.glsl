#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(SSAOGenerationPush, push)

#define KERNEL_SIZE 26

const f32vec3 kernelSamples[KERNEL_SIZE] = { // HIGH (26 samples)
    f32vec3(0.2196607,0.9032637,0.2254677),
    f32vec3(0.05916681,0.2201506,0.1430302),
    f32vec3(-0.4152246,0.1320857,0.7036734),
    f32vec3(-0.3790807,0.1454145,0.100605),
    f32vec3(0.3149606,-0.1294581,0.7044517),
    f32vec3(-0.1108412,0.2162839,0.1336278),
    f32vec3(0.658012,-0.4395972,0.2919373),
    f32vec3(0.5377914,0.3112189,0.426864),
    f32vec3(-0.2752537,0.07625949,0.1273409),
    f32vec3(-0.1915639,-0.4973421,0.3129629),
    f32vec3(-0.2634767,0.5277923,0.1107446),
    f32vec3(0.8242752,0.02434147,0.06049098),
    f32vec3(0.06262707,-0.2128643,0.03671562),
    f32vec3(-0.1795662,-0.3543862,0.07924347),
    f32vec3(0.06039629,0.24629,0.4501176),
    f32vec3(-0.7786345,-0.3814852,0.2391262),
    f32vec3(0.2792919,0.2487278,0.05185341),
    f32vec3(0.1841383,0.1696993,0.8936281),
    f32vec3(-0.3479781,0.4725766,0.719685),
    f32vec3(-0.1365018,-0.2513416,0.470937),
    f32vec3(0.1280388,-0.563242,0.3419276),
    f32vec3(-0.4800232,-0.1899473,0.2398808),
    f32vec3(0.6389147,0.1191014,0.5271206),
    f32vec3(0.1932822,-0.3692099,0.6060588),
    f32vec3(-0.3465451,-0.1654651,0.6746758),
    f32vec3(0.2448421,-0.1610962,0.1289366)
};

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out vec2 out_uv;

void main() {
    out_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(out_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

f32vec3 get_world_position_from_depth(f32vec2 uv, f32 depth) {
    f32vec4 clipSpacePosition = f32vec4(uv * 2.0 - 1.0, depth, 1.0);
    f32vec4 viewSpacePosition = deref(push.camera_info).inverse_projection_matrix * clipSpacePosition;

    viewSpacePosition /= viewSpacePosition.w;
    f32vec4 worldSpacePosition = deref(push.camera_info).inverse_view_matrix * viewSpacePosition;

    return worldSpacePosition.xyz;
}

f32vec3 get_view_position_from_depth(f32vec2 uv, f32 depth) {
    f32vec4 clipSpacePosition = f32vec4(uv * 2.0 - 1.0, depth, 1.0);
    f32vec4 viewSpacePosition = deref(push.camera_info).inverse_projection_matrix * clipSpacePosition;

    viewSpacePosition /= viewSpacePosition.w;

    return viewSpacePosition.xyz;
}

f32 rand(f32vec2 c) {
	return fract(sin(dot(c.xy, f32vec2(12.9898, 78.233))) * 43758.5453);
}

f32 noise(f32vec2 p, f32 freq) {
	f32 unit = 2560 / freq;
	f32vec2 ij = floor(p / unit);
	f32vec2 xy = mod(p, unit) / unit;
	xy = .5 * (1. - cos(3.14159265359 * xy));
	f32 a = rand((ij + f32vec2(0., 0.)));
	f32 b = rand((ij + f32vec2(1., 0.)));
	f32 c = rand((ij + f32vec2(0., 1.)));
	f32 d = rand((ij + f32vec2(1., 1.)));
	f32 x1 = mix(a, b, xy.x);
	f32 x2 = mix(c, d, xy.x);
	return mix(x1, x2, xy.y);
}

f32 pNoise(f32vec2 p, i32 res) {
	f32 persistance = .5;
	f32 n = 0.;
	f32 normK = 0.;
	f32 f = 4.;
	f32 amp = 1.;
	i32 iCount = 0;
	for (i32 i = 0; i < 50; i++) {
		n += amp * noise(p, f);
		f *= 2.;
		normK += amp;
		amp *= persistance;
		if (iCount == res) break;
		iCount++;
	}
	f32 nf = n / normK;
	return nf * nf * nf * nf;
}

layout(location = 0) in f32vec2 in_uv;

layout (location = 0) out f32 out_ssao;

void main() {
  
    f32vec3 frag_position = get_view_position_from_depth(in_uv, texture(daxa_sampler2D(push.depth, push.sampler_id), in_uv).r);
	f32vec3 normal = mat3x3(deref(push.camera_info).view_matrix) *  normalize(texture(daxa_sampler2D(push.normal, push.sampler_id), in_uv).rgb);

	ivec2 tex_dim = textureSize(daxa_sampler2D(push.normal, push.sampler_id), 0); 
#if USE_NOISE_TEXTURE == 0
	ivec2 noise_dim = textureSize(daxa_sampler2D(push.normal, push.sampler_id), 0);
	const f32vec2 noise_uv = f32vec2(f32(tex_dim.x)/f32(noise_dim.x), f32(tex_dim.y)/(noise_dim.y)) * in_uv;  

    f32vec3 random_vec = normalize(f32vec3(
        noise(in_uv, noise_dim.x * 2),
        noise(pow(in_uv, f32vec2(1.1)), pow(noise_dim.x * 4.2, 1.5 + in_uv.x / 10.0)),
        0.0
    ));
#else
	const float texel_size_noise = 1.0 / 128.0;
	float noise_primary = sample_texture(push.noise_texture, (in_uv * f32vec2(tex_dim)) * texel_size_noise).r;
	float noise_secondary = sample_texture(push.noise_texture, (in_uv * f32vec2(tex_dim)) * texel_size_noise - 3.25124895912).r;
	f32vec3 random_vec = normalize(f32vec3(
        in_uv + vec2(noise_primary, noise_secondary),
        0.0
    ));
#endif
	f32vec3 tangent = normalize(random_vec - normal * dot(random_vec, normal));
	f32vec3 bitangent = cross(tangent, normal);
	mat3 TBN = mat3(tangent, bitangent, normal);

	f32 occlusion = 0.0f;
	for(i32 i = 0; i < push.kernel_size; i++) {		
		f32vec3 sample_pos = TBN * kernelSamples[i].xyz; 
		sample_pos = frag_position + sample_pos * push.radius; 
		
		// project
		f32vec4 offset = f32vec4(sample_pos, 1.0f);
		offset = deref(push.camera_info).projection_matrix * offset; 
		offset.xy /= offset.w; 
		offset.xy = offset.xy * 0.5f + 0.5f; 
		
		f32vec3 sample_depth_v = get_view_position_from_depth(offset.xy, texture(daxa_sampler2D(push.depth, push.sampler_id), offset.xy).r);
        f32 sample_depth = sample_depth_v.z;

		f32 range_check = smoothstep(0.0f, 1.0f, push.radius / abs(frag_position.z - sample_depth));
		occlusion += (sample_depth >= sample_pos.z + push.bias ? 1.0f : 0.0f) * range_check;           
	}
	occlusion = 1.0 - (occlusion / f32(KERNEL_SIZE));
	
	out_ssao = occlusion;
}

#endif