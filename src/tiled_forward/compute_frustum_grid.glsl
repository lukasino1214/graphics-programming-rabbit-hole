#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(ComputeFrustumsPush, push)

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

Plane compute_plane(f32vec3 p0, f32vec3 p1, f32vec3 p2) {
    Plane plane;

    f32vec3 v0 = p1 - p0;
    f32vec3 v2 = p2 - p0;

    plane.normal = normalize(cross(v0, v2));
    plane.dist = dot(plane.normal, p0);

    return plane;
}

const vec2 ndc_upper_left = vec2(-1.0, -1.0);
const float ndc_near_plane = 0.0;
const float ndc_far_plane = 1.0;


void main() {
    //uint index = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
    //uint index = gl_GlobalInvocationID.y * gl_NumWorkGroups.x + gl_GlobalInvocationID.x;

    f32vec3 eye_position = f32vec3(0.0, 0.0, 0.0);

    f32vec4 screen_space[4];
    // Top left point
    screen_space[0] = f32vec4(gl_GlobalInvocationID.xy * TILE_SIZE, -1.0, 1.0);
    // Top right point
    screen_space[1] = f32vec4(f32vec2(gl_GlobalInvocationID.x + 1, gl_GlobalInvocationID.y) * TILE_SIZE, -1.0, 1.0);
    // Bottom left point
    screen_space[2] = f32vec4(f32vec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y + 1) * TILE_SIZE, -1.0, 1.0);
    // Bottom right point
    screen_space[3] = f32vec4(f32vec2(gl_GlobalInvocationID.x + 1, gl_GlobalInvocationID.y + 1) * TILE_SIZE, -1.0, 1.0);
    // Top left point

    f32vec3 view_space[4];
    for(i32 i = 0; i < 4; i++) {
        view_space[i] = screen_to_view(screen_space[i]).xyz;
    }

    Frustum frustum;
    // Left plane
    frustum.planes[0] = compute_plane(eye_position, view_space[2], view_space[0]);
    // Right plane
    frustum.planes[1] = compute_plane(eye_position, view_space[1], view_space[3]);
    // Top plane
    frustum.planes[2] = compute_plane(eye_position, view_space[0], view_space[1]);
    // Bottom plane
    frustum.planes[3] = compute_plane(eye_position, view_space[3], view_space[2]);

    if (gl_GlobalInvocationID.x < push.tile_nums.x && gl_GlobalInvocationID.y < push.tile_nums.y) {
        //deref(push.frustum_buffer[index]) = frustum;
        deref(push.frustum_buffer[gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * push.tile_nums.x]) = frustum;
    }
}