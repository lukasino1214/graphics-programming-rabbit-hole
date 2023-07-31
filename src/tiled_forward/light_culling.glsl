// #include "shared.inl"
// 
// DAXA_DECL_PUSH_CONSTANT(CullingPush, push)
// 
// #define MAX_POINT_LIGHT_PER_TILE NUM_LIGHTS - 1
// 
// const f32vec2 ndc_upper_left = f32vec2(-1.0, -1.0);
// const f32 ndc_near_plane = 0.0;
// const f32 ndc_far_plane = 1.0;
// 
// struct ViewFrustum
// {
// 	f32vec4 planes[6];
// 	f32vec3 points[8]; // 0-3 near 4-7 far
// };
// 
// layout(local_size_x = 32) in;
// 
// shared ViewFrustum frustum;
// shared uint light_count_for_tile;
// shared f32 min_depth;
// shared f32 max_depth;
// 
// // Construct view frustum
// ViewFrustum createFrustum(ivec2 tile_id)
// {
// 
// 	//mat4 inv_projview = inverse(camera.projview);
//     mat4 inv_projview = deref(push.camera_info).inverse_projection_matrix * deref(push.camera_info).inverse_view_matrix;
// 
// 	f32vec2 ndc_size_per_tile = 2.0 * f32vec2(TILE_SIZE, TILE_SIZE) / push.viewport_size;
// 
// 	f32vec2 ndc_pts[4];  // corners of tile in ndc
// 	ndc_pts[0] = ndc_upper_left + tile_id * ndc_size_per_tile;  // upper left
// 	ndc_pts[1] = f32vec2(ndc_pts[0].x + ndc_size_per_tile.x, ndc_pts[0].y); // upper right
// 	ndc_pts[2] = ndc_pts[0] + ndc_size_per_tile;
// 	ndc_pts[3] = f32vec2(ndc_pts[0].x, ndc_pts[0].y + ndc_size_per_tile.y); // lower left
// 
// 	ViewFrustum frustum;
// 
// 	f32vec4 temp;
// 	for (i32 i = 0; i < 4; i++)
// 	{
// 		temp = inv_projview * f32vec4(ndc_pts[i], min_depth, 1.0);
// 		frustum.points[i] = temp.xyz / temp.w;
// 		temp = inv_projview * f32vec4(ndc_pts[i], max_depth, 1.0);
// 		frustum.points[i + 4] = temp.xyz / temp.w;
// 	}
// 
//     f32vec3 camera_position = deref(push.camera_info).position;
// 
// 	f32vec3 temp_normal;
// 	for (i32 i = 0; i < 4; i++)
// 	{
// 		//Cax+Cby+Ccz+Cd = 0, planes[i] = (Ca, Cb, Cc, Cd)
// 		// temp_normal: normal without normalization
// 		temp_normal = cross(frustum.points[i] - camera_position, frustum.points[i + 1] - camera_position);
// 		temp_normal = normalize(temp_normal);
// 		frustum.planes[i] = f32vec4(temp_normal, - dot(temp_normal, frustum.points[i]));
// 	}
// 	// near plane
// 	{
// 		temp_normal = cross(frustum.points[1] - frustum.points[0], frustum.points[3] - frustum.points[0]);
// 		temp_normal = normalize(temp_normal);
// 		frustum.planes[4] = f32vec4(temp_normal, - dot(temp_normal, frustum.points[0]));
// 	}
// 	// far plane
// 	{
// 		temp_normal = cross(frustum.points[7] - frustum.points[4], frustum.points[5] - frustum.points[4]);
// 		temp_normal = normalize(temp_normal);
// 		frustum.planes[5] = f32vec4(temp_normal, - dot(temp_normal, frustum.points[4]));
// 	}
// 
// 	return frustum;
// }
// 
// bool isCollided(PointLight light, ViewFrustum frustum)
// {
// 	bool result = true;
// 
//     // Step1: sphere-plane test
// 	for (i32 i = 0; i < 6; i++)
// 	{
// 		if (dot(light.position, frustum.planes[i].xyz) + frustum.planes[i].w  < - light.radius )
// 		{
// 			result = false;
// 			break;
// 		}
// 	}
// 
//     if (!result)
//     {
//         return false;
//     }
// 
//     // Step2: bbox corner test (to reduce false positive)
//     f32vec3 light_bbox_max = light.position + f32vec3(light.radius);
//     f32vec3 light_bbox_min = light.position - f32vec3(light.radius);
//     i32 probe;
//     probe=0; for( i32 i=0; i<8; i++ ) probe += ((frustum.points[i].x > light_bbox_max.x)?1:0); if( probe==8 ) return false;
//     probe=0; for( i32 i=0; i<8; i++ ) probe += ((frustum.points[i].x < light_bbox_min.x)?1:0); if( probe==8 ) return false;
//     probe=0; for( i32 i=0; i<8; i++ ) probe += ((frustum.points[i].y > light_bbox_max.y)?1:0); if( probe==8 ) return false;
//     probe=0; for( i32 i=0; i<8; i++ ) probe += ((frustum.points[i].y < light_bbox_min.y)?1:0); if( probe==8 ) return false;
//     probe=0; for( i32 i=0; i<8; i++ ) probe += ((frustum.points[i].z > light_bbox_max.z)?1:0); if( probe==8 ) return false;
//     probe=0; for( i32 i=0; i<8; i++ ) probe += ((frustum.points[i].z < light_bbox_min.z)?1:0); if( probe==8 ) return false;
// 
// 	return true;
// }
// 
// void main()
// {
// 	ivec2 tile_id = ivec2(gl_WorkGroupID.xy);
// 	uint tile_index = tile_id.y * push.tile_nums.x + tile_id.x;
// 
// 	// TODO: depth culling???
// 
// 	if (gl_LocalInvocationIndex == 0)
// 	{
// 		min_depth = 1.0;
// 		max_depth = 0.0;
// 
// 		for (i32 y = 0; y < TILE_SIZE; y++)
// 		{
// 			for (i32 x = 0; x < TILE_SIZE; x++)
// 			{
// 				f32vec2 sample_loc = (f32vec2(TILE_SIZE, TILE_SIZE) * tile_id + f32vec2(x, y) ) / push.viewport_size;
// 				f32 pre_depth = texture(daxa_sampler2D(push.depth_image, push.depth_sampler), sample_loc).x;
// 				min_depth = min(min_depth, pre_depth);
// 				max_depth = max(max_depth, pre_depth); //TODO: parallize this
// 			}
// 		}
// 
// 		if (min_depth >= max_depth)
// 		{
// 			min_depth = max_depth;
// 		}
// 
// 		frustum = createFrustum(tile_id);
// 		light_count_for_tile = 0;
// 	}
// 
// 	barrier();
// 
// 	for (uint i = gl_LocalInvocationIndex; i < NUM_LIGHTS && light_count_for_tile < MAX_POINT_LIGHT_PER_TILE; i += gl_WorkGroupSize.x)
// 	{
// 		if (isCollided(deref(push.point_light_buffer[i]), frustum))
// 		{
// 			uint slot = atomicAdd(light_count_for_tile, 1);
// 			if (slot >= MAX_POINT_LIGHT_PER_TILE) {break;}
// 			//light_visiblities[tile_index].lightindices[slot] = i;
//             deref(push.visible_point_light_indices[tile_index*NUM_LIGHTS+slot]).index = i;
// 		}
// 	}
// 
// 	barrier();
// 
// 	if (gl_LocalInvocationIndex == 0) {
//         if (light_count_for_tile != NUM_LIGHTS) {
// 			deref(push.visible_point_light_indices[tile_index*NUM_LIGHTS + light_count_for_tile]).index = -1;
// 		}
// 	}
// }



