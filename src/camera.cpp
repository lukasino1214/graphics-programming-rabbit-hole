#include "camera.hpp"
#include <numbers>
#include <glm/gtx/rotate_vector.hpp>
#include <iostream>

void Camera3D::resize(i32 size_x, i32 size_y) {
    aspect = static_cast<f32>(size_x) / static_cast<f32>(size_y);
    proj_mat = glm::perspective(glm::radians(fov), aspect, near_clip, far_clip);
    proj_mat[1][1] *= -1.0f;
}

auto Camera3D::get_vp() -> glm::mat4 {
    return proj_mat * view_mat;
}

auto Camera3D::get_view() -> glm::mat4 {
    return view_mat;
}

void ControlledCamera3D::update(f32 dt) {
    constexpr auto MAX_ROT = 1.56825555556f;
    if (rotation.y > MAX_ROT) { rotation.y = MAX_ROT; }
    if (rotation.y < -MAX_ROT) { rotation.y = -MAX_ROT; }

    glm::vec3 forward_direction = glm::normalize(glm::vec3{ cos(rotation.x) * cos(rotation.y), -sin(rotation.y), sin(rotation.x) * cos(rotation.y) });
    glm::vec3 up_direction = glm::vec3{ 0.0f, 1.0f, 0.0f };
    glm::vec3 right_direction = glm::normalize(glm::cross(forward_direction, up_direction));

    glm::vec3 move_direction = glm::vec3{ 0.0f };
    if (move.pz) { move_direction += forward_direction; }
    if (move.nz) { move_direction -= forward_direction; }
    if (move.nx) { move_direction += right_direction; }
    if (move.px) { move_direction -= right_direction; }
    if (move.py) { move_direction += up_direction; }
    if (move.ny) { move_direction -= up_direction; }

    if (glm::dot(move_direction, move_direction) > std::numeric_limits<float>::epsilon()) {
        delta_position += glm::normalize(move_direction) * dt * acceleration;
    }
    if (glm::dot(delta_position, delta_position) > std::numeric_limits<float>::epsilon()) {
        delta_position = glm::sign(delta_position) * glm::clamp(glm::abs(delta_position) - dt * glm::abs(glm::normalize(delta_position)) * drag, glm::vec3(0.0), glm::abs(glm::normalize(delta_position)));
    }

    position += (move.sprint ? sprint_speed : 2.0f) * delta_position * 0.1f;
    camera.view_mat = glm::lookAt(position, position + forward_direction, glm::vec3{0.0f, 1.0f, 0.0f});
}

void ControlledCamera3D::on_key(i32 key, i32 action) {
    if (key == keybinds.move_pz)
        move.pz = action != 0;
    if (key == keybinds.move_nz)
        move.nz = action != 0;
    if (key == keybinds.move_px)
        move.px = action != 0;
    if (key == keybinds.move_nx)
        move.nx = action != 0;
    if (key == keybinds.move_py)
        move.py = action != 0;
    if (key == keybinds.move_ny)
        move.ny = action != 0;
    if (key == keybinds.toggle_sprint)
        move.sprint = action != 0;
}

void ControlledCamera3D::on_mouse_move(f32 delta_x, f32 delta_y) {
    rotation.x += delta_x * mouse_sens * 0.0001f * camera.fov;
    rotation.y -= delta_y * mouse_sens * 0.0001f * camera.fov;
}