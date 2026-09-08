//
// M5.3 frustum-culling verification (the EntityCulling/MoreCulling analog).
// Run: ctest --test-dir build --output-on-failure
// Harness intentionally minimal (project rule: minimize dependencies).
//
// Pins the two safety properties the frozen-image regression relies on:
//   1. Plane extraction is correct for this engine's Mat4 conventions
//      (column-major, GL clip volume) -- verified against hand-computed
//      half-space cases from a 90-degree camera at the origin.
//   2. The AABB test is CONSERVATIVE: intersecting, touching, inside, and
//      frustum-containing boxes are never culled; only boxes fully outside
//      one plane are.

#include "math/Frustum.h"
#include "math/Mat4.h"
#include "math/Vec3.h"
#include "rendering/Camera.h"

#include <cmath>
#include <cstdio>

#ifdef _WIN32
#include <io.h>   // _isatty: detect a double-clicked console run
#endif

namespace {

using engine::Aabb;
using engine::Frustum;

int g_checks   = 0;
int g_failures = 0;

void pauseIfInteractive() {
#ifdef _WIN32
    if (_isatty(_fileno(stdin))) {
        std::printf("\nPress Enter to close...");
        std::fflush(stdout);
        std::fgetc(stdin);
    }
#endif
}

void expectTrue(bool condition, const char* what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL: %s\n", what);
    }
}

// Camera at the origin looking down -Z (identity view), 90 deg vertical FOV,
// square aspect: at distance |z| the visible |x| and |y| are <= |z|.
Frustum originFrustum() {
    const engine::Mat4 proj = engine::Mat4::perspective(1.57079632f, 1.0f, 1.0f, 100.0f);
    return engine::Frustum::fromViewProjection(proj * engine::Mat4::identity());
}

Aabb box(float x0, float y0, float z0, float x1, float y1, float z1) {
    engine::Aabb b;
    b.seed(engine::Vec3(x0, y0, z0));
    b.grow(engine::Vec3(x1, y1, z1));
    return b;
}

void testPointContainment() {
    const Frustum f = originFrustum();

    // Straight ahead, between near (1) and far (100).
    expectTrue(f.containsPoint(engine::Vec3(0.0f, 0.0f, -5.0f)),
               "point straight ahead is inside");
    expectTrue(f.containsPoint(engine::Vec3(0.0f, 0.0f, -99.0f)),
               "point near far plane is inside");

    // Behind the camera.
    expectTrue(!f.containsPoint(engine::Vec3(0.0f, 0.0f, 5.0f)),
               "point behind camera is outside");
    // Closer than the near plane.
    expectTrue(!f.containsPoint(engine::Vec3(0.0f, 0.0f, -0.5f)),
               "point in front of near plane is outside");
    // Beyond the far plane.
    expectTrue(!f.containsPoint(engine::Vec3(0.0f, 0.0f, -150.0f)),
               "point beyond far plane is outside");

    // 90 deg fov, square aspect: at z=-3 the visible square is |x|,|y| <= 3.
    expectTrue(f.containsPoint(engine::Vec3(2.9f, 2.9f, -3.0f)),
               "point just inside the view corner is inside");
    expectTrue(!f.containsPoint(engine::Vec3(3.1f, 0.0f, -3.0f)),
               "point just outside right edge is outside");
    expectTrue(!f.containsPoint(engine::Vec3(-3.1f, 0.0f, -3.0f)),
               "point just outside left edge is outside");
    expectTrue(!f.containsPoint(engine::Vec3(0.0f, 3.1f, -3.0f)),
               "point just outside top edge is outside");
    expectTrue(!f.containsPoint(engine::Vec3(0.0f, -3.1f, -3.0f)),
               "point just outside bottom edge is outside");
}

void testAabbCulling() {
    const Frustum f = originFrustum();

    // Box fully in view -> kept.
    expectTrue(f.intersects(box(-1.0f, -1.0f, -6.0f, 1.0f, 1.0f, -4.0f)),
               "box in front of camera is kept");
    // Box fully behind the camera -> culled.
    expectTrue(!f.intersects(box(-1.0f, -1.0f, 4.0f, 1.0f, 1.0f, 12.0f)),
               "box behind camera is culled");
    // Box fully beyond far -> culled.
    expectTrue(!f.intersects(box(-1.0f, -1.0f, -120.0f, 1.0f, 1.0f, -110.0f)),
               "box beyond far plane is culled");
    // Box fully sideways -> culled.
    expectTrue(!f.intersects(box(20.0f, -1.0f, -6.0f, 22.0f, 1.0f, -4.0f)),
               "box fully outside the right plane is culled");

    // Conservative guarantees: everything that overlaps the volume is kept.
    expectTrue(f.intersects(box(-1.0f, -1.0f, -6.0f, 1.0f, 1.0f, 6.0f)),
               "box straddling the camera plane is kept");
    expectTrue(f.intersects(box(-1.0f, -1.0f, -1.5f, 1.0f, 1.0f, -0.5f)),
               "box straddling the near plane is kept");
    expectTrue(f.intersects(box(-50.0f, -50.0f, -50.0f, 50.0f, 50.0f, 50.0f)),
               "room-sized box containing camera + frustum is kept");
    // Touching the frustum boundary exactly (dot == 0) must be kept.
    expectTrue(f.intersects(box(-3.0f, -3.0f, -6.0f, 3.0f, 3.0f, -3.0f)),
               "box touching the 90-deg view edges is kept");
    // Flat (zero-thickness) boxes: quads behave like their hull.
    expectTrue(f.intersects(box(-1.0f, 5.0f, -8.0f, 1.0f, 5.0f, -8.0f)),
               "flat quad in view is kept");
    expectTrue(!f.intersects(box(-1.0f, 5.0f, 8.0f, 1.0f, 5.0f, 8.0f)),
               "flat quad behind camera is culled");
}

