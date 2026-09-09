#include "assets/OBJ.h"

#include "math/Vec3.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <utility>   // std::move (do not rely on <algorithm> leaking it)

namespace engine {
namespace {

// (positionIndex, normalIndex) pair packed into one 64-bit key.
// 0xFFFFFFFF marks "no normal".
std::uint64_t cornerKey(std::uint32_t positionIndex, std::uint32_t normalIndex) {
    return (static_cast<std::uint64_t>(positionIndex) << 32) | normalIndex;
}

// Resolves an OBJ 1-based index (negative = relative to end of list).
// Returns -1 when out of range.
int resolveIndex(std::int64_t raw, std::size_t listSize) {
    if (listSize == 0) return -1;
    std::int64_t idx = raw;
    if (idx < 0) idx = static_cast<std::int64_t>(listSize) + idx + 1;
    if (idx < 1 || idx > static_cast<std::int64_t>(listSize)) return -1;
    return static_cast<int>(idx - 1);
}

// M5.5: strict integer parse for face corner tokens. std::atoll interpreted
// every malformed token as 0 -- "f 2x//1" became corner 0 (silently invalid
// but in-range for the ==0 check), and "1.5" became corner 1 (a VALID index
// pointing at the wrong vertex: silent geometry corruption). strtoll with
// full-consumption + range checking rejects both.
bool parseInt64(const std::string& text, std::int64_t& value) {
    if (text.empty()) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (errno == ERANGE ||
        end == text.c_str() ||
        *end != '\0') {
        return false;
    }
    value = static_cast<std::int64_t>(parsed);
    return true;
}

// Splits "v/vt/vn" into up to three components; missing parts -> -1.
// Returns false when any PRESENT component fails strict integer parsing
// (the caller drops the whole face instead of guessing an index).
bool parseCorner(const std::string& token, std::int64_t& v, std::int64_t& vt, std::int64_t& vn) {
    v = 0;
    vt = -1;
    vn = -1;

    const std::size_t firstSlash = token.find('/');
    if (firstSlash == std::string::npos) {
        return parseInt64(token, v);
    }
    if (!parseInt64(token.substr(0, firstSlash), v)) {
        return false;
    }

    const std::size_t secondSlash = token.find('/', firstSlash + 1);
    if (secondSlash == std::string::npos) {
        // "v/vt" form (possibly empty vt after a trailing slash)
        const std::string vtString = token.substr(firstSlash + 1);
        if (!vtString.empty() && !parseInt64(vtString, vt)) {
            return false;
        }
        return true;
    }

    // "v/.../vn" form (possibly empty vt between the slashes)
    const std::string vtString = token.substr(firstSlash + 1, secondSlash - firstSlash - 1);
    if (!vtString.empty() && !parseInt64(vtString, vt)) {
        return false;
    }
    const std::string vnString = token.substr(secondSlash + 1);
    if (!vnString.empty() && !parseInt64(vnString, vn)) {
        return false;
    }
    return true;
}

// --- M5.5: ear-clipping polygon triangulation ------------------------------
// The old importer fan-triangulated every polygon from corner 0. For CONCAVE
// faces the fan triangles can extend OUTSIDE the polygon (overdrawn geometry
// that does not match the authored shape) -- the classic failure of naive
// fan triangulation. Ear clipping is O(n^2) worst case, correct for concave
// simple polygons, and identical to the fan for triangles (the only face
// form the shipped cornell/model assets use -- so vertex/index output for
// every frozen asset is byte-identical; only multi-corner faces change).

struct ProjectedPoint {
    float x;
    float y;
};

float cross2D(const ProjectedPoint& a,
              const ProjectedPoint& b,
              const ProjectedPoint& c) {
    return (b.x - a.x) * (c.y - a.y) -
           (b.y - a.y) * (c.x - a.x);
}

bool pointInTriangle2D(const ProjectedPoint& p,
                        const ProjectedPoint& a,
                        const ProjectedPoint& b,
                        const ProjectedPoint& c,
                        float winding) {
    constexpr float eps = 1.0e-7f;
    const float ab = cross2D(a, b, p) * winding;
    const float bc = cross2D(b, c, p) * winding;
    const float ca = cross2D(c, a, p) * winding;
    return ab >= -eps && bc >= -eps && ca >= -eps;
}

// Corners: any type with an accessible .pos member indexing into positions.
// Emits CCW-consistent triangles (indices into the corners array) that tile
// the polygon exactly. Returns false when the polygon is degenerate (zero
// area, collinear, or no ear found within the guard bound).
template <typename Corner>
bool triangulatePolygon(const Corner* corners,
                        int cornerCount,
                        const std::vector<Vec3>& positions,
                        std::vector<std::array<int, 3>>& outTriangles) {
    outTriangles.clear();

    if (cornerCount < 3) {
        return false;
    }

    if (cornerCount == 3) {
        const Vec3& a = positions[corners[0].pos];
        const Vec3& b = positions[corners[1].pos];
        const Vec3& c = positions[corners[2].pos];
        if (length(cross(b - a, c - a)) <= 1.0e-10f) {
            return false;  // degenerate triangle
        }
        outTriangles.push_back({0, 1, 2});
        return true;
    }

    // Newell normal: robust polygon-plane normal even for non-planar rings;
    // chooses the best projection axis (drop the dominant normal axis).
    Vec3 normal(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < cornerCount; ++i) {
        const Vec3& current = positions[corners[i].pos];
        const Vec3& next    = positions[corners[(i + 1) % cornerCount].pos];
        normal.x += (current.y - next.y) * (current.z + next.z);
        normal.y += (current.z - next.z) * (current.x + next.x);
        normal.z += (current.x - next.x) * (current.y + next.y);
    }
    if (length(normal) <= 1.0e-10f) {
        return false;  // flat/degenerate polygon ring
    }

    const float ax = std::fabs(normal.x);
    const float ay = std::fabs(normal.y);
    const float az = std::fabs(normal.z);
    int dropAxis = 2;
    if (ax >= ay && ax >= az) {
        dropAxis = 0;
    } else if (ay >= ax && ay >= az) {
        dropAxis = 1;
    }

    std::vector<ProjectedPoint> projected(static_cast<std::size_t>(cornerCount));
    for (int i = 0; i < cornerCount; ++i) {
        const Vec3& p = positions[corners[i].pos];
        if (dropAxis == 0) {
            projected[i] = {p.y, p.z};
        } else if (dropAxis == 1) {
            projected[i] = {p.x, p.z};
        } else {
            projected[i] = {p.x, p.y};
        }
    }

    // Winding of the projected ring (ear convexity must match it).
    float signedArea = 0.0f;
    for (int i = 0; i < cornerCount; ++i) {
        const ProjectedPoint& a = projected[i];
        const ProjectedPoint& b = projected[(i + 1) % cornerCount];
        signedArea += a.x * b.y - b.x * a.y;
    }
    if (std::fabs(signedArea) <= 1.0e-10f) {
        return false;  // projected ring is degenerate
    }
    const float winding = signedArea > 0.0f ? 1.0f : -1.0f;

    std::vector<int> remaining;
    remaining.reserve(static_cast<std::size_t>(cornerCount));
    for (int i = 0; i < cornerCount; ++i) {
        remaining.push_back(i);
    }

    int guard = 0;
    while (remaining.size() > 3) {
        bool foundEar = false;

        for (std::size_t i = 0; i < remaining.size(); ++i) {
            const int previous = remaining[(i + remaining.size() - 1) % remaining.size()];
            const int current  = remaining[i];
            const int next     = remaining[(i + 1) % remaining.size()];

            // Reflex vertex (concave corner) cannot be an ear tip.
            const float convexity =
                cross2D(projected[previous], projected[current], projected[next]) * winding;
            if (convexity <= 1.0e-8f) {
                continue;
            }

            // An ear must not contain any other remaining vertex (points
            // ON the ear boundary count as contained -- conservative but
            // safe; the ear search just moves on to the next candidate).
            bool containsVertex = false;
            for (int candidate : remaining) {
                if (candidate == previous || candidate == current || candidate == next) {
                    continue;
                }
                if (pointInTriangle2D(projected[candidate],
                                      projected[previous],
                                      projected[current],
                                      projected[next],
                                      winding)) {
                    containsVertex = true;
                    break;
                }
            }
            if (containsVertex) {
                continue;
            }

            // Degenerate (zero-area) ears are skipped but still clipped.
            const Vec3& a = positions[corners[previous].pos];
            const Vec3& b = positions[corners[current].pos];
            const Vec3& c = positions[corners[next].pos];
            if (length(cross(b - a, c - a)) > 1.0e-10f) {
                outTriangles.push_back({previous, current, next});
            }

            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
            foundEar = true;
            break;
        }

        if (!foundEar) {
            return false;  // no ear in a ring of >3 vertices: not a simple polygon
        }
        if (++guard > cornerCount * cornerCount) {
            return false;  // paranoia bound (cannot trigger for simple polygons)
        }
    }

    if (remaining.size() == 3) {
        const Vec3& a = positions[corners[remaining[0]].pos];
        const Vec3& b = positions[corners[remaining[1]].pos];
        const Vec3& c = positions[corners[remaining[2]].pos];
        if (length(cross(b - a, c - a)) > 1.0e-10f) {
            outTriangles.push_back({remaining[0], remaining[1], remaining[2]});
        }
    }

    return !outTriangles.empty();
}

} // namespace

LoadObjResult loadOBJ(const std::string& path, const LoadObjOptions& options) {
    LoadObjResult result;

    std::ifstream file(path);
    if (!file.is_open()) {
        result.error = "cannot open file: " + path;
        return result;
    }

    // Raw OBJ pools.
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    bool hasTexCoords = false;  // only presence matters: UVs are not consumed

    // Output assembly.
    std::vector<Vertex>                     outVertices;
    std::vector<std::uint32_t>              outIndices;
    std::unordered_map<std::uint64_t, std::uint32_t> dedupe;

    // Reuse per-line/per-face scratch instead of allocating for every triangle.
    std::istringstream stream;
    std::string tag, token;
    std::vector<std::array<int, 3>> triangles;
    std::string line;
    int lineNo = 0;
    bool fileHasNormals = false;
    int skippedFaces = 0;

    auto warn = [&](const std::string& message) {
        if (!result.warnings.empty()) result.warnings += "; ";
        result.warnings += message;
    };

    while (std::getline(file, line)) {
        ++lineNo;

        // Strip a trailing CR (Windows-authored files).
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Cut comments.
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);

        stream.clear();
        stream.str(line);
        if (!(stream >> tag)) continue;

        if (tag == "v") {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (stream >> x >> y >> z) {
                positions.push_back(Vec3(x, y, z));
            }
        } else if (tag == "vn") {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (stream >> x >> y >> z) {
                normals.push_back(normalize(Vec3(x, y, z)));
                fileHasNormals = true;
            }
        } else if (tag == "vt") {
            hasTexCoords = true;
        } else if (tag == "f") {
            // Collect the polygon's corners first.
            struct Corner { int pos; int nrm; bool hasNrm; };
            Corner corners[64];
            int cornerCount = 0;
            bool faceValid = true;

            while (stream >> token) {
                if (cornerCount >= 64) {  // absurd polygon guard
                    faceValid = false;
                    break;
                }
                std::int64_t vRaw = -1, vtRaw = -1, vnRaw = -1;
                // M5.5: strict corner parsing -- garbage tokens ("1.5",
                // "2x", "") fail the face instead of silently becoming
                // corner 0/1 via atoll's zero-clip.
                if (!parseCorner(token, vRaw, vtRaw, vnRaw) || vRaw == 0) {
                    faceValid = false;
                    break;
                }

                const int p = resolveIndex(vRaw, positions.size());
                if (p < 0) { faceValid = false; break; }

                Corner c{};
                c.pos = p;
                c.hasNrm = false;
                if (vnRaw != -1 && vnRaw != 0) {
                    const int n = resolveIndex(vnRaw, normals.size());
                    if (n >= 0) {
                        c.nrm = n;
                        c.hasNrm = true;
                    }
                }
                corners[cornerCount++] = c;
            }

            if (!faceValid || cornerCount < 3) {
                ++skippedFaces;
                continue;
            }

            // M5.5: EAR-CLIP triangulation with degeneracy rejection: the
            // polygon is decomposed into triangles that tile it exactly,
            // including concave faces (the old corner-0 fan could emit
            // triangles OUTSIDE the polygon). Zero-area triangles (repeated
            // corners, collinear points) are dropped instead of producing
            // NaN normals downstream. Triangle faces emit {0,1,2} --
            // identical output to the old fan for every shipped asset
            // (cornell geometry + bundled models are triangle-authored).
            if (!triangulatePolygon(corners, cornerCount, positions, triangles)) {
                ++skippedFaces;
                continue;
            }

            for (const std::array<int, 3>& triangle : triangles) {
                const Corner tri[3] = {
                    corners[triangle[0]],
                    corners[triangle[1]],
                    corners[triangle[2]]
                };

                const Vec3& pa = positions[tri[0].pos];
                const Vec3& pb = positions[tri[1].pos];
                const Vec3& pc = positions[tri[2].pos];

                const Vec3 rawNormal = cross(pb - pa, pc - pa);
                if (length(rawNormal) <= 1.0e-10f) {
                    ++skippedFaces;
                    continue;  // unreachable (ear clip already rejected); safety net
                }
                const Vec3 triangleNormal = normalize(rawNormal);

                // M5.5: ONE synthetic normal per triangle instead of one per
                // missing-normal corner. The old code pushed a fresh normals
                // pool entry for EVERY corner lacking an authored normal --
                // a quad without normals pushed 6 entries where 2 suffice,
                // and a 60-gon pushed ~3480 where 58 suffice. Flat shading
                // is unchanged: the synthetic normal IS the triangle normal,
                // and the (pos, nrm) dedupe key keeps indexing uniform.
                int syntheticNormalIndex = -1;
                bool needsSyntheticNormal = false;
                for (const Corner& corner : tri) {
                    if (!corner.hasNrm) {
                        needsSyntheticNormal = true;
                        break;
                    }
                }
                if (needsSyntheticNormal) {
                    normals.push_back(triangleNormal);
                    syntheticNormalIndex = static_cast<int>(normals.size()) - 1;
                }

                // Resolve all three normal indices up front so a bad one
                // rejects the WHOLE triangle (never a partial 1-2 index
                // emission that would corrupt the index stream).
                int nrmIndices[3] = { -1, -1, -1 };
                bool normalsResolved = true;
                for (int c = 0; c < 3; ++c) {
                    nrmIndices[c] = tri[c].hasNrm ? tri[c].nrm : syntheticNormalIndex;
                    if (nrmIndices[c] < 0) {
                        normalsResolved = false;
                    }
                }
                if (!normalsResolved) {
                    ++skippedFaces;
                    continue;  // unreachable (see synthetic push above); safety net
                }

                for (int c = 0; c < 3; ++c) {
                    const Corner& corner = tri[c];
                    const int nrmIndex = nrmIndices[c];

                    const std::uint32_t nIdx = static_cast<std::uint32_t>(nrmIndex);
                    const std::uint64_t key = cornerKey(static_cast<std::uint32_t>(corner.pos), nIdx);

                    const std::uint32_t newIndex = static_cast<std::uint32_t>(outVertices.size());
                    const auto [it, inserted] = dedupe.try_emplace(key, newIndex);
                    if (!inserted) {
                        outIndices.push_back(it->second);
                        continue;
                    }

                    const Vec3& p = positions[corner.pos];
                    const Vec3& n = normals[nIdx];
                    Vertex out{};
                    out.x = p.x; out.y = p.y; out.z = p.z;
                    out.nx = n.x; out.ny = n.y; out.nz = n.z;
                    out.r = 1.0f; out.g = 1.0f; out.b = 1.0f;  // white; PBR albedo tints

                    outVertices.push_back(out);
                    outIndices.push_back(newIndex);
                }
            }
        }
        // Everything else (o, g, s, usemtl, mtllib, ...) is intentionally ignored.
    }