#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(CullingPush, push)

#define MAX_POINT_LIGHT_PER_TILE NUM_LIGHTS - 1

struct Plane {
	f32vec3 normal;
	f32 dis;
};

Plane compute_plane(f32vec3 p0, f32vec3 p1, f32vec3 p2) {
	Plane plane;

	f32vec3 v0 = p1 - p0;
    f32vec3 v2 = p2 - p0;

	plane.normal = normalize(cross(v0, v2));
	plane.dis = dot(plane.normal, p0);

	return plane;
}

struct Frustum {
	Plane planes[4];
};

f32vec4 clip_to_view(f32vec4 clip) {
    f32vec4 view = deref(push.camera_info).inverse_projection_matrix * clip;
    view = view / view.w;

    return view;
}

f32vec4 screen_to_view(f32vec4 screen) {
    f32vec2 uv = screen.xy / f32vec2(push.viewport_size);
    f32vec4 clip = f32vec4(f32vec2(uv.x, 1.0 - uv.y) * 2.0 - 1.0, screen.z, screen.w);

    return clip_to_view(clip);
}

struct Sphere {
	f32vec3 center;
	f32 radius;
};

bool sphere_inside_plane(Sphere sphere, Plane plane) {
	return dot(plane.normal, sphere.center) - plane.dis < -sphere.radius;
}

bool sphere_inside_frustum(Sphere sphere, Frustum frustum, f32 z_near, f32 z_far) {
	bool result = true;

	if (sphere.center.z - sphere.radius > z_near || sphere.center.z + sphere.radius < z_far) {
        result = false;
    }

	for (i32 i = 0; i < 4 && result; ++i) {
		if (sphere_inside_plane(sphere, frustum.planes[i])) {
			result = false;
		}
	}

	return result;
}

shared uint light_count_for_tile;
shared uint min_depth_uint;
shared uint max_depth_uint;
shared Frustum frustum;
shared uint z_near_uint;
shared uint z_far_uint;

