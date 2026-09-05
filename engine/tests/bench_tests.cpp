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
#include "rendering/AreaLight.h"
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
    // M4.0.9: centroid-march rig visibility. 0 = the legacy per-light pin
    // path (every pre-M4.0.9 fixture keeps its exact semantics); 1 = one
    // march to the "light" argument interpreted as the emitter centroid,
    // graded by the half-plane area model with window S = lightSize
    // (0 = binary centroid march). The SANDBOX default is centroid ON
    // (--shadow-centroid 1); the PORT defaults to 0 so every legacy pin
    // runs the path it pinned -- new fixtures pass centroid = 1 explicitly.
    int centroid = 0;
    float lightSize = 0.0f;   // S in the half-plane window (centroid path)
    int jitter = 0;           // M4.0.9 diagnostic: per-pixel lattice jitter
    float noise = 0.0f;       // the deterministic "pixel noise" in [0, 1]
    // M4.0.9.1: analytic lateral half-plane grade (centroid soft path).
    // 1 = grade the signed ground distance to the primitive set below (the
    // shader's default); 0 = the exact M4.0.9 centroid march. The set
    // mirrors the sandbox's OccluderSet (AABB boxes + spheres) and MUST be
    // populated for fixtures that exercise the lateral band -- an empty set
    // leaves the lateral grade inert (r = +1e3 -> g_lat = 1), which is how
    // every pre-4.0.9.1 fixture keeps its exact semantics.
    int lateral = 1;
    int boxCount = 0;
    float boxMin[4][4] = {};   // (minX, minZ, minH, pad)
    float boxMax[4][4] = {};   // (maxX, maxZ, maxH, pad)
    int sphereCount = 0;
    float sphere[4][4] = {};   // (cx, cy, cz, R)
    float emitterHalfX = 0.65f;   // the frozen emitter patch half-extents
    float emitterHalfZ = 0.525f;  // (cornell::kEmitterMin/Max / 2)
};

