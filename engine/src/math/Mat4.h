#pragma once

#include "math/Vec3.h"

namespace engine {

// Column-major 4x4 matrix matching OpenGL's uniform layout: m[col * 4 + row].
struct Mat4 {
    float m[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    const float* data() const { return m; }

    static Mat4 identity();
    static Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ);
    static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up);
    static Mat4 translate(const Vec3& t);
    static Mat4 scale(float s);
    static Mat4 scale(const Vec3& s);            // non-uniform (M3)
    static Mat4 rotateX(float radians);
    static Mat4 rotateY(float radians);
    static Mat4 rotateZ(float radians);

    Mat4 transpose() const;                       // M3
    Mat4 inverse() const;                         // M3: general 4x4 inverse
    // M3: normal matrix for shading -- inverse-transpose of the model matrix.
    // Upload as a mat4, slice with mat3() in the shader.
    Mat4 normalMatrix() const { return inverse().transpose(); }
};

// Column-major multiply: out = a * b.
Mat4 operator*(const Mat4& a, const Mat4& b);

} // namespace engine