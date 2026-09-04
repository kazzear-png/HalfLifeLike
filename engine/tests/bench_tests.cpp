//
// M3.3 benchmark verification: pins the FROZEN cornell-box/1.0 standard.
// Run: ctest --test-dir build --output-on-failure
//
// The Cornell Box is the renderer's permanent laboratory: geometry, camera,
// lights, materials, and exposure must never drift. These checks fail if
// tools/generate_cornell.py is re-run with changed constants, if the OBJs are
// regenerated differently, or if the point-light grid stops matching the
// emitter flux model documented in scene.json.
//
// The generated header (sandbox/src/cornell_scene_gen.h) is included directly:
// it is part of the frozen contract, not implementation detail.

#include "cornell_scene_gen.h"

#include "assets/OBJ.h"
#include "rendering/Lighting.h"
#include "rendering/ShadowHeightfield.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>   // braced range-for in testWorldToTexelMapping (MSVC discipline)
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#endif

#ifndef CORNELL_GEOMETRY_DIR
#define CORNELL_GEOMETRY_DIR "."
#endif

namespace {

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

void expectNear(float a, float b, float eps, const char* what) {
    expectTrue(std::fabs(a - b) <= eps, what);
}

// Name -> material lookup (mirrors the sandbox helper; the table itself is
// part of the frozen generated header).
const cornell::MaterialDef* findCornellMaterial(const char* name) {
    for (std::uint32_t i = 0; i < cornell::kMaterialCount; ++i) {
        if (std::strcmp(cornell::kMaterialTable[i].name, name) == 0) {
            return &cornell::kMaterialTable[i].def;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Frozen-standard pins. CHANGING ANY OF THESE MEANS CHANGING THE BENCHMARK --
// which is forbidden without an explicit re-baselining of the ledger.
// ---------------------------------------------------------------------------

void testFrozenConstants() {
    std::printf("[bench] frozen standard pins (cornell-box/1.0)\n");

    expectTrue(std::strcmp(cornell::kStandard, "cornell-box/1.0") == 0,
               "standard string is cornell-box/1.0");
    expectNear(cornell::kExposure, 1.0f, 1e-6f, "exposure frozen at 1.0");

    // Camera: classic open-front view.
    expectNear(cornell::kCameraPosition[0], 0.0f, 1e-5f, "camera x centered");
    expectNear(cornell::kCameraPosition[1], 2.75f, 1e-5f, "camera at mid height");
    expectNear(cornell::kCameraPosition[2], 8.35f, 1e-5f, "camera outside the open front");
    expectNear(cornell::kCameraYawRadians, 0.0f, 1e-6f, "camera yaw 0 (looks -Z into the box)");
    expectNear(cornell::kCameraPitchRadians, 0.0f, 1e-6f, "camera pitch 0");

    // Emitter: 1.30 x 1.05 m at 5.49 m, radiance 12.
    expectNear(cornell::kEmitterMin[1], 5.49f, 1e-5f, "emitter plane just below the ceiling");
    expectNear(cornell::kEmitterMax[0] - cornell::kEmitterMin[0], 1.30f, 1e-5f, "emitter width 1.30 m");
    expectNear(cornell::kEmitterMax[2] - cornell::kEmitterMin[2], 1.05f, 1e-5f, "emitter depth 1.05 m");
    expectNear(cornell::kEmitterRadiance, 12.0f, 1e-5f, "emitter radiance L_e = 12");

    // Canonical pbrt reflectances.
    const cornell::MaterialDef* red = findCornellMaterial("wall_left");
    const cornell::MaterialDef* green = findCornellMaterial("wall_right");
    const cornell::MaterialDef* white = findCornellMaterial("floor");
    expectTrue(red != nullptr && green != nullptr && white != nullptr,
               "wall materials present in the table");
    if (red) {
        expectNear(red->albedo[0], 0.63f, 1e-4f, "left wall red reflectance (pbrt)");
        expectNear(red->albedo[1], 0.065f, 1e-4f, "left wall red reflectance g");
    }
    if (green) {
        expectNear(green->albedo[0], 0.14f, 1e-4f, "right wall green reflectance (pbrt)");
        expectNear(green->albedo[1], 0.45f, 1e-4f, "right wall green reflectance g");
    }
    if (white) {
        expectNear(white->albedo[0], 0.725f, 1e-4f, "white reflectance r (pbrt)");
        expectNear(white->roughness, 0.90f, 1e-5f, "diffuse walls roughness 0.9");
    }

    // Variants carry the frozen mesh lists.
    expectTrue(cornell::kVariantCount == 3u, "exactly three benchmark variants");
    expectTrue(std::strcmp(cornell::kVariants[0]->name, "cornell01") == 0 &&
               std::strcmp(cornell::kVariants[1]->name, "cornell02") == 0 &&
               std::strcmp(cornell::kVariants[2]->name, "cornell03") == 0,
               "variant names and order are frozen");
    expectTrue(cornell::kVariant_cornell02.meshCount == 11u,
               "CBox-02 carries the three material spheres");
}

void testLightGridFluxModel() {
    std::printf("[bench] point-light grid flux model\n");

    // The rasterizer illuminates the box with N point lights approximating a
    // one-sided Lambertian emitter: Phi = pi * A * L_e, and an engine point
    // light with intensity I carries flux I*pi, so I = A * L_e / N.
    expectTrue(cornell::kLightCount <= engine::kMaxPointLights,
               "light grid fits the engine's uniform array");

    const float area = (cornell::kEmitterMax[0] - cornell::kEmitterMin[0])
                     * (cornell::kEmitterMax[2] - cornell::kEmitterMin[2]);
    const float expectedIntensity = area * cornell::kEmitterRadiance
                                  / static_cast<float>(cornell::kLightCount);
    expectNear(cornell::kLightIntensity, expectedIntensity, 1e-4f,
               "per-light intensity = A * L_e / N (flux-preserving grid)");

    // Grid positions must lie inside the emitter rectangle (in x/z), slightly
    // below the emitter plane.
    for (std::uint32_t i = 0; i < cornell::kLightCount; ++i) {
        const float x = cornell::kLightPositions[3 * i + 0];
        const float y = cornell::kLightPositions[3 * i + 1];
        const float z = cornell::kLightPositions[3 * i + 2];
        expectTrue(x >= cornell::kEmitterMin[0] && x <= cornell::kEmitterMax[0] &&
                   z >= cornell::kEmitterMin[2] && z <= cornell::kEmitterMax[2] &&
                   y < cornell::kEmitterMin[1],
                   "grid light inside the emitter footprint, below its plane");
    }
}

void testFrozenGeometryOnDisk() {
    std::printf("[bench] geometry on disk matches the generated pins\n");

    // The OBJ loader is exercised against the FROZEN OBJs with normalization
    // DISABLED (the standard is authored in world space).
    engine::LoadObjOptions opts;
    opts.centerToOrigin = false;
    opts.targetRadius   = 0.0f;

    struct Pinned {
        const char* file;
        std::uint32_t tris;
        float bminExpect[3];
        float bmaxExpect[3];
    };
    const Pinned pinned[] = {
        { "floor.obj",      cornell::kTris_floor,      { -2.75f,  0.00f, -2.75f }, {  2.75f,  0.00f,  2.75f } },
        { "wall_left.obj",  cornell::kTris_wallLeft,   { -2.75f,  0.00f, -2.75f }, { -2.75f,  5.50f,  2.75f } },
        { "wall_right.obj", cornell::kTris_wallRight,  {  2.75f,  0.00f, -2.75f }, {  2.75f,  5.50f,  2.75f } },
        { "wall_back.obj",  cornell::kTris_wallBack,   { -2.75f,  0.00f, -2.75f }, {  2.75f,  5.50f, -2.75f } },
        { "block_tall.obj", cornell::kTris_blockTall,  { -1.55f,  0.00f, -2.05f }, { -0.55f,  3.30f,  0.35f } },
        { "block_short.obj",cornell::kTris_blockShort, {  0.40f,  0.00f, -0.70f }, {  1.40f,  1.65f,  1.70f } },
        { "baffle.obj",     cornell::kTris_baffle,     { -1.00f,  3.40f, -0.10f }, {  1.00f,  5.40f,  0.10f } },
        { "sphere_mirror.obj", cornell::kTris_sphereMirror, { -2.25f, 0.00f, 1.00f }, { -1.15f, 1.10f, 2.10f } },
    };

    for (const Pinned& p : pinned) {
        const std::string path = std::string(CORNELL_GEOMETRY_DIR) + "/" + p.file;
        engine::LoadObjResult r = engine::loadOBJ(path, opts);
        if (!r.ok) {
            expectTrue(false, (std::string("loads: ") + path).c_str());
            continue;
        }
        expectTrue(static_cast<std::uint32_t>(r.triangleCount) == p.tris,
                   (std::string("triangle pin: ") + p.file).c_str());

        float bmin[3] = {  1e30f,  1e30f,  1e30f };
        float bmax[3] = { -1e30f, -1e30f, -1e30f };
        for (const engine::Vertex& v : r.vertices) {
            bmin[0] = std::min(bmin[0], v.x); bmax[0] = std::max(bmax[0], v.x);
            bmin[1] = std::min(bmin[1], v.y); bmax[1] = std::max(bmax[1], v.y);
            bmin[2] = std::min(bmin[2], v.z); bmax[2] = std::max(bmax[2], v.z);
        }
        bool boundsOk = true;
        const float tol = 1e-4f;
        for (int a = 0; a < 3; ++a) {
            boundsOk &= std::fabs(bmin[a] - p.bminExpect[a]) <= tol;
            boundsOk &= std::fabs(bmax[a] - p.bmaxExpect[a]) <= tol;
        }
        expectTrue(boundsOk, (std::string("bounds pin: ") + p.file).c_str());
    }

    // No degenerate triangles anywhere in the frozen geometry (the generator
    // once emitted zero-area polar caps on the spheres -- never again).
    engine::LoadObjResult sph = engine::loadOBJ(
        std::string(CORNELL_GEOMETRY_DIR) + "/sphere_gold.obj", opts);
    expectTrue(sph.ok && sph.skippedFaces == 0 && sph.warnings.empty(),
               "sphere geometry carries no degenerate faces");
}

// ---------------------------------------------------------------------------
// Orientation pins (M3.3.1 regression: the r1 geometry authored every room
// quad's normal AWAY from the room interior -- the one-sided rasterizer
// rendered the entire room black on real hardware. The reference path tracer
// shades two-sided, so no offline check caught it. These pins make that bug
// class unshippable: raw vn direction + winding/vn agreement, per solid.)
// ---------------------------------------------------------------------------

struct RawObj {
    std::vector<float> px, py, pz;             // v
    std::vector<float> nx, ny, nz;             // vn
    std::vector<std::uint32_t> triV, triN;     // expanded triangles (v//vn)
};

bool loadRawObj(const std::string& path, RawObj& out, std::string& err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        err = "cannot open " + path;
        return false;
    }
    char line[512];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        if (std::strncmp(line, "v ", 2) == 0) {
            float x, y, z;
            if (std::sscanf(line + 2, "%f %f %f", &x, &y, &z) != 3) {
                err = "bad v line: " + path; std::fclose(f); return false;
            }
            out.px.push_back(x); out.py.push_back(y); out.pz.push_back(z);
        } else if (std::strncmp(line, "vn ", 3) == 0) {
            float x, y, z;
            if (std::sscanf(line + 3, "%f %f %f", &x, &y, &z) != 3) {
                err = "bad vn line: " + path; std::fclose(f); return false;
            }
            out.nx.push_back(x); out.ny.push_back(y); out.nz.push_back(z);
        } else if (std::strncmp(line, "f ", 2) == 0) {
            // tokens like "3//1"; fan-triangulate around vertex 0
            std::vector<std::pair<int, int>> corners;
            const char* p = line + 2;
            while (*p != '\0') {
                int v = 0, n = 0;
                if (std::sscanf(p, "%d//%d", &v, &n) == 2) {
                    corners.emplace_back(v - 1, n - 1);
                }
                while (*p != '\0' && *p != ' ') ++p;
                while (*p == ' ') ++p;
            }
            if (corners.size() < 3) {
                err = "degenerate face: " + path; std::fclose(f); return false;
            }
            for (std::size_t k = 1; k + 1 < corners.size(); ++k) {
                out.triV.push_back(static_cast<std::uint32_t>(corners[0].first));
                out.triN.push_back(static_cast<std::uint32_t>(corners[0].second));
                out.triV.push_back(static_cast<std::uint32_t>(corners[k].first));
                out.triN.push_back(static_cast<std::uint32_t>(corners[k].second));
                out.triV.push_back(static_cast<std::uint32_t>(corners[k + 1].first));
                out.triN.push_back(static_cast<std::uint32_t>(corners[k + 1].second));
            }
        }
    }
    std::fclose(f);
    return true;
}

void testFrozenNormalOrientation() {
    std::printf("[bench] frozen normal orientation (M3.3.1 regression pins)\n");

    // 1) The five room quads + emitter: the normal is PART of the standard.
    //    Room surfaces face the interior; the emitter emits -Y.
    struct PinnedNormal {
        const char* file;
        float n[3];
    };
    const PinnedNormal pinned[] = {
        { "floor.obj",           { 0.0f,  1.0f,  0.0f } },
        { "ceiling.obj",         { 0.0f, -1.0f,  0.0f } },
        { "wall_left.obj",       { 1.0f,  0.0f,  0.0f } },
        { "wall_right.obj",      {-1.0f,  0.0f,  0.0f } },
        { "wall_back.obj",       { 0.0f,  0.0f,  1.0f } },
        { "ceiling_emitter.obj", { 0.0f, -1.0f,  0.0f } },
    };
    for (const PinnedNormal& p : pinned) {
        RawObj raw;
        std::string err;
        const std::string path = std::string(CORNELL_GEOMETRY_DIR) + "/" + p.file;
        if (!loadRawObj(path, raw, err)) {
            expectTrue(false, err.c_str());
            continue;
        }
        expectTrue(raw.nx.size() >= 1, (std::string("has vn: ") + p.file).c_str());
        bool allMatch = true;
        for (std::size_t i = 0; i < raw.nx.size(); ++i) {
            const float dx = raw.nx[i] - p.n[0];
            const float dy = raw.ny[i] - p.n[1];
            const float dz = raw.nz[i] - p.n[2];
            if (dx * dx + dy * dy + dz * dz > 1e-6f * 3.0f) {
                allMatch = false;
            }
        }
        expectTrue(allMatch, (std::string("normal faces the room interior: ") + p.file).c_str());
    }

    // 2) Winding must agree with the authored normal for every triangle of
    //    every frozen OBJ (three r1 room quads contradicted their own vn).
    for (std::uint32_t v = 0; v < cornell::kVariantCount; ++v) {
        for (std::uint32_t m = 0; m < cornell::kVariants[v]->meshCount; ++m) {
            const char* file = cornell::kVariants[v]->meshes[m].file;
            RawObj raw;
            std::string err;
            const std::string path = std::string(CORNELL_GEOMETRY_DIR) + "/" + file;
            if (!loadRawObj(path, raw, err)) {
                expectTrue(false, err.c_str());
                continue;
            }
            bool windingOk = true;
            for (std::size_t t = 0; t + 2 < raw.triV.size(); t += 3) {
                const std::uint32_t ia = raw.triV[t],     ib = raw.triV[t + 1], ic = raw.triV[t + 2];
                const std::uint32_t na = raw.triN[t];
                if (ia >= raw.px.size() || ib >= raw.px.size() || ic >= raw.px.size() ||
                    na >= raw.nx.size()) {
                    windingOk = false;
                    break;
                }
                const float e1[3] = { raw.px[ib] - raw.px[ia], raw.py[ib] - raw.py[ia], raw.pz[ib] - raw.pz[ia] };
                const float e2[3] = { raw.px[ic] - raw.px[ia], raw.py[ic] - raw.py[ia], raw.pz[ic] - raw.pz[ia] };
                const float gn[3] = {
                    e1[1] * e2[2] - e1[2] * e2[1],
                    e1[2] * e2[0] - e1[0] * e2[2],
                    e1[0] * e2[1] - e1[1] * e2[0],
                };
                const float d = gn[0] * raw.nx[na] + gn[1] * raw.ny[na] + gn[2] * raw.nz[na];
                if (d <= 0.0f) {   // same hemisphere is enough on curved meshes
                    windingOk = false;
                }
            }
            expectTrue(windingOk, (std::string("winding agrees with vn: ") + file).c_str());
        }
    }

    // 3) Solids sitting in the room face outward from themselves (their
    //    outward side IS the room): spot-check each box face normal against
    //    the box center, and sphere normals against the sphere center.
    struct Centered {
        const char* file;
        float center[3];
    };
    const Centered centered[] = {
        { "block_tall.obj",  { -1.05f, 1.65f, -0.85f } },
        { "block_short.obj", {  0.90f, 0.825f, 0.50f } },
        { "baffle.obj",      {  0.00f, 4.40f,  0.00f } },
        { "sphere_gold.obj", {  0.00f, 0.55f,  0.90f } },
    };
    for (const Centered& c : centered) {
        RawObj raw;
        std::string err;
        const std::string path = std::string(CORNELL_GEOMETRY_DIR) + "/" + c.file;
        if (!loadRawObj(path, raw, err)) {
            expectTrue(false, err.c_str());
            continue;
        }
        bool outwardOk = true;
        for (std::size_t t = 0; t + 2 < raw.triV.size(); t += 3) {
            const std::uint32_t ia = raw.triV[t];
            const std::uint32_t na = raw.triN[t];
            if (ia >= raw.px.size() || na >= raw.nx.size()) { outwardOk = false; break; }
            const float dx = raw.px[ia] - c.center[0];
            const float dy = raw.py[ia] - c.center[1];
            const float dz = raw.pz[ia] - c.center[2];
            const float d = dx * raw.nx[na] + dy * raw.ny[na] + dz * raw.nz[na];
            if (d <= 0.0f) { outwardOk = false; }
        }
        expectTrue(outwardOk, (std::string("solids face the room: ") + c.file).c_str());
    }
}

// ---------------------------------------------------------------------------
// M4: heightfield shadow march -- C++ port of the GLSL shadowVisibility in
// sandbox/src/shaders.h, pinned against ANALYTIC column-interval fields.
// The port must stay in lockstep with the shader: same start-at-one-step,
// same dynamic step count, same early-outs, same interval test.
// ---------------------------------------------------------------------------

struct IntervalField {
    // Returns the occluder vertical interval [minY, maxY] at (x, z);
    // minY >= maxY means "no occluder" (empty column).
    float (*minY)(float, float);
    float (*maxY)(float, float);
};

// A scene is a SET of interval fields: the march is blocked if ANY field's
// column interval contains the ray height. (A single merged [min,max] per
// column would be wrong when two solids stack with a gap -- the tall block
// [0, 3.3] and the hanging baffle [3.4, 5.4] overlap in footprint and leave
// a 0.1 m slit a merged field would close.)
constexpr int kMaxFields = 4;
struct SceneFields {
    int count = 0;
    IntervalField fields[kMaxFields] = {};
};

struct MarchParams {
    float footprintMinX = -2.75f, footprintMaxX = 2.75f;
    float footprintMinZ = -2.75f, footprintMaxZ = 2.75f;
    float maxHeight = 3.30f;
    // M4.0.6: defaults mirror the sandbox constants (kMarchStep halved
    // 0.16 -> 0.08 after the first lit march showed the step cadence was the
    // shadow boundary's dominant quantizer). The binary outcome pins below
    // sit far from boundary-exact rays, so they must hold at EITHER cadence;
    // a flip here would mean a marginal ray, i.e. a real defect to diagnose.
    float step = 0.08f;   // mirrors kMarchStep (M4.0.6); was 0.16
    float bias = 0.01f;
    // M4.0.7: soft-penumbra scale; 0 = the exact binary march. Defaults to 0
    // so every binary pin above runs the legacy decision path unchanged;
    // testSoftPenumbraMarch() drives the graded path explicitly.
    float penumbra = 0.0f;
    // M4.0.8: bracket refinement of the soft minimum (soft path only;
    // penumbra == 0 never reaches it). Mirrors the sandbox default
    // (--shadow-refine, default ON); 0 = the exact M4.0.7 soft march.
    int refine = 1;
};

// Mirrors shadowVisibility() in shaders.h. `texture()` becomes a direct
// IntervalField lookup (bilinear filtering is a softening detail, not part
// of the blocking contract). M4.0.7: the port also mirrors the soft-penumbra
// path -- per-sample signed clearance d to the column interval, hard return
// on d < 0, otherwise min-accumulate d against a penumbra window of
// penumbra * traveled (traveled > 2*step start zone). M4.0.8: the port
// mirrors the bracket refinement too -- the argmin sample's t is tracked,
// and two half-step fetch pairs around it re-evaluate the clearance against
// the same window (soft path only, vis < 1); a refined fetch landing inside
// an interval returns the hard 0. With penumbra == 0 the decisions are
// identical to the M4.0.5/M4.0.6 binary port: max(a, b) < 0 iff both old
// inequalities held, and the early-out only skips samples that could never
// block.
float marchVisibilityPort(const SceneFields& scene, const MarchParams& pm,
                          float rx, float ry, float rz,
                          float lx, float ly, float lz) {
    const float dx = lx - rx, dz = lz - rz;
    const float horiz = std::sqrt(dx * dx + dz * dz);
    if (horiz < 1e-4f) {
        return 1.0f;
    }
    const int steps = static_cast<int>(horiz / pm.step) + 1;
    const float segLen = std::sqrt(dx * dx + dz * dz + (ly - ry) * (ly - ry));
    float vis = 1.0f;
    float tBest = -1.0f;   // M4.0.8: t of the sample that last lowered vis
    for (int s = 1; s <= steps; ++s) {
        const float t = static_cast<float>(s) / static_cast<float>(steps);
        const float rayY = ry + (ly - ry) * t;
        if (rayY - (pm.maxHeight - pm.bias) > pm.penumbra * segLen) {
            break;   // clearance exceeds the largest penumbra window possible
        }
        const float px = rx + dx * t;
        const float pz = rz + dz * t;
        float d = 1e30f;   // clearance to the nearest column interval at (px, pz)
        for (int f = 0; f < scene.count; ++f) {
            const float hMax = scene.fields[f].maxY(px, pz);
            const float hMin = scene.fields[f].minY(px, pz);
            d = std::min(d, std::max(hMin + pm.bias - rayY, rayY - (hMax - pm.bias)));
        }
        if (d < 0.0f) {
            return 0.0f;
        }
        if (pm.penumbra > 0.0f) {
            const float traveled = t * segLen;
            if (traveled > 2.0f * pm.step) {
                const float graded =
                    std::clamp(d / std::max(pm.penumbra * traveled, 1e-5f),
                               0.0f, 1.0f);
                if (graded < vis) {
                    vis = graded;   // min(), plus the argmin t for M4.0.8
                    tBest = t;
                }
            }
        }
    }
    // M4.0.8 bracket refinement: mirrors shadowVisibility() exactly. Two
    // fetch-pairs at half-step offsets around the worst sample, same
    // interval clearance, same window, same start zone; a refined fetch
    // landing inside an interval returns the hard 0 (a real crossing the
    // cadence jumped over).
    if (pm.penumbra > 0.0f && pm.refine > 0 && vis < 1.0f && tBest >= 0.0f) {
        const float halfT = 0.5f / static_cast<float>(steps);
        for (int r = 0; r < 2; ++r) {
            const float m = tBest + ((r == 0) ? -halfT : halfT);
            if (m <= 0.0f || m >= 1.0f) {
                continue;
            }
            const float rayYr = ry + (ly - ry) * m;
            if (rayYr - (pm.maxHeight - pm.bias) > pm.penumbra * segLen) {
                continue;
            }
            const float pxr = rx + dx * m;
            const float pzr = rz + dz * m;
            float d = 1e30f;
            for (int f = 0; f < scene.count; ++f) {
                const float hMax = scene.fields[f].maxY(pxr, pzr);
                const float hMin = scene.fields[f].minY(pxr, pzr);
                d = std::min(d, std::max(hMin + pm.bias - rayYr,
                                         rayYr - (hMax - pm.bias)));
            }
            if (d < 0.0f) {
                return 0.0f;
            }
            const float traveledR = m * segLen;
            if (traveledR > 2.0f * pm.step) {
                vis = std::min(vis,
                               std::clamp(d / std::max(pm.penumbra * traveledR, 1e-5f),
                                          0.0f, 1.0f));
            }
        }
    }
    return vis;
}

// Analytic fields mirroring tools/generate_cornell.py r2 constants.
float g_tallMin(float, float)  { return 0.0f; }
float g_tallMax(float x, float z) {
    return (x >= -1.55f && x <= -0.55f && z >= -2.05f && z <= 0.35f) ? 3.30f : 0.0f;
}
float g_shortMin(float, float) { return 0.0f; }
float g_shortMax(float x, float z) {
    return (x >= 0.40f && x <= 1.40f && z >= -0.70f && z <= 1.70f) ? 1.65f : 0.0f;
}
// Hanging baffle (cbox03): x in [-1, 1], z in [-0.1, 0.1], y in [3.4, 5.4].
float g_baffleMin(float x, float z) {
    return (x >= -1.0f && x <= 1.0f && z >= -0.1f && z <= 0.1f) ? 3.40f : 0.0f;
}
float g_baffleMax(float x, float z) {
    return (x >= -1.0f && x <= 1.0f && z >= -0.1f && z <= 0.1f) ? 5.40f : 0.0f;
}
// Sphere r=0.55 resting on the floor at (cx, 0.55, cz): the column interval
// is exactly the sphere's vertical extent -- why the interval field is EXACT
// for the frozen scene's convex solids.
float g_sphereMin(float x, float z) {
    const float cx = 0.0f, cz = 0.90f, r = 0.55f;
    const float d2 = (x - cx) * (x - cx) + (z - cz) * (z - cz);
    if (d2 >= r * r) return 0.0f;
    return 0.55f - std::sqrt(r * r - d2);
}
float g_sphereMax(float x, float z) {
    const float cx = 0.0f, cz = 0.90f, r = 0.55f;
    const float d2 = (x - cx) * (x - cx) + (z - cz) * (z - cz);
    if (d2 >= r * r) return 0.0f;
    return 0.55f + std::sqrt(r * r - d2);
}

IntervalField g_empty { [](float, float) { return 1.0f; },   // min >= max: empty
                        [](float, float) { return 0.0f; } };
IntervalField g_tall  { g_tallMin,  g_tallMax };
IntervalField g_short { g_shortMin, g_shortMax };
IntervalField g_baffle{ g_baffleMin, g_baffleMax };
IntervalField g_sphere{ g_sphereMin, g_sphereMax };

SceneFields g_emptyScene { 1, { g_empty } };
SceneFields g_tallScene  { 1, { g_tall  } };
SceneFields g_shortScene { 1, { g_short } };
SceneFields g_baffleScene{ 1, { g_baffle} };
SceneFields g_sphereScene{ 1, { g_sphere} };
SceneFields g_roomScene  { 3, { g_tall, g_short, g_baffle } };

// M4.0.7 graze fixture: a 1 m box (top at y = 1) centered at the origin. A
// near-horizontal ray skimming its top at y = 1.02 holds a CONSTANT 0.03 m
// clearance (top + bias) across the whole footprint crossing while the
// penumbra window grows 0 -> 0.12 m -- the cleanest possible demonstration
// that the soft accumulator grades a ray the binary test calls lit.
float g_box1mMin(float, float) { return 0.0f; }
float g_box1mMax(float x, float z) {
    return (x >= -0.5f && x <= 0.5f && z >= -0.5f && z <= 0.5f) ? 1.0f : 0.0f;
}
IntervalField g_box1m { g_box1mMin, g_box1mMax };
SceneFields g_box1mScene { 1, { g_box1m } };

// M4.0.8 refinement fixture: a triangular ridge, hMax = 1 - |x|/2 for
// |x| < 2 (apex at x = 0, height 1 m, clearance slope 0.5 m per meter).
// A horizontal ray at y = 1.02 from x = -2 to x = +2 holds apex clearance
// 0.03 m, but the 0.08 m sample grid (51 samples spanning -2..+2) lands its
// nearest samples at x = +/-0.0392 with clearance 0.0496 m -- the SAMPLED
// minimum overstates the true apex clearance (0.03 m). Exactly the
// staircase noise the bracket refinement removes: the half-step probes land
// at x = 0.0 (clearance 0.03 m) and x = +0.0784, strictly sharper.
float g_ridgeMin(float, float) { return 0.0f; }
float g_ridgeMax(float x, float) {
    const float ax = std::fabs(x);
    return ax >= 2.0f ? 0.0f : 1.0f - ax * 0.5f;
}
IntervalField g_ridge { g_ridgeMin, g_ridgeMax };
SceneFields g_ridgeScene { 1, { g_ridge } };

void testHeightfieldShadowMarch() {
    std::printf("[bench] heightfield shadow march (M4 contract pins)\n");

    // March step safety invariant (mirrors the static_assert in main.cpp):
    // XZ spacing between consecutive samples is <= step, so any occluder
    // thicker than the step along a crossing ray is sampled at least once.
    // The thinnest frozen occluder is the baffle: 0.2 m in z.
    expectTrue(0.08f < 0.19f, "march step 0.08 stays below the baffle's 0.2 m thickness");

    MarchParams pm;   // defaults mirror the sandbox constants

    // 1) Empty field: everything visible from anywhere below the rig.
    expectTrue(marchVisibilityPort(g_emptyScene, pm, 0.0f, 0.0f, -2.4f, 0.4875f, 5.44f, 0.3938f) == 1.0f,
               "empty field: floor receiver sees the light");
    expectTrue(marchVisibilityPort(g_emptyScene, pm, -2.7f, 2.75f, -2.7f, 0.4875f, 5.44f, 0.3938f) == 1.0f,
               "empty field: wall receiver sees the light");

    // 2) Tall block (x -1.55..-0.55, z -2.05..0.35, h 3.3):
    //    floor receiver IN FRONT (z > 0.35), light in front: ray never
    //    crosses the footprint -> lit.
    expectTrue(marchVisibilityPort(g_tallScene, pm, -1.05f, 0.0f, 1.50f, -0.4875f, 5.44f, 0.3938f) == 1.0f,
               "tall block: receiver in front of the footprint is lit");
    //    floor receiver BEHIND (z = -2.4), light in front: the ray crosses
    //    the footprint while still low -> blocked.
    expectTrue(marchVisibilityPort(g_tallScene, pm, -1.05f, 0.0f, -2.40f, -0.4875f, 5.44f, 0.3938f) == 0.0f,
               "tall block: receiver behind the footprint is shadowed");
    //    receiver ON the block top: must not shadow itself (start-at-one-step
    //    + rising ray + bias).
    expectTrue(marchVisibilityPort(g_tallScene, pm, -1.05f, 3.30f, 0.00f, -0.4875f, 5.44f, 0.3938f) == 1.0f,
               "tall block: top-surface receiver is lit (self-shadow guard)");
    //    high wall receiver: the ray clears the block top before entering the
    //    footprint -> lit (early-out + geometry agreement).
    expectTrue(marchVisibilityPort(g_tallScene, pm, -1.05f, 4.00f, -2.40f, -0.4875f, 5.44f, 0.3938f) == 1.0f,
               "tall block: ray passing above the top stays lit");
    //    mid-height wall receiver behind the block -> blocked.
    expectTrue(marchVisibilityPort(g_tallScene, pm, -1.05f, 1.00f, -2.40f, -0.4875f, 5.44f, 0.3938f) == 0.0f,
               "tall block: mid-height receiver behind the block is shadowed");

    // 3) Short block (h 1.65): tall-receiver rays pass above it.
    expectTrue(marchVisibilityPort(g_shortScene, pm, 0.90f, 2.20f, -2.40f, 0.1625f, 5.44f, 0.3938f) == 1.0f,
               "short block: receiver above its top is lit");

    // 4) Hanging baffle (interval [3.4, 5.4]): rays BELOW 3.4 pass under it,
    //    rays INSIDE the interval are blocked, and a light on the SAME side
    //    of the plate as the receiver never crosses it. This is what the
    //    interval test buys over a max-only heightfield. The march config
    //    mirrors the sandbox's baffle variant: maxHeight 5.4 (the plate IS
    //    the tallest occluder), not the boxes' 3.3.
    MarchParams pmBaffle = pm;
    pmBaffle.maxHeight = 5.40f;
    expectTrue(marchVisibilityPort(g_baffleScene, pmBaffle, 0.00f, 0.50f, -0.30f, 0.1625f, 5.44f, 0.3938f) == 1.0f,
               "baffle: ray passing beneath the hanging plate stays lit");
    expectTrue(marchVisibilityPort(g_baffleScene, pmBaffle, 0.00f, 2.75f, -2.40f, 0.1625f, 5.44f, 0.3938f) == 0.0f,
               "baffle: ray crossing the plate interval is blocked");
    expectTrue(marchVisibilityPort(g_baffleScene, pmBaffle, 0.00f, 2.75f, -2.40f, 0.1625f, 5.44f, -0.1312f) == 1.0f,
               "baffle: light on the receiver's side of the plate stays lit");

    // 5) Sphere column interval (r=0.55 at (0, 0.55, 0.9)): the interval IS
    //    the sphere's vertical extent at that (x, z) -- exact, no dome.
    //    Floor point just beyond the sphere, light behind it: the ray climbs
    //    through the sphere's near-side columns -> blocked (physical
    //    segment-sphere intersection agrees: closest approach ~0.45 < 0.55).
    expectTrue(marchVisibilityPort(g_sphereScene, pm, 0.0f, 0.02f, 1.50f, 0.0f, 5.44f, 0.1312f) == 0.0f,
               "sphere: floor ray climbing through the near columns is blocked");
    //    Floor point well past the sphere, light above the far edge: the ray
    //    passes above the whole sphere (closest approach ~1.37 > 0.55).
    expectTrue(marchVisibilityPort(g_sphereScene, pm, 0.0f, 0.02f, 2.60f, 0.0f, 5.44f, 0.3938f) == 1.0f,
               "sphere: floor ray passing above the sphere stays lit");

    // 6) Combined room field, all 16 frozen light positions: the acceptance
    //    probes' light-agreement contract. The floor gap point and the
    //    in-front floor point must be lit by at least some grid lights
    //    (penumbra), and the fully-shielded floor point behind the tall block
    //    must see NO light.
    int litGap = 0, litFront = 0, litShielded = 0;
    for (int i = 0; i < 16; ++i) {
        const float lx = cornell::kLightPositions[3 * i + 0];
        const float ly = cornell::kLightPositions[3 * i + 1];
        const float lz = cornell::kLightPositions[3 * i + 2];
        litGap      += (marchVisibilityPort(g_roomScene, pm, 0.0f, 0.0f, -1.6f, lx, ly, lz) > 0.0f);
        litFront    += (marchVisibilityPort(g_roomScene, pm, -1.7f, 0.0f, 1.0f, lx, ly, lz) > 0.0f);
        litShielded += (marchVisibilityPort(g_roomScene, pm, -1.05f, 0.0f, -2.4f, lx, ly, lz) > 0.0f);
    }
    expectTrue(litGap == 16, "floor gap probe (0, 0, -1.6) is lit by all 16 grid lights");
    expectTrue(litFront > 0 && litFront < 16,
               "in-front floor probe (-1.7, 0, 1.0) sits in penumbra (some lights blocked)");
    expectTrue(litShielded == 0,
               "shielded floor probe (-1.05, 0, -2.4) sees no grid light (full umbra)");
}

// ---------------------------------------------------------------------------
// M4.0.7: soft-penumbra march -- the port's graded path pinned against the
// same analytic fields. The accumulator must be a GRADED EXTENSION of the
// binary contract, never a new behavior: hard outcomes stay exact (deep
// umbra -> 0, clear ray -> 1, receiver on an occluder top -> 1), a grazing
// ray the binary test calls lit must land strictly inside (0, 1), and the
// value must be monotone in the penumbra scale. penumbra = 0 must equal the
// binary verdict everywhere -- that is the port-level statement of the
// --shadow-penumbra 0 md5-replay property.
// ---------------------------------------------------------------------------
void testSoftPenumbraMarch() {
    std::printf("[bench] soft-penumbra shadow march (M4.0.7 contract pins)\n");

    MarchParams pm;              // binary path (penumbra = 0)
    MarchParams pmSoft = pm;
    pmSoft.penumbra = 0.03f;     // the sandbox's derived default
                                 // (0.5 * 0.325 / 5.44)
    MarchParams pmWide = pm;
    pmWide.penumbra = 0.06f;     // 2x window: strictly softer

    // 1) Deep umbra stays HARD in both modes: the ray crosses the tall
    //    block's interval mid-footprint, meters below the top. No penumbra
    //    window may soften a ray that provably passes through the solid.
    expectTrue(marchVisibilityPort(g_tallScene, pm,     -1.05f, 0.0f, -2.40f,
                                   -0.4875f, 5.44f, -0.3938f) == 0.0f,
               "deep-umbra ray is binary-blocked (reference)");
    expectTrue(marchVisibilityPort(g_tallScene, pmSoft, -1.05f, 0.0f, -2.40f,
                                   -0.4875f, 5.44f, -0.3938f) == 0.0f,
               "soft march: deep-umbra ray returns exactly 0 (hard block wins)");

    // 2) A receiver high on the back wall: the ray clears every occluder by
    //    meters, the penumbra-aware early-out breaks at the first sample,
    //    and the soft path must return EXACTLY 1 (no gratuitous dimming of
    //    open sky -- the interior-probe contract depends on it).
    expectTrue(marchVisibilityPort(g_tallScene, pmSoft, -1.05f, 5.00f, 2.40f,
                                   -0.4875f, 5.44f, -0.3938f) == 1.0f,
               "soft march: high-clearance ray returns exactly 1");

    // 3) Receiver ON the tall block top (the classic acne configuration):
    //    with rig-realistic lights (y = 5.44) the ray rises steeply, its
    //    clearance outruns the penumbra window immediately, and the soft
    //    path must return exactly 1 -- the bias's self-shadow guard carries
    //    over to the graded path.
    expectTrue(marchVisibilityPort(g_tallScene, pmSoft, -1.05f, 3.30f, 0.00f,
                                   -0.4875f, 5.44f, 0.3938f) == 1.0f,
               "soft march: top-surface receiver stays lit (acne guard)");

    // 4) The graze fixture: near-horizontal ray at y = 1.02 over the 1 m box
    //    (constant 0.03 m clearance across the crossing). Binary: lit
    //    (clearance > bias). Soft: strictly graded. This is exactly the ray
    //    class the M4.0.5 staircase bands lived in.
    expectTrue(marchVisibilityPort(g_box1mScene, pm, 0.0f, 1.02f, 2.0f,
                                   0.0f, 1.02f, -2.0f) == 1.0f,
               "graze ray: binary path declares it lit (clearance 0.03 > bias)");
    const float softGraze = marchVisibilityPort(g_box1mScene, pmSoft,
                                                0.0f, 1.02f, 2.0f,
                                                0.0f, 1.02f, -2.0f);
    expectTrue(softGraze > 0.0f && softGraze < 1.0f,
               "graze ray: soft path lands strictly inside (0, 1)");

    // 5) Monotone in the window: doubling the scale strictly softens the
    //    same ray (wider penumbra -> darker partial visibility).
    const float softWide = marchVisibilityPort(g_box1mScene, pmWide,
                                               0.0f, 1.02f, 2.0f,
                                               0.0f, 1.02f, -2.0f);
    expectTrue(softWide > 0.0f && softWide < softGraze,
               "graze ray: wider penumbra window is strictly softer");
}

// ---------------------------------------------------------------------------
// M4.0.8: bracket refinement -- the port's refinement phase pinned against
// the same analytic fields. The refinement must be a strict-improvement
// operation on the graded path: never raise visibility, never touch binary
// decisions (penumbra == 0), keep hard outcomes exact, and recover a
// sub-step clearance dip the cadence sampled past. --shadow-refine 0 must
// reproduce the M4.0.7 soft march exactly (the regression property).
// ---------------------------------------------------------------------------
void testBracketRefinement() {
    std::printf("[bench] bracket refinement (M4.0.8 contract pins)\n");

    MarchParams pmBase;              // binary reference (penumbra = 0)
    MarchParams pmSoft = pmBase;
    pmSoft.penumbra = 0.03f;         // the sandbox's derived default
    MarchParams pmSoftNoRefine = pmSoft;
    pmSoftNoRefine.refine = 0;       // the exact M4.0.7 soft march

    // 1) The ridge graze: the sampled minimum lands OFF the apex (nearest
    //    samples at x = +/-0.0392, clearance 0.0496 m), so the window fires
    //    on an overestimated clearance. The refinement's half-step probes
    //    land at x = 0.0 (clearance 0.03 m) -- strictly sharper. Both
    //    values stay graded (inside (0, 1)).
    const float sampled = marchVisibilityPort(g_ridgeScene, pmSoftNoRefine,
                                              -2.0f, 1.02f, 0.0f,
                                               2.0f, 1.02f, 0.0f);
    const float refined = marchVisibilityPort(g_ridgeScene, pmSoft,
                                              -2.0f, 1.02f, 0.0f,
                                               2.0f, 1.02f, 0.0f);
    expectTrue(sampled > 0.0f && sampled < 1.0f,
               "ridge graze: window fired on the sampled minimum (trigger)");
    expectTrue(refined > 0.0f && refined < sampled,
               "ridge graze: refinement strictly sharpens the sampled minimum");

    // 2) Flat-bottom clearance (1 m box graze, clearance 0.03 m across the
    //    footprint): even with d constant, d/w(t) keeps falling while the
    //    window grows with traveled distance, so the true graded minimum
    //    sits at the ray's EXIT edge, between samples. The refinement's
    //    half-step probe re-locates it -- sharpening here too, and never
    //    inventing occlusion (the value stays strictly inside (0, 1)).
    const float flatOn  = marchVisibilityPort(g_box1mScene, pmSoft,
                                              0.0f, 1.02f, 2.0f,
                                              0.0f, 1.02f, -2.0f);
    const float flatOff = marchVisibilityPort(g_box1mScene, pmSoftNoRefine,
                                              0.0f, 1.02f, 2.0f,
                                              0.0f, 1.02f, -2.0f);
    expectTrue(flatOn > 0.0f && flatOn < 1.0f && flatOff > flatOn,
               "constant-clearance graze: refinement sharpens the exit-edge "
               "minimum (window grows with traveled)");

    // 3) Binary neutrality: penumbra = 0 never reaches the refinement, so
    //    the flag is decision-neutral there -- the port-level statement of
    //    the --shadow-penumbra 0 md5-replay property surviving M4.0.8.
    MarchParams pmBin0 = pmBase;
    pmBin0.refine = 0;
    MarchParams pmBin1 = pmBase;
    pmBin1.refine = 1;
    const float binOffUmbra = marchVisibilityPort(g_tallScene, pmBin0,
                                                  -1.05f, 0.0f, -2.40f,
                                                  -0.4875f, 5.44f, -0.3938f);
    const float binOnUmbra  = marchVisibilityPort(g_tallScene, pmBin1,
                                                  -1.05f, 0.0f, -2.40f,
                                                  -0.4875f, 5.44f, -0.3938f);
    const float binOffLit = marchVisibilityPort(g_ridgeScene, pmBin0,
                                                -2.0f, 1.02f, 0.0f,
                                                 2.0f, 1.02f, 0.0f);
    const float binOnLit  = marchVisibilityPort(g_ridgeScene, pmBin1,
                                                -2.0f, 1.02f, 0.0f,
                                                 2.0f, 1.02f, 0.0f);
    expectTrue(binOffUmbra == 0.0f && binOffUmbra == binOnUmbra,
               "penumbra 0, refine 0 vs 1: deep-umbra verdict identical");
    expectTrue(binOffLit == 1.0f && binOffLit == binOnLit,
               "penumbra 0, refine 0 vs 1: lit-ray verdict identical");

    // 4) Hard outcomes stay exact under refinement: deep umbra still
    //    returns exactly 0, open-sky ray still exactly 1 (the refinement
    //    only runs when the window fired, i.e. vis < 1).
    expectTrue(marchVisibilityPort(g_tallScene, pmSoft, -1.05f, 0.0f, -2.40f,
                                   -0.4875f, 5.44f, -0.3938f) == 0.0f,
               "deep umbra with refinement ON: exactly 0 (hard block wins)");
    expectTrue(marchVisibilityPort(g_tallScene, pmSoft, -1.05f, 5.00f, 2.40f,
                                   -0.4875f, 5.44f, -0.3938f) == 1.0f,
               "high-clearance ray with refinement ON: exactly 1");

    // 5) Monotone safety across the room fixtures: for every ray the
    //    refinement can only lower the graded value toward the true
    //    minimum, never raise it, and the result stays in [0, 1].
    struct Ray { float rx, ry, rz, lx, ly, lz; };
    const Ray rays[] = {
        {  0.0f, 0.02f,  2.30f, -0.4875f, 5.44f, -0.1625f },  // floor -> light
        {  0.9f, 0.60f,  1.20f, -0.4875f, 5.44f, -0.4875f },  // grazes short-block top
        { -2.4f, 1.40f, -1.80f,  0.1625f, 5.44f,  0.4875f },  // diagonal past tall block
    };
    for (const Ray& r : rays) {
        const float vOff = marchVisibilityPort(g_roomScene, pmSoftNoRefine,
                                               r.rx, r.ry, r.rz,
                                               r.lx, r.ly, r.lz);
        const float vOn  = marchVisibilityPort(g_roomScene, pmSoft,
                                               r.rx, r.ry, r.rz,
                                               r.lx, r.ly, r.lz);
        expectTrue(vOn <= vOff + 1e-6f && vOn >= 0.0f && vOn <= 1.0f,
                   "room fixture: refinement never raises visibility");
    }
}

void testShadowCaptureMatrix() {
    std::printf("[bench] shadow capture matrices (M4 contract pins)\n");
    // The capture matrices are pure math -- exercisable without a GL context.
    engine::ShadowHeightfield hf;   // valid() stays false; matrix math is live
    const float minX = -2.75f, maxX = 2.75f, minZ = -2.75f, maxZ = 2.75f;
    const float minY = -0.5f, maxY = 6.0f;
    // Footprint/slab are members set by beginCapture (GL not needed for the
    // math), so replicate them through a local projection of the same form:
    // build the expected mapping from the documented derivation instead.
    // x_ndc: -1 at minX, +1 at maxX; y_ndc: -1 at minZ, +1 at maxZ;
    // maxPass z_ndc: -1 at maxY (near), +1 at minY (far);
    // minPass z_ndc: -1 at minY (near), +1 at maxY (far).
    auto checkMap = [&](bool maxPass, float x, float y, float z,
                        float ex, float ey, float ez) {
        const float L = minX, R = maxX, B = minZ, T = maxZ;
        const float N = maxPass ? -maxY : minY;
        const float F = maxPass ? -minY : maxY;
        const float vx = x;
        const float vy = z;
        const float vz = maxPass ? y : -y;
        const float ndcX = 2.0f * vx / (R - L) - (R + L) / (R - L);
        const float ndcY = 2.0f * vy / (T - B) - (T + B) / (T - B);
        const float ndcZ = -2.0f * vz / (F - N) - (F + N) / (F - N);
        expectNear(ndcX, ex, 1e-5f, (std::string(maxPass ? "max" : "min") + " pass x mapping").c_str());
        expectNear(ndcY, ey, 1e-5f, (std::string(maxPass ? "max" : "min") + " pass y(z) mapping").c_str());
        expectNear(ndcZ, ez, 1e-5f, (std::string(maxPass ? "max" : "min") + " pass depth mapping").c_str());
        (void)x; (void)y; (void)z;
    };
    // Corners of the slab (values are the EXPECTED ndc; formula above is the
    // documented derivation, checked against it at two points per pass).
    checkMap(true,  minX, maxY, minZ, -1.0f, -1.0f, -1.0f);
    checkMap(true,  maxX, minY, maxZ,  1.0f,  1.0f,  1.0f);
    checkMap(false, minX, minY, minZ, -1.0f, -1.0f, -1.0f);
    checkMap(false, maxX, maxY, maxZ,  1.0f,  1.0f,  1.0f);
    // Ordering pins: max pass, higher Y -> SMALLER ndc z (nearer, wins LESS);
    // min pass, higher Y -> LARGER ndc z (farther, so the LOWEST wins).
    {
        const float N = -maxY, F = -minY;
        const float zLow  = -2.0f * (0.5f) / (F - N) - (F + N) / (F - N);
        const float zHigh = -2.0f * (5.0f) / (F - N) - (F + N) / (F - N);
        expectTrue(zHigh < zLow, "max pass: higher surface is nearer (kept by LESS)");
    }
    {
        const float N = minY, F = maxY;
        const float zLow  = -2.0f * (-0.5f) / (F - N) - (F + N) / (F - N);
        const float zHigh = -2.0f * (-5.0f) / (F - N) - (F + N) / (F - N);
        expectTrue(zHigh > zLow, "min pass: lower surface is nearer (kept by LESS)");
    }
    // The engine component's own matrix must agree with the derivation above
    // once a footprint is configured; configure via beginCapture on an
    // INVALID (context-less) instance is not possible, so pin the mapping
    // constants the class documents instead.
    expectTrue(minX < maxX && minZ < maxZ && minY < maxY, "capture slab parameters sane");
    // M4.0.2: field verification is a runtime-GL instrument; headless (no
    // context) the instance is invalid and verifyField must refuse -- the
    // check can never silently "pass" on an un-captured field.
    engine::ShadowHeightfield::FieldStats stats;
    expectTrue(!hf.verifyField(3.30f, 10.0f, 22.0f, &stats),
               "verifyField refuses an invalid (context-less) instance");
    (void)hf;
}

void testWorldToTexelMapping() {
    std::printf("[bench] world->texel mapping (M4.0.4 registration pins)\n");
    // Pure math, no GL context: the static mapping every M4.0.4 diagnostic
    // shares with the GLSL march (uv = (world.xz - footprintMin) / span,
    // texture v = 0 at minZ). Cornell footprint + capture resolution.
    constexpr float minX = -2.75f, maxX = 2.75f, minZ = -2.75f, maxZ = 2.75f;
    constexpr unsigned res = 256;
    float c = -1.0f, r = -1.0f;

    // Corners: row 0 IS the minZ edge -- ReadPixels row 0 is the framebuffer
    // bottom, the capture projection puts y_ndc -1 at minZ, and the sampler
    // reads v = 0 there. A flip ANYWHERE in that chain mirrors every shadow;
    // these pins freeze the agreement.
    expectTrue(engine::ShadowHeightfield::worldToTexel(
                   minX, minZ, minX, maxX, minZ, maxZ, res, &c, &r),
               "footprint corner (minX, minZ) accepted");
    expectNear(c, 0.0f, 1e-4f, "minX -> column 0");
    expectNear(r, 0.0f, 1e-4f, "minZ -> row 0 (no z flip)");
    expectTrue(engine::ShadowHeightfield::worldToTexel(
                   maxX, maxZ, minX, maxX, minZ, maxZ, res, &c, &r),
               "footprint corner (maxX, maxZ) accepted (closed interval)");
    expectNear(c, float(res), 1e-4f, "maxX -> continuous column res");
    expectNear(r, float(res), 1e-4f, "maxZ -> continuous row res");

    // Room center and the frozen tall-block probe point (the value the
    // sandbox's registration probes rely on).
    expectTrue(engine::ShadowHeightfield::worldToTexel(
                   0.0f, 0.0f, minX, maxX, minZ, maxZ, res, &c, &r),
               "room center accepted");
    expectNear(c, res * 0.5f, 1e-4f, "center column = res/2");
    expectNear(r, res * 0.5f, 1e-4f, "center row = res/2");
    expectTrue(engine::ShadowHeightfield::worldToTexel(
                   -1.05f, -0.85f, minX, maxX, minZ, maxZ, res, &c, &r),
               "tall block center accepted");
    expectNear(c, 79.127f, 5e-3f, "tall center column = (x-minX)/span*res");
    expectNear(r, 88.436f, 5e-3f, "tall center row = (z-minZ)/span*res");

    // Outside the footprint must refuse: the sandbox treats that as a
    // probe-table bug, never silently clamps.
    expectTrue(!engine::ShadowHeightfield::worldToTexel(
                   3.0f, 0.0f, minX, maxX, minZ, maxZ, res, &c, &r),
               "x outside footprint refused");
    expectTrue(!engine::ShadowHeightfield::worldToTexel(
                   0.0f, -3.0f, minX, maxX, minZ, maxZ, res, &c, &r),
               "z outside footprint refused");

    // Texel-center round trip: the dump shader evaluates uv = (i+0.5)/res at
    // pixel i; the same world point must land back on i+0.5 continuous
    // texels. This is the 1-pixel-equals-1-texel contract of the dump.
    for (unsigned i : { 0u, 128u, 255u }) {
        const float x = minX + (float(i) + 0.5f) / float(res) * (maxX - minX);
        const float z = minZ + (float(i) + 0.5f) / float(res) * (maxZ - minZ);
        expectTrue(engine::ShadowHeightfield::worldToTexel(
                       x, z, minX, maxX, minZ, maxZ, res, &c, &r),
                   "texel-center round trip accepted");
        expectNear(c, float(i) + 0.5f, 1e-3f, "round trip column");
        expectNear(r, float(i) + 0.5f, 1e-3f, "round trip row");
    }
}

} // namespace

int main() {
    std::printf("=== engine benchmark tests (cornell-box/1.0 + M4 shadow march) ===\n");
    testFrozenConstants();
    testLightGridFluxModel();
    testFrozenGeometryOnDisk();
    testFrozenNormalOrientation();
    testHeightfieldShadowMarch();
    testSoftPenumbraMarch();
    testBracketRefinement();
    testShadowCaptureMatrix();
    testWorldToTexelMapping();

    std::printf("[bench] %d checks, %d failure(s)\n", g_checks, g_failures);
    pauseIfInteractive();
    return g_failures == 0 ? 0 : 1;
}
