#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(ComputeLightListPush, push)

layout (local_size_x = TILE_SIZE, local_size_y = TILE_SIZE) in;

f32vec4 clip_to_view(f32vec4 clip) {
    f32vec4 view = deref(push.camera_info).inverse_projection_matrix * clip;
    view = view / view.w;
    
    return view;
}

f32vec4 screen_to_view(f32vec4 screen) {
    f32vec2 uv = screen.xy / f32vec2(push.viewport_size); 
    f32vec4 clip = f32vec4(f32vec2(uv.x, uv.y) * 2.0 - 1.0, screen.z, screen.w);

    return clip_to_view(clip);
}

struct Sphere {
    f32vec3 center;
    f32 radius;
};

bool sphere_inside_plane(Sphere sphere, Plane plane) {
    return dot(plane.normal, sphere.center) - plane.dist < -sphere.radius;
}

bool sphere_inside_frustum(Sphere sphere, Frustum frustum, f32 z_near, f32 z_far) {
    bool result = true;

    if (sphere.center.z - sphere.radius > z_near || sphere.center.z + sphere.radius < z_far) {
        result = false;
    }

    for (u32 i = 0; i < 4 && result; i++) {
        if (sphere_inside_plane(sphere, frustum.planes[i])) {
            result = false;
        }
    }

    return result;
}

shared uint min_depth_uint;
shared uint max_depth_uint;
shared Frustum frustum;

shared uint light_count;

void main() {
    uint index = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
    //uint index = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * push.tile_nums.x;

    f32 depth = texelFetch(daxa_sampler2D(push.depth_image, push.depth_sampler), ivec2(gl_GlobalInvocationID.xy), 0).r;
    uint depth_uint = floatBitsToUint(depth);

    if(gl_LocalInvocationIndex == 0) {
        min_depth_uint = 0x7F7FFFFF;
        max_depth_uint = 0;
        light_count = 0;
        frustum = deref(push.frustum_buffer[index]);
    }

    barrier();

    atomicMin(min_depth_uint, depth_uint);
	atomicMax(max_depth_uint, depth_uint);

    barrier();

    f32 min_depth_float = uintBitsToFloat(min_depth_uint);
    f32 max_depth_float = uintBitsToFloat(max_depth_uint);

    f32 min_depth_view = clip_to_view(f32vec4(0.0, 0.0, min_depth_float, 1.0)).z;
    f32 max_depth_view = clip_to_view(f32vec4(0.0, 0.0, max_depth_float, 1.0)).z;
    f32 near_clip_view = clip_to_view(f32vec4(0.0, 0.0, 0.0, 1.0)).z;

    Plane min_plane;
    min_plane.normal = f32vec3(0.0, 0.0, -1.0);
    min_plane.dist = -min_depth_view;

    for (uint i = gl_LocalInvocationIndex; i < NUM_LIGHTS && light_count < NUM_LIGHTS; i += TILE_SIZE * TILE_SIZE) {
        PointLight light = deref(push.point_light_buffer[i]);

        Sphere sphere;
        sphere.center = f32vec3(deref(push.camera_info).view_matrix * f32vec4(light.position, 1.0));
        sphere.radius = light.radius;

        //if(true) {
        if(sphere_inside_frustum(sphere, frustum, near_clip_view, max_depth_view) && !sphere_inside_plane(sphere, min_plane)) {
            uint light_index = atomicAdd(light_count, 1);
            if(light_index > NUM_LIGHTS) { break; }
            deref(push.point_light_index_buffer[index * NUM_LIGHTS + light_index]).index = i;
        }
    }

    barrier();

    deref(push.point_light_grid_buffer[index]).count = u32(light_count);
}