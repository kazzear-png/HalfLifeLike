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

Mat4 Mat4::scale(const Vec3& s) {
    Mat4 out;  // identity
    out.m[0]  = s.x;
    out.m[5]  = s.y;
    out.m[10] = s.z;
    return out;
}

Mat4 Mat4::transpose() const {
    Mat4 out;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out.m[col * 4 + row] = m[row * 4 + col];
        }
    }
    return out;
}

Mat4 Mat4::inverse() const {
    // General 4x4 inverse via cofactors (GluInvertMatrix, established form).
    const float* t = m;
    Mat4 out;
    float* o = out.m;

    const float a00 = t[0],  a01 = t[1],  a02 = t[2],  a03 = t[3];
    const float a10 = t[4],  a11 = t[5],  a12 = t[6],  a13 = t[7];
    const float a20 = t[8],  a21 = t[9],  a22 = t[10], a23 = t[11];
    const float a30 = t[12], a31 = t[13], a32 = t[14], a33 = t[15];

    const float b00 = a00 * a11 - a01 * a10;
    const float b01 = a00 * a12 - a02 * a10;
    const float b02 = a00 * a13 - a03 * a10;
    const float b03 = a01 * a12 - a02 * a11;
    const float b04 = a01 * a13 - a03 * a11;
    const float b05 = a02 * a13 - a03 * a12;
    const float b06 = a20 * a31 - a21 * a30;
    const float b07 = a20 * a32 - a22 * a30;
    const float b08 = a20 * a33 - a23 * a30;
    const float b09 = a21 * a32 - a22 * a31;
    const float b10 = a21 * a33 - a23 * a31;
    const float b11 = a22 * a33 - a23 * a32;

    float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (det == 0.0f) {
        return Mat4::identity();  // singular: return identity rather than NaNs
    }
    det = 1.0f / det;

    o[0]  = (a11 * b11 - a12 * b10 + a13 * b09) * det;
    o[1]  = (a02 * b10 - a01 * b11 - a03 * b09) * det;
    o[2]  = (a31 * b05 - a32 * b04 + a33 * b03) * det;
    o[3]  = (a22 * b04 - a21 * b05 - a23 * b03) * det;
    o[4]  = (a12 * b08 - a10 * b11 - a13 * b07) * det;
    o[5]  = (a00 * b11 - a02 * b08 + a03 * b07) * det;
    o[6]  = (a32 * b02 - a30 * b05 - a33 * b01) * det;
    o[7]  = (a20 * b05 - a22 * b02 + a23 * b01) * det;
    o[8]  = (a10 * b10 - a11 * b08 + a13 * b06) * det;
    o[9]  = (a01 * b08 - a00 * b10 - a03 * b06) * det;
    o[10] = (a30 * b04 - a31 * b02 + a33 * b00) * det;
    o[11] = (a21 * b02 - a20 * b04 - a23 * b00) * det;
    o[12] = (a11 * b07 - a10 * b09 - a12 * b06) * det;
    o[13] = (a00 * b09 - a01 * b07 + a02 * b06) * det;
    o[14] = (a31 * b01 - a30 * b03 - a32 * b00) * det;
    o[15] = (a20 * b03 - a21 * b01 + a22 * b00) * det;
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