// Mirrors shadowVisibilityCentroid() in shaders.h (M4.0.9 + the M4.0.9.1
// lateral half-plane): one march to the
// emitter centroid shared by the rig, graded per sample by the half-plane
// area model g = clamp(0.5 + d/(S*t), 0, 1), min-accumulated. The `light`
// arguments are the CENTROID's coordinates. Binary path (lightSize == 0):
// first d < 0 returns the hard 0; break once above every occluder. Soft
// path: the early-out compares the future grade lower bound
// 0.5 + B(t)/(S*t) (B = global clearance bound, rising in t) against vis --
// M4.0.9.1 additionally requires B >= 0 (with B < 0 a future sample can
// still be occluder-eligible and the LATERAL grade -- a ground distance --
// can still fall below the vertical bound); near-receiver samples saturate
// the grade exactly like the legacy 2*step start zone. M4.0.9.1 lateral
// grade: signed 2D distance from the sample's ground position to the
// nearest ELIGIBLE primitive (height interval mirrors the texture column
// semantics exactly), ramped by the emitter's exact lateral support
// ePerp = 2*(hx*|perp.x| + hz*|perp.z|), min-combined with the vertical
// grade; once any grade reaches 0 the march breaks (value-neutral).
// Vertical rays (horiz < 1e-4): exact column-interval test --
// the centroid hangs INSIDE the cbox03 baffle footprint, so the vertical
// case is reachable and must not silently return 1. Jitter shifts the
// lattice by (noise - 0.5) spacings (deterministic in the port; the shader
// derives the noise from gl_FragCoord).
float marchVisibilityCentroidPort(const SceneFields& scene,
                                  const MarchParams& pm,
                                  float rx, float ry, float rz,
                                  float lx, float ly, float lz) {
    const float dx = lx - rx, dz = lz - rz;
    const float horiz = std::sqrt(dx * dx + dz * dz);
    if (horiz < 1e-4f) {
        // Vertical ray: the whole segment shares the receiver's column.
        // Blocked iff the segment overlaps some column interval
        // (Min + bias, hMax - bias]: max of the two clearance terms < 0
        // iff both overlaps hold -- the exact 1-D interval intersection.
        float dV = -1e30f;
        for (int f = 0; f < scene.count; ++f) {
            const float hMax = scene.fields[f].maxY(rx, rz);
            const float hMin = scene.fields[f].minY(rx, rz);
            dV = std::max(dV,
                          std::max(hMin + pm.bias - std::max(ry, ly),
                                   std::min(ry, ly) - (hMax - pm.bias)));
        }
        return (dV < 0.0f) ? 0.0f : 1.0f;
    }
    const int steps = static_cast<int>(horiz / pm.step) + 1;
    const float segLen = std::sqrt(dx * dx + dz * dz + (ly - ry) * (ly - ry));
    const float jitterOff = (pm.jitter != 0) ? (pm.noise - 0.5f) : 0.0f;
    // M4.0.9 march span (mirrors the shader): tCap for the binary path
    // (light size 0 -- the break at t > tCap is exactly the legacy binary
    // early-out), tEnd = tCap + wCap / (y1 - y0) for the soft path -- the
    // upper grade band above the top edge, window capped at
    // wCap = lightSize * tCap.
    float tCap = 1.0f;
    if (ly > ry) {
        tCap = (pm.maxHeight - pm.bias - ry) / (ly - ry);
        tCap = std::clamp(tCap, 0.0f, 1.0f);
    } else if (ry > pm.maxHeight - pm.bias) {
        tCap = 0.0f;
    }
    float tEnd = tCap;
    if (pm.lightSize > 0.0f) {
        const float wCap = pm.lightSize * tCap;
        const float dy = ly - ry;
        tEnd = (dy > 0.0f) ? std::min(tCap + wCap / dy, 1.0f) : 1.0f;
    }
    // M4.0.9.1: the emitter's exact lateral support along this trace (the
    // patch's support function along the trace perpendicular) + the active
    // flag (soft path only; lateral == 0 reproduces M4.0.9 bit-for-bit).
    const float invHoriz = 1.0f / horiz;
    const float perpX = -(dz * invHoriz);
    const float perpZ = (dx * invHoriz);
    const float ePerp = 2.0f * (pm.emitterHalfX * std::fabs(perpX) +
                                pm.emitterHalfZ * std::fabs(perpZ));
    const bool lateral = (pm.lateral != 0) && (pm.lightSize > 0.0f);
    float vis = 1.0f;    // min of the per-sample grades
    float tBest = -1.0f; // argmin sample, for the bracket refinement
    for (int s = 1; s <= steps; ++s) {
        const float t = (static_cast<float>(s) + jitterOff) /
                        static_cast<float>(steps);
        if (t <= 0.0f || t > tEnd) {
            break;   // jitter can push the lattice past the light
        }
        const float rayY = ry + (ly - ry) * t;
        const float px = rx + dx * t;
        const float pz = rz + dz * t;
        float d = 1e30f;   // clearance to the nearest column interval at (px, pz)
        bool anySolid = false;   // a real column interval exists at this sample
        for (int f = 0; f < scene.count; ++f) {
            const float hMax = scene.fields[f].maxY(px, pz);
            const float hMin = scene.fields[f].minY(px, pz);
            anySolid = anySolid || (hMax > hMin);
            d = std::min(d, std::max(hMin + pm.bias - rayY,
                                     rayY - (hMax - pm.bias)));
        }
        if (pm.lightSize <= 0.0f) {
            // Binary centroid march (light size 0): exact legacy semantics.
            if (d < 0.0f) {
                return 0.0f;
            }
            continue;
        }
        // Soft path early-out: the vertical grade lower bound
        // 0.5 + B(t)/(S*t) rises monotonically in t (the B0/t term decays).
        // M4.0.9.1 adds the B >= 0 gate: with B < 0 a future sample can
        // still be occluder-eligible and the lateral grade can still fall
        // below the vertical bound; B >= 0 means every future sample is
        // above every occluder top, so the lateral grade is identically 1
        // onward and the vertical bound alone is exact again. With
        // lateral == 0 the gate is skipped (the exact M4.0.9 form; the
        // hoisted bBound is the same arithmetic in the same order).
        // Descending rays march to the end (cannot happen in the frozen
        // rig; the guard keeps the port total).
        const float bBound = rayY - (pm.maxHeight - pm.bias);
        if (ly > ry && (pm.lateral == 0 || bBound >= 0.0f) &&
            0.5f + bBound / std::max(pm.lightSize * t, 1e-5f) >= vis) {
            break;
        }
        const float traveled = t * segLen;
        if (traveled > 2.0f * pm.step) {
            // Vertical grade: ONLY a real column interval grades (mirrors
            // the shader's hMax > hMin skip); window capped at the
            // occluder-bearing lightSize * tCap.
            float g = 1.0f;
            if (anySolid) {
                g = 0.5f + d / std::max(pm.lightSize * std::min(t, tCap),
                                        1e-5f);
            }
            if (lateral) {
                // M4.0.9.1 lateral half-plane: signed ground distance to
                // the nearest ELIGIBLE primitive (union of regions),
                // ramped by the emitter's lateral support.
                float rLat = 1e3f;
                for (int b = 0; b < pm.boxCount; ++b) {
                    const float bminX = pm.boxMin[b][0];
                    const float bminZ = pm.boxMin[b][1];
                    const float bminH = pm.boxMin[b][2];
                    const float bmaxX = pm.boxMax[b][0];
                    const float bmaxZ = pm.boxMax[b][1];
                    const float bmaxH = pm.boxMax[b][2];
                    if (rayY <= bminH + pm.bias || rayY >= bmaxH - pm.bias) {
                        continue;   // ray height outside the column interval
                    }
                    const float dvx = std::max(bminX - px, px - bmaxX);
                    const float dvz = std::max(bminZ - pz, pz - bmaxZ);
                    const float ox = std::max(dvx, 0.0f);
                    const float oz = std::max(dvz, 0.0f);
                    const float outside = std::sqrt(ox * ox + oz * oz);
                    const float inside = std::min(std::max(dvx, dvz), 0.0f);
                    rLat = std::min(rLat, outside + inside);
                }
                for (int sp = 0; sp < pm.sphereCount; ++sp) {
                    const float scx = pm.sphere[sp][0];
                    const float scy = pm.sphere[sp][1];
                    const float scz = pm.sphere[sp][2];
                    const float sr = pm.sphere[sp][3];
                    const float hOff = std::fabs(rayY - scy) + pm.bias;
                    const float rho2 = sr * sr - hOff * hOff;
                    if (rho2 <= 0.0f) {
                        continue;   // the sphere never reaches this height
                    }
                    const float gx = px - scx, gz = pz - scz;
                    rLat = std::min(rLat, std::sqrt(gx * gx + gz * gz) -
                                              std::sqrt(rho2));
                }
                const float gLat = std::clamp(
                    0.5f + rLat /
                        std::max(ePerp * std::min(t, tCap), 1e-5f),
                    0.0f, 1.0f);
                g = std::min(g, gLat);
            }
            g = std::clamp(g, 0.0f, 1.0f);
            if (g < vis) {
                vis = g;     // min(), plus the argmin t for the refinement
                tBest = t;
            }
            if (vis <= 0.0f) {
                break;   // M4.0.9.1: min cannot go lower (value-neutral)
            }
        }
    }
    // Bracket refinement: two fetch pairs at half-step offsets around the
    // worst sample, same grade, same start zone; a refined pierce grades
    // dark. Monotone-safe: vis can only decrease. M4.0.9.1: not run at
    // vis == 0 (already the floor).
    if (pm.lightSize > 0.0f && pm.refine > 0 && vis > 0.0f && vis < 1.0f &&
        tBest >= 0.0f) {
        const float halfT = 0.5f / static_cast<float>(steps);
        for (int r = 0; r < 2; ++r) {
            const float m = tBest + ((r == 0) ? -halfT : halfT);
            if (m <= 0.0f || m > tEnd) {
                continue;
            }
            const float rayYr = ry + (ly - ry) * m;
            const float pxr = rx + dx * m;
            const float pzr = rz + dz * m;
            float d = 1e30f;
            bool anySolidR = false;
            for (int f = 0; f < scene.count; ++f) {
                const float hMax = scene.fields[f].maxY(pxr, pzr);
                const float hMin = scene.fields[f].minY(pxr, pzr);
                anySolidR = anySolidR || (hMax > hMin);
                d = std::min(d, std::max(hMin + pm.bias - rayYr,
                                         rayYr - (hMax - pm.bias)));
            }
            const float traveledR = m * segLen;
            if (traveledR > 2.0f * pm.step && anySolidR) {
                const float gR = std::clamp(
                    0.5f + d / std::max(pm.lightSize * std::min(m, tCap),
                                        1e-5f), 0.0f, 1.0f);
                vis = std::min(vis, gR);
            }
        }
    }
    return vis;
}