layout(local_size_x = TILE_SIZE, local_size_y = TILE_SIZE, local_size_z = 1) in;

void main() {
	// get id of the tile
	ivec2 tile_id = ivec2(gl_WorkGroupID.xy);
	uint tile_index = tile_id.y * push.tile_nums.x + tile_id.x;

	// set shared variables
	if(gl_LocalInvocationIndex == 0) {
		min_depth_uint = 0xFFFFFFFF;
		max_depth_uint = 0;
		light_count_for_tile = 0;
		z_near_uint = 0xFFFFFFFF;
		z_far_uint = 0;
	}

	// barrier();

	// // get the min or max depth for the tile
	// f32vec2 uv = f32vec2(gl_GlobalInvocationID.xy) / f32vec2(push.viewport_size);
	// f32 depth = texture(daxa_sampler2D(push.depth_image, push.depth_sampler), uv).r;
	// uint depth_uint = floatBitsToUint(depth);

	// atomicMin(min_depth_uint, depth_uint);
	// atomicMax(max_depth_uint, depth_uint);

	// f32vec4 screen_depth = f32vec4(uv, depth, 1.0);
	// f32vec4 view_depth = screen_to_view(screen_depth);
	// uint view_depth_uint = floatBitsToUint(view_depth.z);

	// atomicMin(z_near_uint, view_depth_uint);
	// atomicMax(z_far_uint, view_depth_uint);

	// barrier();

	// if(gl_LocalInvocationIndex == 0) {
	// 	f32vec3 eye_position = f32vec3(0.0);
	// 	f32vec4 screen_space[4];
	// 	screen_space[0] = f32vec4(gl_GlobalInvocationID.xy * TILE_SIZE, -1.0, 1.0);
	// 	screen_space[1] = f32vec4(f32vec2(gl_GlobalInvocationID.x + 1, gl_GlobalInvocationID.y) * TILE_SIZE, -1.0, 1.0);
	// 	screen_space[2] = f32vec4(f32vec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y + 1) * TILE_SIZE, -1.0, 1.0);
	// 	screen_space[3] = f32vec4(f32vec2(gl_GlobalInvocationID.x + 1, gl_GlobalInvocationID.y + 1) * TILE_SIZE, -1.0, 1.0);

	// 	f32vec3 view_space[4];
	// 	for ( i32 i = 0; i < 4; i++ ) {
	// 		view_space[i] = screen_to_view(screen_space[i]).xyz;
	// 	}


	// 	frustum.planes[0] = compute_plane(eye_position, view_space[2], view_space[0]);
	// 	frustum.planes[1] = compute_plane(eye_position, view_space[1], view_space[3]);
	// 	frustum.planes[2] = compute_plane(eye_position, view_space[0], view_space[1]);
	// 	frustum.planes[3] = compute_plane(eye_position, view_space[3], view_space[2]);
	// }

	// barrier();

	// f32 min_depth = uintBitsToFloat(min_depth_uint);
	// f32 max_depth = uintBitsToFloat(max_depth_uint);
	// f32 z_far = uintBitsToFloat(z_far_uint);
	// f32 z_near = uintBitsToFloat(z_near_uint);
	// f32 diff = z_near - z_far;
    // z_far -= diff;
    // z_near += diff;

	// for (uint i = gl_LocalInvocationIndex; i < NUM_LIGHTS && light_count_for_tile < MAX_POINT_LIGHT_PER_TILE; i += gl_WorkGroupSize.x) {
	// 	PointLight light = deref(push.point_light_buffer[i]);
	// 	Sphere sphere;
	// 	sphere.center = (deref(push.camera_info).view_matrix * f32vec4(light.position, 1.0)).xyz;
	// 	sphere.radius = light.radius;
		
	// 	if (sphere_inside_frustum(sphere, frustum, z_near, z_far)) {
	// 		uint slot = atomicAdd(light_count_for_tile, 1);
	// 		if (slot >= MAX_POINT_LIGHT_PER_TILE) {break;}
    //         deref(push.visible_point_light_indices[tile_index * NUM_LIGHTS + slot]).index = i32(i);
	// 	}
	// }

	// barrier();

	// if (gl_LocalInvocationIndex == 0) {
    //     if (light_count_for_tile != NUM_LIGHTS) {
	// 		deref(push.visible_point_light_indices[tile_index * NUM_LIGHTS + light_count_for_tile]).index = -1;
	// 	}
	// }

	barrier();

	if (gl_LocalInvocationIndex == 0) {
        for(i32 i = 0; i < NUM_LIGHTS; i++) {
			uint slot = atomicAdd(light_count_for_tile, 1);
			deref(push.visible_point_light_indices[tile_index*NUM_LIGHTS + i]).index = i;
		}
		if(light_count_for_tile != NUM_LIGHTS) {
			deref(push.visible_point_light_indices[tile_index*NUM_LIGHTS + light_count_for_tile]).index = -1;
		}
	}
}