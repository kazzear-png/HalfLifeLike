#include "math/Mat4.h"

#include <cmath>

namespace engine {

Mat4 Mat4::identity() {
    return Mat4{};
}

Mat4 Mat4::perspective(float fovYRadians, float aspect, float nearZ, float farZ) {
    Mat4 out;
    for (float& v : out.m) v = 0.0f;

    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    out.m[0]  = f / aspect;
    out.m[5]  = f;
    out.m[10] = (farZ + nearZ) / (nearZ - farZ);
    out.m[11] = -1.0f;
    out.m[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
    return out;
}

Mat4 Mat4::lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 fwd   = normalize(target - eye);
    const Vec3 right = normalize(cross(fwd, up));
    const Vec3 upv   = cross(right, fwd);

    Mat4 out;  // identity
    out.m[0]  = right.x;  out.m[4]  = right.y;  out.m[8]  = right.z;
    out.m[1]  = upv.x;    out.m[5]  = upv.y;    out.m[9]  = upv.z;
    out.m[2]  = -fwd.x;   out.m[6]  = -fwd.y;   out.m[10] = -fwd.z;
    out.m[12] = -dot(right, eye);
    out.m[13] = -dot(upv, eye);
    out.m[14] =  dot(fwd, eye);
    return out;
}

Mat4 Mat4::translate(const Vec3& t) {
    Mat4 out;  // identity
    out.m[12] = t.x;
    out.m[13] = t.y;
    out.m[14] = t.z;
    return out;
}

Mat4 Mat4::scale(float s) {
    Mat4 out;  // identity
    out.m[0] = s;
    out.m[5] = s;
    out.m[10] = s;
    return out;
}

Mat4 Mat4::rotateX(float radians) {
    Mat4 out;  // identity
    const float c = std::cos(radians), s = std::sin(radians);
    out.m[5]  = c;  out.m[9]  = -s;
    out.m[6]  = s;  out.m[10] =  c;
    return out;
}

Mat4 Mat4::rotateY(float radians) {
    Mat4 out;  // identity
    const float c = std::cos(radians), s = std::sin(radians);
    out.m[0]  = c;  out.m[8]  = s;
    out.m[2]  = -s; out.m[10] = c;
    return out;
}

Mat4 Mat4::rotateZ(float radians) {
    Mat4 out;  // identity
    const float c = std::cos(radians), s = std::sin(radians);
    out.m[0] = c;  out.m[4] = -s;
    out.m[1] = s;  out.m[5] =  c;
    return out;
}

Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 out;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

} // namespace engine