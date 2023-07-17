#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(ComputeDraw, push)

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void main() {
    vec2 uv = vec2(gl_GlobalInvocationID.xy) / vec2(gl_NumWorkGroups.xy);

    imageStore(daxa_image2D(push.image), i32vec2(gl_GlobalInvocationID.xy),f32vec4(uv, 0.0, 1.0));
}