#include "math/Frustum.h"

namespace engine {

// ---------------------------------------------------------------------------
// Aabb
// ---------------------------------------------------------------------------

void Aabb::seed(const Vec3& p) {
    min = p;
    max = p;
}

void Aabb::grow(const Vec3& p) {
    if (p.x < min.x) min.x = p.x;
    if (p.y < min.y) min.y = p.y;
    if (p.z < min.z) min.z = p.z;
    if (p.x > max.x) max.x = p.x;
    if (p.y > max.y) max.y = p.y;
    if (p.z > max.z) max.z = p.z;
}

void Aabb::growToContain(const Aabb& b) {
    grow(b.min);
    grow(b.max);
}

Aabb Aabb::transformed(const Mat4& m) const {
    // Column-major point transform, applied to all 8 corners; the result is
    // the tight axis-aligned hull around the (possibly rotated) box.
    const float* a = m.data();
    Aabb out;
    out.seed(Vec3(
        a[0] * min.x + a[4] * min.y + a[8]  * min.z + a[12],
        a[1] * min.x + a[5] * min.y + a[9]  * min.z + a[13],
        a[2] * min.x + a[6] * min.y + a[10] * min.z + a[14]));
    for (int corner = 1; corner < 8; ++corner) {
        const float x = (corner & 1) ? max.x : min.x;
        const float y = (corner & 2) ? max.y : min.y;
        const float z = (corner & 4) ? max.z : min.z;
        out.grow(Vec3(
            a[0] * x + a[4] * y + a[8]  * z + a[12],
            a[1] * x + a[5] * y + a[9]  * z + a[13],
            a[2] * x + a[6] * y + a[10] * z + a[14]));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Frustum
// ---------------------------------------------------------------------------

Frustum Frustum::fromViewProjection(const Mat4& vp) {
    // Gribb-Hartmann extraction. Row r of a column-major Mat4 (m[col*4+row])
    // is (m[r], m[4+r], m[8+r], m[12+r]); with the GL clip volume the six
    // side planes are row3 +/- row{0,1,2}:
    //   left   = row3 + row0    right = row3 - row0
    //   bottom = row3 + row1    top    = row3 - row1
    //   near   = row3 + row2    far    = row3 - row2
    // A point p=(x,y,z,1) is inside when dot(plane, p) >= 0 for all six.
    // (Planes are NOT normalized -- only the sign of the p-vertex test
    // matters, so the division by |n| is skipped.)
    Frustum f;
    const float* m = vp.data();
    auto setPlane = [&f](int index, float a0, float b0, float c0, float d0,
                         float a1, float b1, float c1, float d1, bool add) {
        if (add) {
            f.m_planes[index][0] = a0 + a1;
            f.m_planes[index][1] = b0 + b1;
            f.m_planes[index][2] = c0 + c1;
            f.m_planes[index][3] = d0 + d1;
        } else {
            f.m_planes[index][0] = a0 - a1;
            f.m_planes[index][1] = b0 - b1;
            f.m_planes[index][2] = c0 - c1;
            f.m_planes[index][3] = d0 - d1;
        }
    };
    for (int r = 0; r < 3; ++r) {
        const float ra = m[r],  rb = m[4 + r],  rc = m[8 + r],  rd = m[12 + r];
        const float w0 = m[3], w1 = m[7], w2 = m[11], w3 = m[15];
        // left(2r), right(2r+1) from row0; bottom/top from row1; near/far row2
        setPlane(2 * r,     w0, w1, w2, w3, ra, rb, rc, rd, true);
        setPlane(2 * r + 1, w0, w1, w2, w3, ra, rb, rc, rd, false);
    }
    return f;
}

bool Frustum::containsPoint(const Vec3& p) const {
    for (int i = 0; i < 6; ++i) {
        if (m_planes[i][0] * p.x + m_planes[i][1] * p.y +
            m_planes[i][2] * p.z + m_planes[i][3] < 0.0f) {
            return false;
        }
    }
    return true;
}

bool Frustum::intersects(const Aabb& b) const {
    // p-vertex test per plane: the box corner furthest along the plane
    // normal. If even that corner is outside, the whole box is outside --
    // cull. Otherwise the box may intersect (or fully contain the frustum,
    // e.g. a room around the camera) -- keep. This can never cull a box that
    // overlaps the frustum, which is the safety property the frozen-image
    // regression (md5) relies on.
    for (int i = 0; i < 6; ++i) {
        const float a = m_planes[i][0];
        const float bb = m_planes[i][1];
        const float c = m_planes[i][2];
        const float d = m_planes[i][3];
        const float px = (a >= 0.0f) ? b.max.x : b.min.x;
        const float py = (bb >= 0.0f) ? b.max.y : b.min.y;
        const float pz = (c >= 0.0f) ? b.max.z : b.min.z;
        if (a * px + bb * py + c * pz + d < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace engine
