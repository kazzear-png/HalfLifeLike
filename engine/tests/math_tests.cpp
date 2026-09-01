//
// M2 math verification. Run: ctest --test-dir build --output-on-failure
// Harness is intentionally minimal (project rule: minimize dependencies);
// exit condition for doctest/Catch2 is in docs/ARCHITECTURE.md decisions.

#include "math/Mat4.h"
#include "math/Vec3.h"
#include "rendering/Camera.h"

#include <cmath>
#include <cstdio>

namespace {

int g_checks   = 0;
int g_failures = 0;

void expectTrue(bool condition, const char* what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL: %s\n", what);
    }
}

void expectNear(float a, float b, float eps, const char* what) {
    expectTrue(std::fabs(a - b) <= eps, what);
}

void expectVec3Near(const engine::Vec3& a, const engine::Vec3& b, float eps, const char* what) {
    expectNear(a.x, b.x, eps, what);
    expectNear(a.y, b.y, eps, what);
    expectNear(a.z, b.z, eps, what);
}

// Transforms a point (w=1) or direction (w=0) by a column-major Mat4.
engine::Vec3 transform(const engine::Mat4& m, float x, float y, float z, float w) {
    const float* a = m.data();
    return engine::Vec3(
        a[0]*x + a[4]*y + a[8]*z  + a[12]*w,
        a[1]*x + a[5]*y + a[9]*z  + a[13]*w,
        a[2]*x + a[6]*y + a[10]*z + a[14]*w);
}

bool matricesEqual(const engine::Mat4& a, const engine::Mat4& b, float eps) {
    for (int i = 0; i < 16; ++i) {
        if (std::fabs(a.data()[i] - b.data()[i]) > eps) return false;
    }
    return true;
}

constexpr float kHalfPi = 1.57079632f;

void testVec3Basics() {
    std::printf("[math] Vec3 basics\n");
    expectNear(engine::dot(engine::Vec3(1,2,3), engine::Vec3(4,5,6)), 32.0f, 1e-6f, "dot");
    expectVec3Near(engine::cross(engine::Vec3(1,0,0), engine::Vec3(0,1,0)),
                   engine::Vec3(0,0,1), 1e-6f, "cross x*y=z");
    expectNear(engine::length(engine::Vec3(3,0,4)), 5.0f, 1e-6f, "length 3-4-5");
    expectNear(engine::length(engine::normalize(engine::Vec3(3,1,-2))), 1.0f, 1e-6f,
               "normalize yields unit length");
}

void testIdentity() {
    std::printf("[math] identity laws\n");
    const engine::Mat4 I = engine::Mat4::identity();
    const engine::Mat4 A = engine::Mat4::translate(engine::Vec3(1,2,3)) * engine::Mat4::rotateY(0.7f);
    expectTrue(matricesEqual(I * A, A, 1e-6f), "I*A == A");
    expectTrue(matricesEqual(A * I, A, 1e-6f), "A*I == A");
}

void testTranslation() {
    std::printf("[math] translation\n");
    const engine::Mat4 T = engine::Mat4::translate(engine::Vec3(2.0f, -3.0f, 5.0f));
    expectVec3Near(transform(T, 0,0,0, 1.0f), engine::Vec3(2,-3,5), 1e-6f,
                   "translate maps origin to translation vector");
    expectVec3Near(transform(T, 1,0,0, 0.0f), engine::Vec3(1,0,0), 1e-6f,
                   "translation does not affect directions (w=0)");
    const engine::Mat4 Tab = engine::Mat4::translate(engine::Vec3(1,1,1)) * engine::Mat4::translate(engine::Vec3(2,3,4));
    expectVec3Near(transform(Tab, 0,0,0, 1.0f), engine::Vec3(3,4,5), 1e-6f,
                   "translate(a)*translate(b) == translate(a+b)");
}

