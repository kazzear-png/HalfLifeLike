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

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

} // namespace

int main() {
    std::printf("=== engine benchmark tests (M3.3, cornell-box/1.0) ===\n");
    testFrozenConstants();
    testLightGridFluxModel();
    testFrozenGeometryOnDisk();
    testFrozenNormalOrientation();

    std::printf("[bench] %d checks, %d failure(s)\n", g_checks, g_failures);
    pauseIfInteractive();
    return g_failures == 0 ? 0 : 1;
}
