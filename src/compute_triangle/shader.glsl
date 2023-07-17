#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(ComputeDraw, push)

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main() {
    u32vec3 pixel = gl_GlobalInvocationID.xyz;
    if (pixel.x >= push.frame_dim.x || pixel.y >= push.frame_dim.y) { return; }

    f32vec2 uv = f32vec2(pixel.xy) / f32vec2(push.frame_dim.xy);
    uv = (uv - 0.5) * 2;

    f32vec2 points[3] = {
        f32vec2(-0.5, +0.5),
        f32vec2(+0.5, +0.5),
        f32vec2(+0.0, -0.5),
    };

    f32vec3 point_colors[3] = {
        f32vec3(0, 0, 1),
        f32vec3(0, 1, 0),
        f32vec3(1, 0, 0),
    };

    f32vec2 points_del[3] = {
        points[1] - points[0],
        points[2] - points[1],
        points[0] - points[2],
    };

    f32 slopes[3] = {
        points_del[0].y / points_del[0].x,
        points_del[1].y / points_del[1].x,
        points_del[2].y / points_del[2].x,
    };

    f32vec3 color = f32vec3(0.2, 0.4, 1.0);
    if (slopes[0] * (uv.x - points[0].x) > (uv.y - points[0].y) && slopes[1] * (uv.x - points[1].x) < (uv.y - points[1].y) && slopes[2] * (uv.x - points[2].x) < (uv.y - points[2].y)) {
        f32 p0 = clamp(dot(points_del[0], uv - points[0]) / dot(points_del[0], points_del[0]), 0, 1);
        f32 p1 = clamp(dot(points_del[1], uv - points[1]) / dot(points_del[1], points_del[1]), 0, 1);
        f32 p2 = clamp(dot(points_del[2], uv - points[2]) / dot(points_del[2], points_del[2]), 0, 1);

        color = mix(color, point_colors[0], f32vec3(1));
        color = mix(color, point_colors[1], p0);
        color = mix(color, point_colors[2], clamp((p1 - p2 + 0.5) / 1.5, 0.0, 1.0));
    }

    imageStore(daxa_image2D(push.image), i32vec2(pixel.xy), f32vec4(color, 1.0));
}