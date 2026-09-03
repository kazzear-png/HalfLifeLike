#include "assets/OBJ.h"

#include "math/Vec3.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

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

// Splits "v/vt/vn" into up to three components; missing parts -> -1.
void parseCorner(const std::string& token, std::int64_t& v, std::int64_t& vt, std::int64_t& vn) {
    v = vt = vn = -1;  // vt/vn use -1 to mean "absent" here (distinct from resolveIndex's job)
    const std::size_t firstSlash = token.find('/');
    if (firstSlash == std::string::npos) {
        v = std::atoll(token.c_str());
        return;
    }
    v = std::atoll(token.substr(0, firstSlash).c_str());

    const std::size_t secondSlash = token.find('/', firstSlash + 1);
    if (secondSlash == std::string::npos) {
        const std::string vtStr = token.substr(firstSlash + 1);
        if (!vtStr.empty()) vt = std::atoll(vtStr.c_str());
        return;
    }
    // v/.../vn form (possibly empty vt between slashes)
    const std::string vtStr = token.substr(firstSlash + 1, secondSlash - firstSlash - 1);
    if (!vtStr.empty()) vt = std::atoll(vtStr.c_str());
    const std::string vnStr = token.substr(secondSlash + 1);
    if (!vnStr.empty()) vn = std::atoll(vnStr.c_str());
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
    std::vector<float> texCoordsUv;  // parsed, ignored

    // Output assembly.
    std::vector<Vertex>                     outVertices;
    std::vector<std::uint32_t>              outIndices;
    std::unordered_map<std::uint64_t, std::uint32_t> dedupe;

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

        std::istringstream stream(line);
        std::string tag;
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
            float u = 0.0f, v = 0.0f, w = 0.0f;
            stream >> u >> v;  // w optional
            texCoordsUv.push_back(u);
            texCoordsUv.push_back(v);
        } else if (tag == "f") {
            // Collect the polygon's corners first.
            struct Corner { int pos; int nrm; bool hasNrm; };
            Corner corners[64];
            int cornerCount = 0;
            bool faceValid = true;

            std::string token;
            while (stream >> token) {
                if (cornerCount >= 64) {  // absurd polygon guard
                    faceValid = false;
                    break;
                }
                std::int64_t vRaw = -1, vtRaw = -1, vnRaw = -1;
                parseCorner(token, vRaw, vtRaw, vnRaw);
                if (vRaw == 0) { faceValid = false; break; }

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

            // Fan triangulation with degeneracy rejection: zero-area triangles
            // (repeated corners, collinear points) are dropped instead of
            // producing NaN normals downstream.
            struct FanTri { int a, b, c; };
            FanTri fan[62];  // cornerCount <= 64 -> at most 62 fan triangles
            int fanCount = 0;
            for (int i = 1; i + 1 < cornerCount; ++i) {
                const Vec3& pa = positions[corners[0].pos];
                const Vec3& pb = positions[corners[i].pos];
                const Vec3& pc = positions[corners[i + 1].pos];
                if (length(cross(pb - pa, pc - pa)) < 1e-10f) {
                    ++skippedFaces;
                    continue;
                }
                fan[fanCount++] = FanTri{ 0, i, i + 1 };
            }
            if (fanCount == 0) {
                continue;  // whole polygon was degenerate (already counted)
            }

            // Face normal from the FIRST VALID triangle is the fallback for
            // corners without a normal (and for flat shading when the file
            // has none at all).
            const Vec3& a = positions[corners[fan[0].a].pos];
            const Vec3& b = positions[corners[fan[0].b].pos];
            const Vec3& c = positions[corners[fan[0].c].pos];
            const Vec3 faceNormal = normalize(cross(b - a, c - a));
            const bool hasFiniteFaceNormal =
                std::isfinite(faceNormal.x) && std::isfinite(faceNormal.y) &&
                std::isfinite(faceNormal.z) && (length(faceNormal) > 0.5f);

            for (int t = 0; t < fanCount; ++t) {
                const Corner tri[3] = { corners[fan[t].a], corners[fan[t].b], corners[fan[t].c] };

                for (const Corner& corner : tri) {
                    // Choose the corner normal: authored, else face normal.
                    int nrmIndex = -1;
                    if (corner.hasNrm) {
                        nrmIndex = corner.nrm;
                    } else if (hasFiniteFaceNormal) {
                        // Face normals become synthetic entries in the normal
                        // pool so dedup + indexing stay uniform.
                        normals.push_back(faceNormal);
                        nrmIndex = static_cast<int>(normals.size()) - 1;
                    } else {
                        normals.push_back(Vec3(0.0f, 1.0f, 0.0f));  // last resort
                        nrmIndex = static_cast<int>(normals.size()) - 1;
                    }

                    const std::uint32_t nIdx = static_cast<std::uint32_t>(nrmIndex);
                    const std::uint64_t key = cornerKey(static_cast<std::uint32_t>(corner.pos), nIdx);

                    auto it = dedupe.find(key);
                    if (it != dedupe.end()) {
                        outIndices.push_back(it->second);
                        continue;
                    }

                    const Vec3& p = positions[corner.pos];
                    const Vec3& n = normals[nIdx];
                    Vertex out{};
                    out.x = p.x; out.y = p.y; out.z = p.z;
                    out.nx = n.x; out.ny = n.y; out.nz = n.z;
                    out.r = 1.0f; out.g = 1.0f; out.b = 1.0f;  // white; PBR albedo tints

                    const std::uint32_t newIndex = static_cast<std::uint32_t>(outVertices.size());
                    outVertices.push_back(out);
                    outIndices.push_back(newIndex);
                    dedupe.emplace(key, newIndex);
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
    if (texCoordsUv.size() > 0) {
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
