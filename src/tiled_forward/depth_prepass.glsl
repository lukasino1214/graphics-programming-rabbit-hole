#include "shared.inl"

DAXA_DECL_PUSH_CONSTANT(DepthPrepassPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;

void main() {
    gl_Position = deref(push.camera_info).projection_matrix * deref(push.camera_info).view_matrix * deref(push.object_info).model_matrix * vec4(deref(push.vertices[gl_VertexIndex]).position, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

void main() {}

#endif