    if (outVertices.empty() || outIndices.empty()) {
        result.error = "no drawable geometry found in: " + path;
        return result;
    }

    if (!fileHasNormals) {
        warn("no normals in file; flat shading generated");
    }
    if (skippedFaces > 0) {
        warn("skipped " + std::to_string(skippedFaces) + " invalid face(s)");
    }
    if (hasTexCoords) {
        warn("texcoords ignored (texturing lands with the material milestone)");
    }

    // --- optional normalization: center to origin, scale to target radius ---
    if (options.centerToOrigin || options.targetRadius > 0.0f) {
        Vec3 bmin( std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max());
        Vec3 bmax(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
        for (const Vertex& v : outVertices) {
            bmin = Vec3(std::min(bmin.x, v.x), std::min(bmin.y, v.y), std::min(bmin.z, v.z));
            bmax = Vec3(std::max(bmax.x, v.x), std::max(bmax.y, v.y), std::max(bmax.z, v.z));
        }
        const Vec3 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f);

        float radius = 0.0f;
        if (options.targetRadius > 0.0f) {
            for (const Vertex& v : outVertices) {
                const Vec3 d = Vec3(v.x, v.y, v.z) - center;
                radius = std::max(radius, length(d));
            }
        }
        const float scale = (options.targetRadius > 0.0f && radius > 1e-8f)
                            ? options.targetRadius / radius : 1.0f;

        for (Vertex& v : outVertices) {
            v.x = (v.x - center.x) * scale;
            v.y = (v.y - center.y) * scale;
            v.z = (v.z - center.z) * scale;
        }
    }

    result.vertices = std::move(outVertices);
    result.indices  = std::move(outIndices);
    result.triangleCount = static_cast<int>(result.indices.size() / 3);
    result.skippedFaces  = skippedFaces;
    result.ok = true;
    return result;
}

} // namespace engine