void testRotation() {
    std::printf("[math] rotation\n");
    const float eps = 1e-5f;
    expectVec3Near(transform(engine::Mat4::rotateY(kHalfPi), 1,0,0, 0.0f),
                   engine::Vec3(0,0,-1), eps, "rotateY(90): +X -> -Z");
    expectVec3Near(transform(engine::Mat4::rotateX(kHalfPi), 0,1,0, 0.0f),
                   engine::Vec3(0,0,1), eps, "rotateX(90): +Y -> +Z");
    expectVec3Near(transform(engine::Mat4::rotateZ(kHalfPi), 1,0,0, 0.0f),
                   engine::Vec3(0,1,0), eps, "rotateZ(90): +X -> +Y");
    // Rotations preserve vector length.
    const engine::Vec3 v = transform(engine::Mat4::rotateY(0.37f) * engine::Mat4::rotateX(-1.2f),
                                     0.3f, -0.7f, 0.9f, 0.0f);
    expectNear(engine::length(v), std::sqrt(1.39f), eps, "rotation preserves length");
}

void testPerspective() {
    std::printf("[math] perspective\n");
    const float eps = 1e-4f;
    const engine::Mat4 P = engine::Mat4::perspective(kHalfPi /*90 deg*/, 1.0f, 0.1f, 100.0f);

    // 90 deg fov, aspect 1: f = 1/tan(45) = 1 -> m00 = m11 = 1.
    expectNear(P.data()[0], 1.0f, eps, "perspective m00 (fov90 aspect1)");
    expectNear(P.data()[5], 1.0f, eps, "perspective m11 (fov90 aspect1)");

    // Depth mapping, GL convention (right-handed): near -> NDC z = -1,
    // far -> NDC z = +1. Requires the perspective divide by clip.w.
    const float nearZ = 0.1f, farZ = 100.0f;
    auto ndcZ = [&](float worldZ) {
        const engine::Vec3 clip = transform(P, 0, 0, worldZ, 1.0f);
        const float w = P.data()[11] * worldZ + P.data()[15];
        return clip.z / w;
    };
    expectNear(ndcZ(-nearZ), -1.0f, eps, "near plane maps to NDC -1");
    expectNear(ndcZ(-farZ),   1.0f, eps, "far plane maps to NDC +1");
    expectTrue(ndcZ(-1.0f) > -1.0f && ndcZ(-1.0f) < 1.0f, "mid-depth inside NDC range");
}

void testLookAt() {
    std::printf("[math] lookAt\n");
    const float eps = 1e-5f;
    const engine::Mat4 V = engine::Mat4::lookAt(engine::Vec3(0,0,5), engine::Vec3(0,0,0), engine::Vec3(0,1,0));
    expectVec3Near(transform(V, 0,0,5, 1.0f), engine::Vec3(0,0,0), eps, "lookAt maps eye to origin");
    expectVec3Near(transform(V, 0,0,0, 1.0f), engine::Vec3(0,0,-5), eps, "lookAt maps target to -Z (in front)");
    expectVec3Near(transform(V, 1,0,5, 1.0f), engine::Vec3(1,0,0), eps, "world +X (camera right) -> view +X");
    expectVec3Near(transform(V, 0,0,-1, 0.0f), engine::Vec3(0,0,-1), eps, "view direction -> -Z (w=0)");
}

void testCamera() {
    std::printf("[math] camera\n");
    engine::Camera cam;
    cam.setPosition(engine::Vec3(0.0f, 0.0f, 5.0f));
    cam.setPerspective(0.7854f, 1.333f, 0.1f, 100.0f);

    expectVec3Near(cam.forward(), engine::Vec3(0,0,-1), 1e-6f, "default forward is -Z");
    expectVec3Near(transform(cam.view(), 0,0,5, 1.0f), engine::Vec3(0,0,0), 1e-5f,
                   "camera view maps its position to origin");

    cam.setYaw(kHalfPi);
    expectVec3Near(cam.forward(), engine::Vec3(1,0,0), 1e-5f, "yaw=90 forward=+X (turn right)");
    expectVec3Near(cam.right(),   engine::Vec3(0,0,1), 1e-6f, "yaw=90 right=+Z");

    cam.setPitch(3.0f);
    expectNear(cam.pitch(),  engine::Camera::kMaxPitch,  1e-6f, "pitch clamped high");
    cam.setPitch(-3.0f);
    expectNear(cam.pitch(), -engine::Camera::kMaxPitch, 1e-6f, "pitch clamped low");
}

} // namespace

int main() {
    testVec3Basics();
    testIdentity();
    testTranslation();
    testRotation();
    testPerspective();
    testLookAt();
    testCamera();

    std::printf("[math] %d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}