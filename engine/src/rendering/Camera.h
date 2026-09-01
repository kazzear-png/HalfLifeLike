#pragma once
//
// FPS-style fly camera (M2): yaw/pitch orientation + perspective projection.
// Yaw 0 looks down -Z; positive yaw turns right (toward +X). Pitch + looks up,
// clamped to +/-~89.95 deg so lookAt() never degenerates (forward || up).
// Euler angles suffice for direct control; a Quat lands when smooth
// orientation interpolation is actually needed (docs/ARCHITECTURE.md).

#include "math/Mat4.h"
#include "math/Vec3.h"

namespace engine {

class Camera {
public:
    Camera() = default;

    // --- projection ---
    void setPerspective(float fovYRadians, float aspect, float nearZ, float farZ);
    const Mat4& projection() const { return m_projection; }

    // --- transform ---
    const Vec3& position() const { return m_position; }
    void setPosition(const Vec3& p) { m_position = p; }

    float yaw() const   { return m_yaw; }
    float pitch() const { return m_pitch; }
    void setYaw(float radians) { m_yaw = radians; }
    void setPitch(float radians);  // clamped
    void addYaw(float radians)   { m_yaw += radians; }
    void addPitch(float radians) { setPitch(m_pitch + radians); }

    // Unit basis vectors derived from yaw/pitch.
    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const { return cross(right(), forward()); }

    Mat4 view() const;                 // world -> camera space
    Mat4 viewProjection() const { return m_projection * view(); }

    static constexpr float kMaxPitch = 1.5703f;  // ~89.95 deg

private:
    Vec3 m_position{0.0f, 0.0f, 0.0f};
    float m_yaw   = 0.0f;
    float m_pitch = 0.0f;
    Mat4 m_projection;  // identity until setPerspective()
};

} // namespace engine