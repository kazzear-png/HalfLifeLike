#pragma once
//
// M5.3: view-frustum culling math -- the engine-side analog of the
// culling-family mods (EntityCulling / More Culling / Sodium's face culling):
// never submit geometry whose conservative bounds miss the view volume.
//
// Frustum: six planes extracted from a view-projection matrix (Gribb &
// Hartmann 1999, "Fast Extraction of Viewing Frustum Planes from the
// World-View-Projection Matrix"). The engine's Mat4 is column-major with the
// standard GL clip conventions (-w <= x,y,z <= w), so the rows of the
// composed matrix combine directly into the six planes; points on the camera
// side of every plane are inside.
//
// Aabb: axis-aligned bounding box, the conservative visibility proxy for a
// mesh. Boxes are intentionally kept degenerate-tolerant (flat quads, zero
// thickness) -- the plane test handles them naturally via the p-vertex.
//

#include "math/Mat4.h"
#include "math/Vec3.h"

namespace engine {

struct Aabb {
    Vec3 min{0.0f, 0.0f, 0.0f};
    Vec3 max{0.0f, 0.0f, 0.0f};

    // Expands to include p (call once per vertex; first call "seeds" from the
    // default zero box, so seed with an explicit vertex first, or use seed()).
    void seed(const Vec3& p);
    void grow(const Vec3& p);
    void growToContain(const Aabb& b);

    Vec3 center() const { return Vec3(0.5f * (min.x + max.x),
                                      0.5f * (min.y + max.y),
                                      0.5f * (min.z + max.z)); }

    // Conservative world-space AABB of this box under m: transforms all 8
    // corners and re-fits the axis-aligned hull. Never shrinks below the
    // true transformed volume, so culling against it can never clip visible
    // geometry (rotations make the hull larger, not smaller).
    Aabb transformed(const Mat4& m) const;
};

class Frustum {
public:
    // Six planes from a world->clip matrix (e.g. Camera::viewProjection()).
    // Plane order: left, right, bottom, top, near, far.
    static Frustum fromViewProjection(const Mat4& vp);

    // Exact point test (all six half-spaces).
    bool containsPoint(const Vec3& p) const;

    // Conservative AABB test using the p-vertex per plane: the box is culled
    // ONLY when its support corner along a plane's normal is outside that
    // plane. A box that intersects, touches, or is inside the frustum always
    // reports true -- no false negatives, by construction.
    bool intersects(const Aabb& b) const;

    // Test/instrumentation access: plane i as (a, b, c, d), i.e.
    // a*x + b*y + c*z + d >= 0 is the inside half-space.
    const float* plane(int i) const { return m_planes[i]; }

private:
    float m_planes[6][4] = {};
};

} // namespace engine