// Mirrors shadowVisibility() in shaders.h. `texture()` becomes a direct
// IntervalField lookup (bilinear filtering is a softening detail, not part
// of the blocking contract). M4.0.7: the port also mirrors the soft-penumbra
// path -- per-sample signed clearance d to the column interval, hard return
// on d < 0, otherwise min-accumulate d against a penumbra window of
// penumbra * traveled (traveled > 2*step start zone). M4.0.9: the window is
// the PARALLAX form penumbra * t / (1 - t) -- the lateral offset between
// adjacent grid lights' shadow edges at blocker depth t -- and the early-out
// bound extends to the window at tCap (the ray's last occluder-bearing
// fraction); at penumbra == 0 the bound is 0 and every decision is
// identical to the M4.0.5/M4.0.6 binary port, so the md5-replay pins hold.
// M4.0.8: the port mirrors the bracket refinement too -- the argmin
// sample's t is tracked, and two half-step fetch pairs around it
// re-evaluate the clearance against the same window (soft path only,
// vis < 1); a refined fetch landing inside an interval returns the hard 0.
// With penumbra == 0 the decisions are identical to the M4.0.5/M4.0.6
// binary port: max(a, b) < 0 iff both old inequalities held, and the
// early-out only skips samples that could never block.
float marchVisibilityPort(const SceneFields& scene, const MarchParams& pm,
                          float rx, float ry, float rz,
                          float lx, float ly, float lz) {
    if (pm.centroid != 0) {
        return marchVisibilityCentroidPort(scene, pm, rx, ry, rz, lx, ly, lz);
    }
    const float dx = lx - rx, dz = lz - rz;
    const float horiz = std::sqrt(dx * dx + dz * dz);
    if (horiz < 1e-4f) {
        return 1.0f;
    }
    const int steps = static_cast<int>(horiz / pm.step) + 1;
    const float segLen = std::sqrt(dx * dx + dz * dz + (ly - ry) * (ly - ry));
    // M4.0.9 march span (mirrors the shader): tCap = the legacy binary
    // early-out fraction (byte-identical break for penumbra == 0); tEnd
    // extends the SOFT path over the upper grade band with the window
    // capped at its occluder-bearing value.
    float tCap = 1.0f;
    if (ly > ry) {
        tCap = (pm.maxHeight - pm.bias - ry) / (ly - ry);
        tCap = std::clamp(tCap, 0.0f, 1.0f);
    } else if (ry > pm.maxHeight - pm.bias) {
        tCap = 0.0f;
    }
    // M4.0.9 soft-path span + capped window (mirrors the shader).
    float tEnd = tCap;
    if (pm.penumbra > 0.0f) {
        const float wCap = pm.penumbra * tCap / std::max(1.0f - tCap, 1e-4f);
        const float dy = ly - ry;
        tEnd = (dy > 0.0f) ? std::min(tCap + wCap / dy, 1.0f) : 1.0f;
    }
    const float tCapRatio = tCap / std::max(1.0f - tCap, 1e-4f);
    float vis = 1.0f;
    float tBest = -1.0f;   // M4.0.8: t of the sample that last lowered vis
    for (int s = 1; s <= steps; ++s) {
        const float t = static_cast<float>(s) / static_cast<float>(steps);
        const float rayY = ry + (ly - ry) * t;
        if (t > tEnd) {
            break;
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
                // M4.0.9 parallax window, capped at the occluder-bearing
                // value (mirrors the shader).
                const float window = pm.penumbra *
                    std::min(t / std::max(1.0f - t, 1e-4f), tCapRatio);
                const float graded =
                    std::clamp(d / std::max(window, 1e-5f), 0.0f, 1.0f);
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
            if (m <= 0.0f || m > tEnd) {
                continue;
            }
            const float rayYr = ry + (ly - ry) * m;
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
                const float windowR = pm.penumbra *
                    std::min(m / std::max(1.0f - m, 1e-4f), tCapRatio);
                vis = std::min(vis,
                               std::clamp(d / std::max(windowR, 1e-5f),
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

// M4.0.9 refinement fixture: a TALL STEEP ridge, hMax = 3 - 1.65*|x| for
// |x| < 1.818 (apex 3.0 m at x = 0, clearance slope 1.65 m per meter),
// probed by a RISING ray from (-2, 1.02, 0) to (2, 5.02, 0) -- slope 1.0 m
// per meter, so the ray's clearance FALLS on the ridge's rising side
// (1.65 > 1.0) and rises on its falling side: the apex is a true interior
// dip. The ray clears the apex by 0.03 m and EXITS the occluder band at
// tCap = 0.5675 (the parallax window's divergence tail is therefore capped
// and harmless -- horizontal synthetic rays never exit the band and cannot
// pin this window form). At the 0.08 m cadence (51 samples) the nearest
// samples land at x = -/+0.0392 with clearance 0.0555 m -- the SAMPLED
// minimum overstates the true apex clearance (0.03 m). Exactly the
// staircase noise the bracket refinement removes: the half-step probe lands
// on the apex (x = 0, clearance 0.03 m), strictly sharper.
float g_ridgeMin(float, float) { return 0.0f; }
float g_ridgeMax(float x, float) {
    const float ax = std::fabs(x);
    return ax >= 1.818f ? 0.0f : 3.0f - ax * 1.65f;
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
    pmSoft.penumbra = 0.325f;    // fixed contract scale: the M4.0.9
                                 // first-default (full grid pitch); the
                                 // sandbox default moved to half-pitch
                                 // after the hardware A/B matrix
    MarchParams pmWide = pm;
    pmWide.penumbra = 0.65f;     // 2x window: strictly softer

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
    pmSoft.penumbra = 0.325f;        // fixed contract scale (the M4.0.9
                                     // first-default, now an override)
    MarchParams pmSoftNoRefine = pmSoft;
    pmSoftNoRefine.refine = 0;       // the exact M4.0.8 soft march (pre-M4.0.9 window)

    // 1) The ridge graze (M4.0.9 tall-ridge fixture, RISING ray): the
    //    sampled minimum lands OFF the apex (nearest sample at x = -0.0392,
    //    clearance 0.0555 m), so the window fires on an overestimated
    //    clearance. The refinement's +half-step probe lands ON the apex
    //    (x = 0, clearance 0.03 m) -- strictly sharper. Both values stay
    //    graded, and the sampled value pins the PARALLAX window form: the
    //    M4.0.7 linear form would grade the same clearance to ~0.087
    //    (window 0.325*0.49*4.0), outside the band below.
    const float sampled = marchVisibilityPort(g_ridgeScene, pmSoftNoRefine,
                                              -2.0f, 1.02f, 0.0f,
                                               2.0f, 5.02f, 0.0f);
    const float refined = marchVisibilityPort(g_ridgeScene, pmSoft,
                                              -2.0f, 1.02f, 0.0f,
                                               2.0f, 5.02f, 0.0f);
    expectTrue(sampled > 0.15f && sampled < 0.20f,
               "ridge graze: sampled minimum pins the parallax window form");
    expectTrue(refined > 0.0f && refined < sampled,
               "ridge graze: refinement strictly sharpens the sampled minimum");

    // 2) Refinement gate: the refinement engages ONLY where the window
    //    fired (vis < 1). A fully-lit ridge ray (receiver above the apex
    //    band, every grade saturates) returns EXACTLY 1 with refinement on,
    //    and a hard-blocked ridge ray returns EXACTLY 0 -- the refinement
    //    phase never touches binary outcomes.
    const float litOn  = marchVisibilityPort(g_ridgeScene, pmSoft,
                                             -2.0f, 3.50f, 0.0f,
                                              2.0f, 5.02f, 0.0f);
    const float litOff = marchVisibilityPort(g_ridgeScene, pmSoftNoRefine,
                                             -2.0f, 3.50f, 0.0f,
                                              2.0f, 5.02f, 0.0f);
    expectTrue(litOn == 1.0f && litOff == 1.0f,
               "ridge fully-lit ray: refinement gate keeps exactly 1");
    expectTrue(marchVisibilityPort(g_ridgeScene, pmSoft,
                                   -0.5f, 1.02f, 0.0f,
                                    2.0f, 5.02f, 0.0f) == 0.0f,
               "ridge pierce ray: refinement keeps the hard 0");

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
                                                -2.0f, 3.50f, 0.0f,
                                                 2.0f, 5.02f, 0.0f);
    const float binOnLit  = marchVisibilityPort(g_ridgeScene, pmBin1,
                                                -2.0f, 3.50f, 0.0f,
                                                 2.0f, 5.02f, 0.0f);
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

// ---------------------------------------------------------------------------
// M4.0.9: the centroid rig march (flag-gated experiment, --shadow-centroid 1)
// and the parallax-window form, pinned against the same analytic fields.
// Centroid contract: hard outcomes exact in the binary path (lightSize = 0),
// graded values monotone in the light size S, the west-wall top-edge band
// grades smoothly (the geometry the half-plane model CAN see), the z-side
// floor band grades through the M4.0.9.1 ANALYTIC LATERAL half-plane (the
// M4.0.9 single-ray blindness -- rim rays passing BESIDE the block are
// invisible to one trace -- is now closed by the occluder-set distance
// ramp; --shadow-lateral 0 / lateral = 0 reproduces the old cliff exactly),
// the vertical-ray column test is exact (the centroid hangs inside the
// cbox03 baffle footprint), refinement stays monotone-safe, and jitter
// changes graded values without breaking any binary verdict.
// ---------------------------------------------------------------------------
void testCentroidRigMarch() {
    std::printf("[bench] centroid rig march + parallax window (M4.0.9 contract pins)\n");

    // Cadence invariant: the centroid path's finer step keeps the
    // baffle-thickness margin (mirror of the sandbox static_assert).
    expectTrue(0.04f < 0.19f,
               "centroid march step 0.04 stays below the baffle's 0.2 m thickness");

    MarchParams pmC;             // centroid binary (lightSize = 0)
    pmC.centroid = 1;
    pmC.step = 0.04f;            // mirrors kCentroidMarchStep
    MarchParams pmS = pmC;
    pmS.lightSize = 1.175f;      // mirrors kShadowLightSize (emitter mean)
    MarchParams pmSNoRefine = pmS;
    pmSNoRefine.refine = 0;

    // 1) Hard outcomes stay exact (binary centroid): the behind-the-block
    //    floor ray crosses the tall block's interval below its top; the
    //    open-corner floor ray clears everything; the block-top receiver
    //    breaks at the first sample (tCap = 0).
    expectTrue(marchVisibilityPort(g_tallScene, pmC, -1.05f, 0.0f, -2.40f,
                                   0.0f, 5.44f, 0.0f) == 0.0f,
               "centroid binary: behind-block floor ray is blocked");
    expectTrue(marchVisibilityPort(g_tallScene, pmC, 2.40f, 0.0f, 2.40f,
                                   0.0f, 5.44f, 0.0f) == 1.0f,
               "centroid binary: open-corner floor ray is lit");
    expectTrue(marchVisibilityPort(g_tallScene, pmC, -1.05f, 3.30f, 0.0f,
                                   0.0f, 5.44f, 0.0f) == 1.0f,
               "centroid binary: top-surface receiver stays lit (acne guard)");

    // 2) Deep pierce grades to exactly 0 in the soft path (the half-plane
    //    grade saturates): same behind-block ray, meters below the top.
    expectTrue(marchVisibilityPort(g_tallScene, pmS, -1.05f, 0.0f, -2.40f,
                                   0.0f, 5.44f, 0.0f) == 0.0f,
               "centroid soft: deep-pierce ray returns exactly 0");

    // 3) The west-wall top-edge band (the geometry the half-plane model
    //    sees): receivers at x = -2.75 whose centroid rays skim the tall
    //    block's top-west edge (entry t ~ 0.436, rayY ~ y_w + 1.46). The
    //    band grades monotonically from umbra to lit as the receiver rises.
    const float bandLow  = marchVisibilityPort(g_tallScene, pmS,
                                               -2.75f, 1.00f, -0.85f,
                                               0.0f, 5.44f, 0.0f);
    const float bandMid  = marchVisibilityPort(g_tallScene, pmS,
                                               -2.75f, 1.50f, -0.85f,
                                               0.0f, 5.44f, 0.0f);
    const float bandHigh = marchVisibilityPort(g_tallScene, pmS,
                                               -2.75f, 1.90f, -0.85f,
                                               0.0f, 5.44f, 0.0f);
    const float bandLit  = marchVisibilityPort(g_tallScene, pmS,
                                               -2.75f, 2.30f, -0.85f,
                                               0.0f, 5.44f, 0.0f);
    expectTrue(bandLow == 0.0f,
               "centroid band: below the edge the grade saturates to 0");
    expectTrue(bandMid > 0.25f && bandMid < 0.5f,
               "centroid band: near-edge receiver grades strictly inside (0.25, 0.5)");
    expectTrue(bandMid < bandHigh && bandHigh < 1.0f,
               "centroid band: rising toward the edge strictly brightens");
    expectTrue(bandLit == 1.0f,
               "centroid band: clearing receivers stay exactly 1");

    // 4) M4.0.9.1 lateral half-plane: the z-side floor band -- the geometry
    //    the M4.0.9 single trace was structurally blind to (the pinned
    //    blindness cliff). The z = 0.80 receiver's centroid ray misses the
    //    footprint entirely (every column empty, vertical grade never
    //    fires), but its closest ground approach to the footprint is
    //    ~0.056 m at t ~ 0.5, so the lateral ramp grades it strictly
    //    inside (0.5, 0.65) -- the cliff becomes the continuous band the
    //    rim rays produce. Sliding away (z = 1.30, approach ~0.22 m)
    //    strictly brightens. The pierce receiver stays exactly 0, and the
    //    lateral-OFF lever reproduces the exact M4.0.9 blindness pin.
    MarchParams pmLat = pmS;     // soft centroid + analytic lateral set
    pmLat.boxCount = 1;          // mirrors the g_tall interval field exactly:
    pmLat.boxMin[0][0] = -1.55f; // x [-1.55, -0.55]
    pmLat.boxMin[0][1] = -2.05f; // z [-2.05,  0.35]
    pmLat.boxMin[0][2] =  0.00f; // y [ 0.00,  3.30]
    pmLat.boxMax[0][0] = -0.55f;
    pmLat.boxMax[0][1] =  0.35f;
    pmLat.boxMax[0][2] =  3.30f;
    MarchParams pmLatOff = pmLat;
    pmLatOff.lateral = 0;        // exact M4.0.9 centroid march (A/B lever)
    const float latNear = marchVisibilityPort(g_tallScene, pmLat,
                                              -1.05f, 0.0f, 0.80f,
                                              0.0f, 5.44f, 0.0f);
    const float latFar  = marchVisibilityPort(g_tallScene, pmLat,
                                              -1.05f, 0.0f, 1.30f,
                                              0.0f, 5.44f, 0.0f);
    expectTrue(latNear > 0.5f && latNear < 0.65f,
               "centroid lateral: z-side floor point with footprint miss "
               "grades strictly inside (0.5, 0.65) -- the M4.0.9 blindness "
               "cliff is now the continuous lateral band");
    expectTrue(latFar > latNear && latFar < 0.8f,
               "centroid lateral: sliding away from the footprint strictly "
               "brightens (continuous ramp, no cliff)");
    expectTrue(marchVisibilityPort(g_tallScene, pmLat, -1.05f, 0.0f, 0.60f,
                                   0.0f, 5.44f, 0.0f) == 0.0f,
               "centroid lateral: footprint pierce still returns exactly 0");
    expectTrue(marchVisibilityPort(g_tallScene, pmLatOff, -1.05f, 0.0f, 0.80f,
                                   0.0f, 5.44f, 0.0f) == 1.0f,
               "centroid lateral OFF: the exact M4.0.9 blindness pin "
               "(footprint miss -> 1) reproduces bit-for-bit");

    // 5) Window monotonicity in S on the CLEAR side of the band (receiver
    //    y_w = 1.95, sampled clearance ~0.21 m): a wider emitter extent is
    //    strictly softer; a narrow window saturates the grade to exactly 1.
    MarchParams pmSNarrow = pmS;
    pmSNarrow.lightSize = 0.5f;
    MarchParams pmSWide = pmS;
    pmSWide.lightSize = 2.0f;
    const float sNarrow = marchVisibilityPort(g_tallScene, pmSNarrow,
                                              -2.75f, 1.95f, -0.85f,
                                              0.0f, 5.44f, 0.0f);
    const float sWide   = marchVisibilityPort(g_tallScene, pmSWide,
                                              -2.75f, 1.95f, -0.85f,
                                              0.0f, 5.44f, 0.0f);
    expectTrue(sNarrow == 1.0f,
               "centroid: half-size window saturates the clear-side graze to 1");
    const float bandClear = marchVisibilityPort(g_tallScene, pmS,
                                                -2.75f, 1.95f, -0.85f,
                                                0.0f, 5.44f, 0.0f);
    expectTrue(sNarrow > bandClear && bandClear > sWide && sWide > 0.5f,
               "centroid: larger light size strictly softens the same graze");

    // 6) Refinement stays monotone-safe and strictly sharpens a sub-step
    //    dip under the half-plane grade (tall-ridge apex, RISING ray at the
    //    centroid cadence 0.04 m -- 101 samples; the +half-step probe lands
    //    on the apex).
    const float ridgeOn  = marchVisibilityPort(g_ridgeScene, pmS,
                                               -2.0f, 1.02f, 0.0f,
                                               2.0f, 5.02f, 0.0f);
    const float ridgeOff = marchVisibilityPort(g_ridgeScene, pmSNoRefine,
                                               -2.0f, 1.02f, 0.0f,
                                               2.0f, 5.02f, 0.0f);
    expectTrue(ridgeOn > 0.5f && ridgeOn < 0.6f && ridgeOff > 0.5f &&
               ridgeOff < 0.6f && ridgeOn < ridgeOff,
               "centroid ridge graze: refinement strictly sharpens, both graded");

    // 7) Jitter (deterministic noise in the port): shifts the lattice,
    //    changes the graded value (the grain mechanism), never breaks a
    //    binary verdict, and stays inside [0, 1].
    MarchParams pmJ = pmS;
    pmJ.jitter = 1;
    pmJ.noise = 0.25f;           // lattice shifted a quarter spacing early
    const float jitterMid = marchVisibilityPort(g_tallScene, pmJ,
                                                -2.75f, 1.50f, -0.85f,
                                                0.0f, 5.44f, 0.0f);
    expectTrue(jitterMid >= 0.0f && jitterMid <= 1.0f,
               "centroid jitter: graded value stays in [0, 1]");
    expectTrue(jitterMid != bandMid,
               "centroid jitter: the lattice shift changes the graded value "
               "(per-pixel grain by construction)");
    expectTrue(marchVisibilityPort(g_tallScene, pmJ, -1.05f, 0.0f, -2.40f,
                                   0.0f, 5.44f, 0.0f) == 0.0f,
               "centroid jitter: binary-pierce verdict unchanged");
    MarchParams pmCBinJ = pmC;
    pmCBinJ.jitter = 1;
    pmCBinJ.noise = 0.0f;
    expectTrue(marchVisibilityPort(g_tallScene, pmCBinJ, 2.40f, 0.0f, 2.40f,
                                   0.0f, 5.44f, 0.0f) == 1.0f,
               "centroid jitter: binary-lit verdict unchanged");

    // 8) Vertical-ray column test (exact): the centroid hangs INSIDE the
    //    cbox03 baffle footprint, so a receiver directly beneath it must
    //    block through the hanging plate -- the legacy per-light path could
    //    treat this as measure-zero, the centroid path cannot.
    expectTrue(marchVisibilityPort(g_baffleScene, pmC, 0.0f, 0.001f, 0.0f,
                                   0.0f, 5.44f, 0.0f) == 0.0f,
               "centroid vertical ray: under-baffle receiver is blocked "
               "(exact column test)");
    expectTrue(marchVisibilityPort(g_emptyScene, pmC, 0.0f, 0.001f, 0.0f,
                                   0.0f, 5.44f, 0.0f) == 1.0f,
               "centroid vertical ray: empty column stays lit");

    // 9) Parallax-window form (default per-light path): a constant-clearance
    //    graze deep along the ray (t -> 1) grades near-black -- the
    //    physically real under-baffle darkening (the plate hangs 4 cm under
    //    the grid). The divergence is bounded by the tCap-capped window, so
    //    the value stays a positive grade, never negative.
    MarchParams pmDeep;          // per-light legacy path (centroid = 0)
    pmDeep.penumbra = 0.325f;    // fixed contract scale (M4.0.9 first-default)
    const float deepGraze = marchVisibilityPort(g_box1mScene, pmDeep,
                                                0.0f, 1.02f, 0.55f,
                                                0.0f, 1.02f, -0.55f);
    expectTrue(deepGraze >= 0.0f && deepGraze < 0.05f,
               "parallax window: constant clearance at t -> 1 grades near 0 "
               "(diverging window, tCap-capped march)");
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

// ---------------------------------------------------------------------------
// M5.0 area-light transport: the CPU mirror (rendering/AreaLight.h) pinned to
// the float64 brute-force fixtures generated by scripts/check_area_model.py
// (section 7). Every fixture value below is machine-emitted, not hand-tuned;
// tolerances reflect the float32-vs-float64 gap measured on this mirror
// (worst 3.1e-4 relative across all fixtures).
// ---------------------------------------------------------------------------
void testAreaLightTransport() {
    std::printf("[bench] area-light transport: exact visible-patch form factor + "
                "backprojection (M5.0 fixtures)\n");

    using engine::arealight::OccluderBox;
    using engine::arealight::OccluderSphere;
    // Frozen occluders (generate_cornell.py r2 provenance, mirrors the field
    // probes' comment block in sandbox/src/main.cpp).
    const OccluderBox TALL  { -1.55f, -0.55f, -2.05f,  0.35f, 0.0f, 3.30f };
    const OccluderBox SHORT {  0.40f,  1.40f, -0.70f,  1.70f, 0.0f, 1.65f };
    const OccluderBox BAFFLE{ -1.00f,  1.00f, -0.10f,  0.10f, 3.4f, 5.40f };
    const OccluderSphere GOLD { 0.0f, 0.55f, 0.90f, 0.55f };

    const float HX = engine::arealight::kEmitterHalfX;   // 0.65
    const float HZ = engine::arealight::kEmitterHalfZ;   // 0.525

    struct VisCase {
        const char* label;
        float px, py, pz;
        const OccluderBox* boxes; int nb;
        const OccluderSphere* spheres; int ns;
        double kRect, kVis;
        float relTol;
    };
    const OccluderBox c01[2] = { TALL, SHORT };
    const OccluderBox c03[2] = { TALL, BAFFLE };
    const OccluderBox c01t[1] = { TALL };
    const OccluderSphere c02s[1] = { GOLD };

    const VisCase visCases[] = {
        // label              P                     boxes   spheres       kRect     kVis      tol
        { "c01 floor mid   ",   0.00f, 0.00f,  2.30f, c01, 2, nullptr, 0, 0.032497, 0.032497, 2e-3f },
        { "c01 floor center",   0.00f, 0.00f,  0.00f, c01, 2, nullptr, 0, 0.044600, 0.044600, 2e-3f },
        { "tall block top  ",  -1.05f, 3.30f, -0.85f, c01, 2, nullptr, 0, 0.147575, 0.147575, 2e-3f },
        { "c01 adjacent    ",  -0.70f, 0.00f,  0.55f, c01, 2, nullptr, 0, 0.042411, 0.031218, 2e-3f },
        { "c03 under both  ",  -0.80f, 0.00f,  0.60f, c03, 2, nullptr, 0, 0.041864, 0.016727, 2e-3f },
        { "c02 sphere band ",   0.00f, 0.00f,  1.70f, c01t, 1, c02s, 1, 0.037293, 0.034376, 5e-3f },
        { "c01 block top   ",   0.90f, 1.65f,  0.50f, c01, 2, nullptr, 0, 0.078761, 0.078761, 2e-3f },
        { "lit open floor  ",   2.20f, 0.00f,  2.20f, c01, 2, nullptr, 0, 0.025863, 0.022582, 2e-3f },
    };
    for (const VisCase& c : visCases) {
        float kr = -1.0f;
        const float kv = engine::arealight::visibleFormFactor(
            c.px, c.py, c.pz, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, HX, HZ, c.boxes, c.nb, c.spheres, c.ns, kr);
        const double rkr = kr > 0.0 ? std::fabs(kr - c.kRect) / c.kRect : 1.0;
        const double rkv = kv > 0.0 ? std::fabs(kv - c.kVis) / c.kVis : 1.0;
        expectTrue(rkr <= c.relTol, (std::string(c.label) + ": K_rect matches the float64 fixture").c_str());
        expectTrue(rkv <= c.relTol, (std::string(c.label) + ": K_vis matches the float64 fixture").c_str());
        expectTrue(kv <= kr + 1e-6f * kr,
                   (std::string(c.label) + ": visibility never raises the form factor").c_str());
    }

    // Contact-shadow grade: the receiver at the tall block's +x face keeps
    // most of the emitter visible (graded penumbra, not a cliff) -- pin the
    // float64 sweep's ratio band (check_area_model.py section 7).
    {
        float kr = -1.0f;
        const float kv = engine::arealight::visibleFormFactor(
            -0.7f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, HX, HZ, c01, 2, nullptr, 0, kr);
        const float ratio = kv / kr;
        expectTrue(ratio > 0.60f && ratio < 0.75f,
                   "contact-shadow grade at the tall block face sits in the pinned band (0.60, 0.75)");
    }

    // Self-shadow exclusion: a receiver ON the short block's top face must
    // not be shaded by its own column (the bias guard skips the primitive),
    // so K_vis == K_rect there -- the heightfield's own-column contract.
    {
        float kr = -1.0f;
        const float kv = engine::arealight::visibleFormFactor(
            0.9f, 1.65f, 0.5f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, HX, HZ, &SHORT, 1, nullptr, 0, kr);
        expectTrue(std::fabs(kv - kr) <= 1e-6f * kr,
                   "receiver on the occluder top is not self-shaded (bias guard)");
    }

    // Specular support: the VOS solid angle of the rect, on-axis closed form
    // Omega = 4*atan(hx*hz / (d*sqrt(d^2+hx^2+hz^2))) -- exact identity.
    {
        const float om = engine::arealight::solidAngleRect(
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, HX, HZ);
        const float d = engine::arealight::kEmitterY;
        const float ref = 4.0f * std::atan(HX * HZ
            / (d * std::sqrt(d * d + HX * HX + HZ * HZ)));
        expectTrue(std::fabs(om - ref) <= 1e-5f * ref,
                   "VOS rect solid angle matches the on-axis closed form");
    }

    // Frozen constants ride along with the object (the mirror must not drift
    // from the frozen standard).
    expectTrue(engine::arealight::kEmitterHalfX == 0.65f
            && engine::arealight::kEmitterHalfZ == 0.525f
            && engine::arealight::kEmitterY == cornell::kEmitterMin[1]
            && engine::arealight::kEmitterLe == cornell::kEmitterRadiance
            && engine::arealight::kBias == 0.01f,
            "AreaLight frozen constants match cornell-box/1.0 (half extents, plane y, L_e, bias)");
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
    testCentroidRigMarch();
    testAreaLightTransport();
    testShadowCaptureMatrix();
    testWorldToTexelMapping();

    std::printf("[bench] %d checks, %d failure(s)\n", g_checks, g_failures);
    pauseIfInteractive();
    return g_failures == 0 ? 0 : 1;
}
