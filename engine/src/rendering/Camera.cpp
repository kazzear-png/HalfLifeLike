#include "rendering/Camera.h"

#include <cmath>

namespace engine {

void Camera::setPerspective(float fovYRadians, float aspect, float nearZ, float farZ) {
    m_projection = Mat4::perspective(fovYRadians, aspect, nearZ, farZ);
}

void Camera::setPitch(float radians) {
    if (radians >  kMaxPitch) radians =  kMaxPitch;
    if (radians < -kMaxPitch) radians = -kMaxPitch;
    m_pitch = radians;
}

Vec3 Camera::forward() const {
    const float cy = std::cos(m_yaw),   sy = std::sin(m_yaw);
    const float cp = std::cos(m_pitch), sp = std::sin(m_pitch);
    return Vec3(sy * cp, sp, -cy * cp);
}

Vec3 Camera::right() const {
    const float cy = std::cos(m_yaw), sy = std::sin(m_yaw);
    return Vec3(cy, 0.0f, sy);
}

Mat4 Camera::view() const {
    const Vec3 worldUp(0.0f, 1.0f, 0.0f);
    return Mat4::lookAt(m_position, m_position + forward(), worldUp);
}

} // namespace engine