void testCameraIntegration() {
    // The real composition path used by the sandbox: Camera::viewProjection.
    engine::Camera cam;
    cam.setPosition(engine::Vec3(0.0f, 0.0f, 5.0f));  // pulled back on +Z
    cam.setYaw(0.0f);
    cam.setPitch(0.0f);
    cam.setPerspective(0.7854f /* 45 deg */, 1.0f, 0.1f, 100.0f);
    const Frustum f = engine::Frustum::fromViewProjection(cam.viewProjection());

    // Object at the world origin is dead center in view.
    expectTrue(f.containsPoint(engine::Vec3(0.0f, 0.0f, 0.0f)),
               "camera composition: origin point is in view");
    expectTrue(f.intersects(box(-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f)),
               "camera composition: origin box is kept");
    // Object far behind the camera (z = +20) is culled.
    expectTrue(!f.intersects(box(19.0f, -1.0f, 19.0f, 21.0f, 1.0f, 21.0f)),
               "camera composition: box behind camera is culled");

    // Rotating the camera 180 degrees flips the cull verdicts (yaw is around
    // +Y; looking +Z now), proving the planes track the view matrix. The box
    // sits ON the view axis (|x| <= 1) so only the depth verdict changes.
    engine::Camera camBack;
    camBack.setPosition(engine::Vec3(0.0f, 0.0f, 5.0f));
    camBack.setYaw(3.14159265f);
    camBack.setPitch(0.0f);
    camBack.setPerspective(0.7854f, 1.0f, 0.1f, 100.0f);
    const Frustum fBack = engine::Frustum::fromViewProjection(camBack.viewProjection());
    expectTrue(fBack.intersects(box(-1.0f, -1.0f, 19.0f, 1.0f, 1.0f, 21.0f)),
               "180-deg turn: box behind before is now in view");
    expectTrue(!fBack.intersects(box(-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f)),
               "180-deg turn: origin box is now behind the camera");
}

void testAabbTransform() {
    // grow/seed bookkeeping.
    engine::Aabb a;
    a.seed(engine::Vec3(1.0f, 2.0f, 3.0f));
    a.grow(engine::Vec3(-1.0f, 5.0f, -3.0f));
    expectTrue(a.min.x == -1.0f && a.min.y == 2.0f && a.min.z == -3.0f,
               "aabb min tracks grows");
    expectTrue(a.max.x == 1.0f && a.max.y == 5.0f && a.max.z == 3.0f,
               "aabb max tracks grows");

    // Translation moves the hull exactly.
    const engine::Aabb t = a.transformed(engine::Mat4::translate(engine::Vec3(10.0f, 0.0f, -20.0f)));
    expectTrue(t.min.x == 9.0f && t.min.z == -23.0f && t.max.x == 11.0f && t.max.z == -17.0f,
               "translated aabb shifts exactly");

    // 90-deg Y rotation: (x, z) -> (z, -x). Box x in [-1,2], z in [-1,1]
    // becomes x' in [-1,1], z' in [-2,1].
    const engine::Aabb r = box(-1.0f, -1.0f, -1.0f, 2.0f, 1.0f, 1.0f)
                               .transformed(engine::Mat4::rotateY(1.57079632f));
    expectTrue(std::fabs(r.min.x - -1.0f) < 1e-5f && std::fabs(r.max.x - 1.0f) < 1e-5f,
               "rotated aabb x-hull is conservative");
    expectTrue(std::fabs(r.min.z - -2.0f) < 1e-5f && std::fabs(r.max.z - 1.0f) < 1e-5f,
               "rotated aabb z-hull is conservative");

    // A symmetric box stays symmetric under rotation (sanity).
    const engine::Aabb s = box(-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f)
                               .transformed(engine::Mat4::rotateY(0.78539816f /* 45 deg */));
    expectTrue(std::fabs(s.max.x - 1.41421356f) < 1e-5f && std::fabs(s.min.x + 1.41421356f) < 1e-5f,
               "45-deg rotated cube hull is the circumscribed square");

    // Degenerate frustum (all-zero planes) rejects nothing: a cull flag
    // flipped off at runtime can never hide geometry.
    const engine::Frustum degenerate{};
    expectTrue(degenerate.intersects(box(-1.0f, -1.0f, 4.0f, 1.0f, 1.0f, 12.0f)),
               "degenerate frustum keeps everything (safe default)");
}

} // namespace

int main() {
    testPointContainment();
    testAabbCulling();
    testCameraIntegration();
    testAabbTransform();

    std::printf("frustum_tests: %d checks, %d failures\n", g_checks, g_failures);
    pauseIfInteractive();
    return g_failures == 0 ? 0 : 1;
}
