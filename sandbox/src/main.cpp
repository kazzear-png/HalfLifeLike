// Milestone 4: M3 demo scene + the frozen cornell-box/1.0 benchmark scenes.
//
// M4 change: cornell scenes gain HEIGHTFIELD SHADOWS -- a one-time top-down
// capture of the occluders' max world Y (engine::ShadowHeightfield), marched
// per point light in the PBR shader. --no-shadows restores the exact M3.3
// direct-only behavior for A/B ledger comparisons.
// Demo scene (M1/M2 objects remain the regression foundation):
//   - reference floor + spinning quad, shaded with the PBR pipeline
//   - imported OBJ "hero" model; keys 1-4 swap the bundled assets
//   - a directional sun + two orbiting point lights (drawn as glowing bulbs)
//   - a camera flashlight (F key), material views (V key), scroll exposure
//
// Cornell Box benchmark (M3.3): --scene cornell01|cornell02|cornell03
//   The frozen two-axis laboratory: fixed camera, fixed lighting (point-light
//   grid approximating the area emitter), fixed exposure, no input. Run with
//   --benchmark N for a deterministic telemetry run (VSync off, warmup
//   discarded, FPS / frame time / CPU time / GPU timer query / draw calls /
//   triangles / lights), then compare the frame against the reference path
//   trace with tools/benchmark_compare.py. See benchmarks/cornell_box/README.md.
//
// Render flow per frame:
//   linear HDR scene -> RGBA16F MSAA target -> resolve -> exposure -> ACES
//   tonemap -> sRGB -> present  (HDR pipeline lives in engine::Renderer)
//
// Verification / CI hook:
//   sandbox --frames N --out shot.ppm
//     renders N frames (deterministic 30 Hz timeline), writes the final
//     post-tonemap backbuffer as a binary PPM, then exits with code 0.

#include "core/Application.h"
#include "assets/OBJ.h"
#include "math/Mat4.h"
#include "math/Vec3.h"
#include "rendering/Camera.h"
#include "rendering/Lighting.h"
#include "rendering/Mesh.h"
#include "rendering/Shader.h"
#include "rendering/ShadowHeightfield.h"
#include "shaders.h"
#include "cornell_scene_gen.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iterator>   // std::begin/std::end (M4.0.4 probe table splice; MSVC does not leak this)
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>   // _getcwd (double-click launches: CWD = exe dir)
#else
#include <unistd.h>   // getcwd
#endif

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

std::string dirOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? std::string(".") : path.substr(0, slash);
}

// Locates the bundled model assets. Works no matter how the sandbox is
// launched: from an IDE (VS debugger CWD = project dir), by double-clicking
// the exe (CWD = build/bin/Debug on MSVC multi-config), or from a terminal.
// Strategy: probe the model dir at the CWD, at the exe dir, and at every
// ancestor of both -- any in-project build layout lands within a few steps.
std::string findModelDir(const char* argv0, std::string* searchedLog = nullptr) {
    auto probeDir = [](const std::string& dir) -> bool {
        const std::string f = dir + "/torus.obj";
        if (FILE* file = std::fopen(f.c_str(), "rb")) {
            std::fclose(file);
            return true;
        }
        return false;
    };

    auto stripLeaf = [](const std::string& p) -> std::string {
        const std::size_t slash = p.find_last_of("/\\");
        return (slash == std::string::npos) ? std::string() : p.substr(0, slash);
    };

    // Current working directory (portable).
    std::string cwd = ".";
    {
        char buf[1024];
#ifdef _WIN32
        if (_getcwd(buf, sizeof(buf)) != nullptr) cwd = buf;
#else
        if (getcwd(buf, sizeof(buf)) != nullptr) cwd = buf;
#endif
    }
    std::string exeDir = dirOf(argv0 != nullptr ? argv0 : "");

    // Seed queue: CWD and exe dir first, then every ancestor of each.
    std::vector<std::string> roots = { cwd, exeDir };
    for (std::size_t i = 0; i < roots.size(); ++i) {          // BFS by ancestry
        std::string parent = stripLeaf(roots[i]);
        if (!parent.empty() && parent != roots[i]) {
            roots.push_back(parent);
        }
        if (i > 64) break;                                     // paranoia bound
    }

    for (const std::string& root : roots) {
        if (root.empty()) continue;
        const std::string candidate = root + "/sandbox/assets/models";
        if (searchedLog) *searchedLog += "  probed: " + candidate + "\n";
        if (probeDir(candidate)) {
            return candidate;
        }
        // Also accept a bare assets/models layout (installed / relocated tree).
        const std::string flat = root + "/assets/models";
        if (probeDir(flat)) {
            return flat;
        }
    }
    return std::string();
}

// Locates the benchmark geometry dir (benchmarks/cornell_box/geometry) with
// the same strategy as findModelDir: probe the CWD, the exe dir, and every
// ancestor of both. Two deliberately separate helpers -- findModelDir is the
// M3.1 hotfix path and stays untouched.
std::string findBenchmarkDir(const char* argv0, std::string* searchedLog = nullptr) {
    auto probeDir = [](const std::string& dir) -> bool {
        const std::string f = dir + "/ceiling_emitter.obj";
        if (FILE* file = std::fopen(f.c_str(), "rb")) {
            std::fclose(file);
            return true;
        }
        return false;
    };

    auto stripLeaf = [](const std::string& p) -> std::string {
        const std::size_t slash = p.find_last_of("/\\");
        return (slash == std::string::npos) ? std::string() : p.substr(0, slash);
    };

    std::string cwd = ".";
    {
        char buf[1024];
#ifdef _WIN32
        if (_getcwd(buf, sizeof(buf)) != nullptr) cwd = buf;
#else
        if (getcwd(buf, sizeof(buf)) != nullptr) cwd = buf;
#endif
    }
    std::string exeDir = dirOf(argv0 != nullptr ? argv0 : "");

    std::vector<std::string> roots = { cwd, exeDir };
    for (std::size_t i = 0; i < roots.size(); ++i) {
        std::string parent = stripLeaf(roots[i]);
        if (!parent.empty() && parent != roots[i]) {
            roots.push_back(parent);
        }
        if (i > 64) break;
    }

    for (const std::string& root : roots) {
        if (root.empty()) continue;
        const std::string candidate = root + "/benchmarks/cornell_box/geometry";
        if (searchedLog) *searchedLog += "  probed: " + candidate + "\n";
        if (probeDir(candidate)) {
            return candidate;
        }
    }
    return std::string();
}

// Procedural UV sphere (used for the visible light bulbs, so the demo does
// not depend on imported assets for its own lighting visualization).
engine::Mesh createSphereMesh(float radius, int slices, int stacks) {
    std::vector<engine::Vertex> verts;
    std::vector<std::uint32_t>  indices;
    verts.reserve(static_cast<std::size_t>((slices + 1) * (stacks + 1)));

    for (int s = 0; s <= stacks; ++s) {
        const float v = static_cast<float>(s) / static_cast<float>(stacks);
        const float phi = v * 3.14159265359f;
        for (int p = 0; p <= slices; ++p) {
            const float u = static_cast<float>(p) / static_cast<float>(slices);
            const float theta = u * 2.0f * 3.14159265359f;

            engine::Vertex vert{};
            vert.nx = std::sin(phi) * std::cos(theta);
            vert.ny = std::cos(phi);
            vert.nz = std::sin(phi) * std::sin(theta);
            vert.x = radius * vert.nx;
            vert.y = radius * vert.ny;
            vert.z = radius * vert.nz;
            vert.r = 1.0f; vert.g = 1.0f; vert.b = 1.0f;
            verts.push_back(vert);
        }
    }
    for (int s = 0; s < stacks; ++s) {
        for (int p = 0; p < slices; ++p) {
            const std::uint32_t a = static_cast<std::uint32_t>(s * (slices + 1) + p);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + static_cast<std::uint32_t>(slices) + 1;
            const std::uint32_t d = c + 1;
            indices.insert(indices.end(), { a, c, b, b, c, d });
        }
    }

    engine::Mesh mesh;
    if (!mesh.create(verts.data(), static_cast<std::uint32_t>(verts.size()),
                     indices.data(), static_cast<std::uint32_t>(indices.size()))) {
        std::fprintf(stderr, "[Sandbox] Failed to create bulb sphere mesh.\n");
    }
    return mesh;
}

bool writePpm(const std::string& path, const unsigned char* rgba, int width, int height) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        std::fprintf(stderr, "[Sandbox] Cannot open '%s' for writing.\n", path.c_str());
        return false;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", width, height);
    // ReadPixels returns bottom-up rows; PPM expects top-down.
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const unsigned char* px = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
            std::fwrite(px, 1, 3, f);  // drop alpha
        }
    }
    std::fclose(f);
    return true;
}

// ---------------------------------------------------------------------------
// Scene description
// ---------------------------------------------------------------------------

struct ModelAsset {
    const char* filename;
    const char* label;
    engine::PbrMaterial material;
};

ModelAsset kModelAssets[4] = {
    // albedo                rough  metal  vertexColor
    { "torus.obj",  "polished gold torus",  { { 1.00f, 0.77f, 0.34f }, 0.16f, 1.00f, false } },
    { "sphere.obj", "chrome sphere",        { { 0.95f, 0.96f, 0.98f }, 0.09f, 1.00f, false } },
    { "rock.obj",   "rough asteroid",       { { 0.52f, 0.48f, 0.45f }, 0.92f, 0.00f, false } },
    // Paint is a dielectric (clearcoat over pigment); metalness 0.15 was a
    // pre-PBR habit and now trips the material validation view (V key).
    { "ship.obj",   "painted hull",         { { 0.82f, 0.24f, 0.18f }, 0.32f, 0.00f, false } },
};

// ---------------------------------------------------------------------------
// Cornell Box benchmark runtime (M3.3, cornell-box/1.0 -- FROZEN standard)
// ---------------------------------------------------------------------------

struct CornellScene {
    const cornell::VariantDef* def = nullptr;
    std::vector<engine::Mesh>  meshes;        // parallel to def->meshes
    std::vector<bool>          meshLoaded;
    engine::Mesh               emitterQuad;   // unlit emissive draw
    bool                       emitterReady = false;
};

const cornell::MaterialDef* findCornellMaterial(const char* name) {
    for (std::uint32_t i = 0; i < cornell::kMaterialCount; ++i) {
        if (std::strcmp(cornell::kMaterialTable[i].name, name) == 0) {
            return &cornell::kMaterialTable[i].def;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// M4 heightfield shadows -- frozen-standard constants (scene.json "box")
// ---------------------------------------------------------------------------

// Room footprint of cornell-box/1.0. Mirrors tools/generate_cornell.py; kept
// OUT of the generated header so that file stays byte-identical to the pinned
// r2 revision (bench_tests pins values, docs pin the md5 story).
constexpr float kRoomHalfX = 2.75f;
constexpr float kRoomHalfZ = 2.75f;
// Occluder tops per variant: the shader's march early-outs above this height.
constexpr float kOccluderTopBoxes  = 3.30f;   // tall block
constexpr float kOccluderTopBaffle = 5.40f;   // cbox03 baffle (reaches y=5.4)
// March step in world meters. Safety invariant: the XZ spacing between
// consecutive march samples is at most kMarchStep (triangle inequality), so
// any occluder thicker than kMarchStep along a crossing ray is sampled at
// least once. The thinnest occluder in the frozen scene is the cbox03
// baffle: 0.2 m thick in z. 0.08 < 0.2 keeps it un-leakable WITH room to
// spare; bench_tests pins the same invariant at runtime.
// M4.0.6: 0.16 -> 0.08 (single change; nothing else touched). The first lit
// march (M4.0.5, ledger ACCEPTED) exposed the march step as the shadow
// boundary's dominant quantizer: the staircase band pitch tracked the 0.16 m
// sample cadence = 7.4 field texels per step (the field itself is
// 5.5 m / 256 = 21.5 mm/texel, and the samplers are already GL_LINEAR, so
// filtering is NOT the quantizer). Halving the step doubles the march's
// texture taps; at M4.0.5 the whole frame cost 0.583 ms GPU at 960x540
// against a 16.7 ms 60 fps budget, so the extra cost is expected to be
// small -- but it is MEASURED in the ledger row (both axes), not assumed.
// Floor of useful refinement: ~0.04 m (~1.9 texels) -- steps finer than the
// field texel pitch buy no information the capture cannot represent.
constexpr float kMarchStep  = 0.08f;
static_assert(kMarchStep < 0.19f,
              "march step must stay below the baffle's 0.2 m thickness or "
              "thin occluders become leakable");
constexpr float kShadowBias = 0.01f;          // ~half a texel height quantum

// M4.0.7: soft-penumbra scale (dimensionless). The 16-light rig is a 4x4
// QUADRATURE of the area emitter: adjacent lights sit kLightPitch = 0.325 m
// apart (x/z in {+-0.1625, +-0.4875}) at height kLightHeight = 5.44 m
// (frozen grid, cornell_scene_gen.h). A point-sample quadrature of an area
// light under-reconstructs the continuum by a blur of about
// (pitch/2) * t / height at an occluder t meters along the ray -- so
// instead of adding light samples (M5's job) or march samples (M4.0.6's
// linear-cost lever), apply that blur where the march already has every
// input it needs: the per-sample clearance d and the traveled distance t.
// Horizon-style soft accumulation costs a few ALU per sample and ZERO extra
// texture taps; it converts the staircase boundary into a graded edge.
// Derived, not tuned: kShadowPenumbra = 0.5 * 0.325 / 5.44 ~= 0.0299.
// 0.0 disables the soft path entirely and reproduces the M4.0.6 binary
// march byte-for-byte (--shadow-penumbra 0 is the A/B + regression pin).
constexpr float kLightPitch     = 0.325f;  // frozen 4x4 grid spacing
constexpr float kLightHeight    = 5.44f;   // frozen grid plane y
constexpr float kShadowPenumbra = 0.5f * kLightPitch / kLightHeight;

// M4.0.8: bracket refinement of the soft minimum (default ON). Research
// verdict (docs/SHADOW_EDGE_REFERENCES.md): every shipped heightfield-ray
// solver is two-phase -- a coarse linear search brackets the feature, a
// refinement phase resolves it (GPU Gems 3 ch.18 names the bare linear
// search "prone to aliasing", which is the staircase the ledger measured).
// The refinement engages ONLY where the M4.0.7 window fired (vis < 1), so
// fully lit and hard-blocked pixels pay nothing and every binary decision
// (penumbra = 0) is untouched: the --shadow-penumbra 0 md5-replay pins
// still hold. --shadow-refine 0 reproduces the M4.0.7 soft march exactly.
constexpr int kShadowRefineDefault = 1;

// --- M4.0.4 field REGISTRATION probes (frozen) -----------------------------
// verifyField() proves the field's aggregate content (coverage %, top,
// interval validity) but not WHERE the content sits. A displaced, mirrored,
// or transposed field passes all of those statistics while every shadow ray
// misses it (M4.0.3 hardware evidence: verify OK + march enabled + image
// byte-identical to --no-shadows). These probes read the captured interval
// at specific frozen world points; the sentinel probes sit OUTSIDE every
// real occluder footprint but INSIDE the footprint a flipped/swapped
// capture would have -- any mirroring failure mode flips exactly one
// sentinel from empty to occluder.
//
// Provenance: footprints from tools/generate_cornell.py (frozen r2):
//   tall  x -1.55..-0.55  z -2.05.. 0.35  y 0..3.30
//   short x  0.40.. 1.40  z -0.70.. 1.70  y 0..1.65
//   spheres (cbox02) r 0.55 at (±1.70, 1.55) and (0, 0.90), tops 1.10
//   baffle (cbox03) x -1.00..1.00 z -0.10..0.10 y 3.40..5.40
struct FieldProbeSpec {
    const char* label;
    float x, z;
    bool  expectOccluder;  // false: hMax must read empty (<= kEmptyMax)
    float expMax;          // expected hMax at the probe (occluder probes)
    float expMin;          // expected hMin (underside; 0 = floor-resting)
};
constexpr float kProbeTopTol = 0.15f;  // same band as verifyField's top check
constexpr float kProbeEmptyMax = 0.25f;  // same occupancy threshold as verifyField

// cbox01 base set (always run; cbox02/03 share these points -- the sentinels
// and corners are clear of the sphere/baffle footprints too).
constexpr FieldProbeSpec kFieldProbesBase[] = {
    // Core registration: the two block centers must read their frozen tops.
    { "tall block center",  -1.05f, -0.85f, true,  3.30f, 0.00f },
    { "short block center",  0.90f,  0.50f, true,  1.65f, 0.00f },
    // Mirror-X sentinel: inside the X-MIRRORED tall footprint, outside
    // every real one (short block starts at z -0.70 > -0.85).
    { "mirror-X sentinel",   1.05f, -0.85f, false, 0.0f,  0.00f },
    // Mirror-Z sentinel: inside the Z-MIRRORED tall footprint (z -0.35..2.05),
    // outside every real one (nearest sphere center 0.955 m > 0.55).
    { "mirror-Z sentinel",  -1.05f,  0.85f, false, 0.0f,  0.00f },
    // Transpose sentinel: a square room makes an X<->Z swap coverage- and
    // top-invisible. This point is inside the TRANSPOSED tall footprint but
    // outside the real one -- the probe a transposed capture fails.
    { "transpose sentinel", -2.00f, -1.05f, false, 0.0f,  0.00f },
    // Plain empty corners (dodging the cbox02 sphere footprints: nearest
    // center 0.96 m > 0.55).
    { "empty corner NE",     2.30f,  2.30f, false, 0.0f,  0.00f },
    { "empty corner NW",    -2.30f,  2.30f, false, 0.0f,  0.00f },
};
// cbox02 extra: gold sphere center column must read the sphere's top.
constexpr FieldProbeSpec kFieldProbeSphere =
    { "gold sphere center",  0.00f,  0.90f, true,  1.10f, 0.00f };
// cbox03 extra: the hanging baffle is the ONE probe whose underside is not
// the floor -- it validates the MIN capture (hMin ~3.40, not ~0).
constexpr FieldProbeSpec kFieldProbeBaffle =
    { "baffle center",       0.00f,  0.00f, true,  5.40f, 3.40f };

// Room shell never occludes the ceiling rig; everything else does.
bool isOccluderMaterial(const char* mat) {
    return std::strncmp(mat, "wall", 4) != 0
        && std::strcmp(mat, "floor") != 0
        && std::strncmp(mat, "ceiling", 7) != 0;
}

bool variantHasBaffle(const cornell::VariantDef* def) {
    for (std::uint32_t i = 0; i < def->meshCount; ++i) {
        if (std::strcmp(def->meshes[i].material, "baffle") == 0) {
            return true;
        }
    }
    return false;
}

// One-time capture pair of every occluder's vertical interval [minY, maxY]
// per footprint texel (max surface from above, min surface from below).
// Contract (3-state):
//   return false                     -> capture unavailable, shadows off
//   return true  + *verifiedOut==false -> captured field FAILED acceptance
//   return true  + *verifiedOut==true  -> valid, verified shadow field
// M4.0.2: on success the captured field is also READ BACK AND VERIFIED
// against the frozen footprints; *verifiedOut carries the verdict. The
// capture path is hardware GL -- M4.0.1 showed an inert field is
// indistinguishable from --no-shadows in the image, so the field must
// prove itself before the telemetry is allowed to say "shadows: on".
// M4.0.3: BUGFIX -- the verifyField() verdict never reached *verifiedOut
// (it was only returned), so hardware captures that verified OK were
// reported as FAILED by the caller and shadows were silently disabled.
bool captureCornellHeightfield(engine::ShadowHeightfield& hf,
                               const CornellScene& scene,
                               engine::Renderer& renderer,
                               bool* verifiedOut) {
    *verifiedOut = false;
    if (!hf.create(/*resolution=*/256, /*minY=*/-0.5f, /*maxY=*/6.0f)) {
        return false;
    }
    for (int pass = 0; pass < 2; ++pass) {
        hf.beginCapture(/*maxPass=*/pass == 0,
                        -kRoomHalfX, kRoomHalfX, -kRoomHalfZ, kRoomHalfZ);
        for (std::uint32_t i = 0; i < scene.def->meshCount; ++i) {
            if (!scene.meshLoaded[i]) {
                continue;
            }
            if (!isOccluderMaterial(scene.def->meshes[i].material)) {
                continue;
            }
            hf.setModel(engine::Mat4::identity());
            renderer.drawIndexed(scene.meshes[i]);
        }
        hf.endCapture();
    }

    // Expected coverage from the FROZEN footprints (pinned by bench_tests):
    //   tall 1.0x2.4 + short 1.0x2.4 = 4.8 m2; spheres 3*pi*0.55^2 = 2.85 m2;
    //   baffle 2.0x0.2 = 0.4 m2; room 5.5x5.5 = 30.25 m2.
    const bool hasBaffle = variantHasBaffle(scene.def);
    bool hasSpheres = false;
    for (std::uint32_t i = 0; i < scene.def->meshCount; ++i) {
        if (std::strncmp(scene.def->meshes[i].material, "sphere_", 7) == 0) {
            hasSpheres = true;
        }
    }
    const float occM2 = 4.8f + (hasSpheres ? 2.85f : 0.0f) + (hasBaffle ? 0.4f : 0.0f);
    const float expectedCoveragePct = 100.0f * occM2 / (5.5f * 5.5f);
    const bool verified = hf.verifyField(
        hasBaffle ? kOccluderTopBaffle : kOccluderTopBoxes,
        expectedCoveragePct - 5.0f, expectedCoveragePct + 5.0f);

    // M4.0.3: propagate the verdict through verifiedOut as the caller-side
    // contract documents. The return value only answers "was a capture made
    // at all?" -- conflating the two here meant a PASSING hardware field
    // still left shadowsActive==false and the march was never enabled.
    *verifiedOut = verified;
    return true;
}

// M4.0.4: read the captured fields back and check WHERE the content sits,
// at the frozen probe points (kFieldProbes* above). Returns true iff every
// probe passed. Each probe uses ShadowHeightfield::worldToTexel -- the same
// mapping the GLSL march uses -- so a PASS means "the texel the march reads
// at this world point really contains the frozen interval". Aggregate
// statistics (verifyField) cannot catch a mirrored/transposed field in a
// square room; these sentinels can.
bool runFieldRegistrationProbes(const cornell::VariantDef* def,
                                const engine::ShadowHeightfield& hf) {
    std::vector<float> fmax, fmin;
    const unsigned res = hf.resolution();
    if (!hf.readbackHeights(fmax, fmin) || res == 0) {
        std::fprintf(stderr,
            "[ShadowHeightfield] registration probes SKIPPED (readback failed) "
            "-- shadows disabled\n");
        return false;
    }

    bool hasSpheres = false;
    for (std::uint32_t i = 0; i < def->meshCount; ++i) {
        if (std::strncmp(def->meshes[i].material, "sphere_", 7) == 0) {
            hasSpheres = true;
        }
    }
    std::vector<FieldProbeSpec> probes;
    probes.insert(probes.end(), std::begin(kFieldProbesBase),
                                std::end(kFieldProbesBase));
    if (hasSpheres)          probes.push_back(kFieldProbeSphere);
    if (variantHasBaffle(def)) probes.push_back(kFieldProbeBaffle);

    int passed = 0;
    for (const FieldProbeSpec& p : probes) {
        float colF = 0.0f, rowF = 0.0f;
        if (!engine::ShadowHeightfield::worldToTexel(
                p.x, p.z, -kRoomHalfX, kRoomHalfX, -kRoomHalfZ, kRoomHalfZ,
                res, &colF, &rowF)) {
            std::fprintf(stderr,
                "[ShadowHeightfield] probe '%s': point outside footprint "
                "(probe-table bug)\n", p.label);
            continue;
        }
        const int col = std::min<int>(int(res) - 1, std::max(0, int(colF)));
        const int row = std::min<int>(int(res) - 1, std::max(0, int(rowF)));
        const float hMax = fmax[std::size_t(row) * res + std::size_t(col)];
        const float hMin = fmin[std::size_t(row) * res + std::size_t(col)];

        char expect[80];
        bool ok;
        if (p.expectOccluder) {
            ok = std::fabs(hMax - p.expMax) <= kProbeTopTol
              && std::fabs(hMin - p.expMin) <= kProbeTopTol;
            std::snprintf(expect, sizeof(expect),
                          "expect hMax %.2f hMin %.2f", double(p.expMax), double(p.expMin));
        } else {
            ok = hMax <= kProbeEmptyMax;
            std::snprintf(expect, sizeof(expect),
                          "expect empty (hMax <= %.2f)", double(kProbeEmptyMax));
        }
        passed += ok ? 1 : 0;
        if (ok) {
            std::printf("[ShadowHeightfield] probe '%s' @ (%.2f, %.2f) texel (%d, %d): "
                        "hMax %.3f hMin %.3f (%s) PASS\n",
                        p.label, double(p.x), double(p.z), col, row,
                        double(hMax), double(hMin), expect);
        } else {
            std::fprintf(stderr,
                "[ShadowHeightfield] probe '%s' @ (%.2f, %.2f) texel (%d, %d): "
                "hMax %.3f hMin %.3f (%s) FAIL -- CAPTURE IS MISREGISTERED\n",
                p.label, double(p.x), double(p.z), col, row,
                double(hMax), double(hMin), expect);
        }
    }
    const int expected = static_cast<int>(probes.size());
    if (passed == expected) {
        std::printf("[ShadowHeightfield] registration probes: %d/%d PASS\n",
                    passed, expected);
    } else {
        std::fprintf(stderr,
            "[ShadowHeightfield] REGISTRATION FAILED: %d/%d probes PASS "
            "-- shadows disabled, telemetry will say so\n", passed, expected);
    }
    return passed == expected;
}

// M4.0.4: render the captured fields DIRECTLY through the march's sampler
// wiring -- bindTextures(maxUnit, minUnit) + setInt("uShadowHeightsMax"/"Min")
// + the same footprint uniforms -- one texel per pixel, raw linear gray, NO
// tonemap. Reads <prefix>_hmax.ppm / <prefix>_hmin.ppm. Discriminates:
//   dump black / displaced -> the field or its SAMPLER path is the bug;
//   dump shows the footprints in place -> field fine, march logic/uniforms.
// The draw goes through renderer.drawIndexed on the floor quad transformed
// by the capture's own ortho, so it exercises the same GL state sequence as
// the PBR draw (a state clobber between bind and draw reproduces here).
void dumpHeightfieldFields(const CornellScene& scene,
                           const engine::ShadowHeightfield& hf,
                           const std::string& prefix,
                           engine::Renderer& renderer) {
    int floorIndex = -1;
    for (std::uint32_t i = 0; i < scene.def->meshCount; ++i) {
        if (scene.meshLoaded[i]
                && std::strcmp(scene.def->meshes[i].material, "floor") == 0) {
            floorIndex = static_cast<int>(i);
            break;
        }
    }
    if (floorIndex < 0) {
        std::fprintf(stderr,
            "[Sandbox] --dump-heightfield: floor mesh unavailable; dump skipped\n");
        return;
    }
    engine::Shader dumpProgram = engine::Shader::fromSource(
        shaders::kHeightfieldDumpVertex, shaders::kHeightfieldDumpFragment);
    if (!dumpProgram.valid()) {
        std::fprintf(stderr,
            "[Sandbox] --dump-heightfield: program failed to compile; dump skipped\n");
        return;
    }

    const unsigned res    = hf.resolution();
    const float    norm   = variantHasBaffle(scene.def) ? kOccluderTopBaffle
                                                        : kOccluderTopBoxes;
    std::vector<unsigned char> px(std::size_t(res) * std::size_t(res) * 4u);

    // Raw backbuffer, depth off, viewport = capture resolution (1 px = 1
    // texel). FBO 0 is still bound from the capture's endCapture().
    engine::gl::Disable(engine::gl::DepthTest);
    renderer.setViewport(0, 0, static_cast<int>(res), static_cast<int>(res));
    engine::gl::ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    engine::gl::Clear(engine::gl::ColorBufferBit);

    dumpProgram.bind();
    dumpProgram.setMat4("uModel", engine::Mat4::identity());
    // The capture's own down-looking ortho maps the room footprint onto the
    // full clip square: the floor quad covers every pixel, 1 px = 1 texel.
    dumpProgram.setMat4("uViewProj", hf.captureViewProjection(/*maxPass=*/true));
    dumpProgram.setFloat3("uShadowFootprintMin", -kRoomHalfX, -kRoomHalfZ, 0.0f);
    dumpProgram.setFloat3("uShadowFootprintMax",  kRoomHalfX,  kRoomHalfZ, 0.0f);
    dumpProgram.setFloat("uDumpNorm", norm);
    dumpProgram.setFloat("uDumpRes", static_cast<float>(res));

    const char* fieldTag[2] = { "hmax", "hmin" };
    for (int f = 0; f < 2; ++f) {
        // The march's EXACT sampler wiring order, every field.
        hf.bindTextures(/*maxUnit=*/0, /*minUnit=*/1);
        dumpProgram.setInt("uShadowHeightsMax", 0);
        dumpProgram.setInt("uShadowHeightsMin", 1);
        dumpProgram.setInt("uDumpField", f);
        renderer.drawIndexed(scene.meshes[std::size_t(floorIndex)]);
        if (!renderer.readBackbufferPixels(int(res), int(res), px.data())) {
            std::fprintf(stderr, "[Sandbox] heightfield dump: GL readback error (%s)\n",
                         fieldTag[f]);
        }
        const std::string path = prefix + "_" + fieldTag[f] + ".ppm";
        if (writePpm(path, px.data(), int(res), int(res))) {
            std::printf("[Sandbox] heightfield dump: %s (%ux%u, linear gray, "
                        "0..%.2f m, black = empty)\n",
                        path.c_str(), res, res, double(norm));
        } else {
            std::fprintf(stderr, "[Sandbox] heightfield dump FAILED: %s\n",
                         path.c_str());
        }
    }

    // Restore the state the frame loop assumes (viewport is re-set per frame;
    // depth test and clear color are restored here).
    engine::gl::Enable(engine::gl::DepthTest);
    renderer.setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

// Flat quad from 4 corners (CCW seen from -Y for the emitter underside).
engine::Mesh createEmitterQuad() {
    const float x0 = cornell::kEmitterMin[0], x1 = cornell::kEmitterMax[0];
    const float y  = cornell::kEmitterMin[1];
    const float z0 = cornell::kEmitterMin[2], z1 = cornell::kEmitterMax[2];
    // Corners ordered CCW seen from below: cross(v1-v0, v2-v0) = (0,-1,0),
    // matching the authored normal and ceiling_emitter.obj. M3.3.1 rule for
    // the frozen scene: winding, vn, and emission side all agree. (The engine
    // does not backface-cull and this quad draws unlit, so this is data
    // hygiene, not rendering behavior.)
    const engine::Vertex verts[4] = {
        { x0, y, z0,   0.0f, -1.0f, 0.0f,   1, 1, 1 },
        { x1, y, z0,   0.0f, -1.0f, 0.0f,   1, 1, 1 },
        { x1, y, z1,   0.0f, -1.0f, 0.0f,   1, 1, 1 },
        { x0, y, z1,   0.0f, -1.0f, 0.0f,   1, 1, 1 },
    };
    const std::uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    engine::Mesh m;
    if (!m.create(verts, 4, idx, 6)) {
        std::fprintf(stderr, "[Sandbox] failed to create emitter quad.\n");
        return m;
    }
    return m;
}

bool setupCornellScene(CornellScene& scene, int variantIndex,
                       const std::string& geoDir) {
    scene.def = cornell::kVariants[variantIndex];
    scene.meshes.resize(scene.def->meshCount);
    scene.meshLoaded.assign(scene.def->meshCount, false);

    engine::LoadObjOptions opts;
    opts.centerToOrigin = false;   // the standard is authored in world space
    opts.targetRadius   = 0.0f;    // never rescale the frozen geometry

    bool allOk = true;
    for (std::uint32_t i = 0; i < scene.def->meshCount; ++i) {
        const char* file = scene.def->meshes[i].file;
        const std::string path = geoDir + "/" + file;
        engine::LoadObjResult model = engine::loadOBJ(path, opts);
        if (!model.ok) {
            std::fprintf(stderr, "[Sandbox] cornell: %s: %s\n", path.c_str(),
                         model.error.c_str());
            allOk = false;
            continue;
        }
        if (!model.warnings.empty()) {
            std::fprintf(stderr, "[Sandbox] cornell: %s: %s\n", path.c_str(),
                         model.warnings.c_str());
        }
        scene.meshLoaded[i] = scene.meshes[i].create(
            model.vertices.data(), static_cast<std::uint32_t>(model.vertices.size()),
            model.indices.data(), static_cast<std::uint32_t>(model.indices.size()));
        allOk = allOk && scene.meshLoaded[i];
        std::printf("[Sandbox] cornell: %s: %d triangles\n", file, model.triangleCount);
    }

    scene.emitterQuad = createEmitterQuad();
    scene.emitterReady = scene.emitterQuad.valid();
    allOk = allOk && scene.emitterReady;
    return allOk;
}

} // namespace

int main(int argc, char** argv) {
    // --- CLI ----------------------------------------------------------------
    int  screenshotFrames = 0;
    std::string screenshotPath = "sandbox.ppm";
    int  benchmarkFrames = 0;          // --benchmark N: deterministic perf/similarity run
    std::string sceneName = "demo";    // --scene demo|cornell01|cornell02|cornell03
    std::string reportPath;            // --report path (benchmark telemetry file)
    int  windowWidth = 1280, windowHeight = 720;
    bool shadowsEnabled = true;        // --no-shadows: exact M3.3 behavior (A/B)
    float shadowPenumbra = kShadowPenumbra;  // --shadow-penumbra overrides; 0 = binary
    float marchStepOverride = kMarchStep;    // --shadow-step overrides (A/B lever)
    int  shadowRefine = kShadowRefineDefault; // --shadow-refine 0|1 (M4.0.8, soft path only)
    std::string dumpHeightfieldPrefix; // --dump-heightfield p: write <p>_hmax/_hmin.ppm
    int  shadowDebugMode = 0;          // --shadow-debug vis|field|uv (M4.0.5 instrument)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            screenshotFrames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            screenshotPath = argv[++i];
        } else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc) {
            sceneName = argv[++i];
        } else if (std::strcmp(argv[i], "--benchmark") == 0 && i + 1 < argc) {
            benchmarkFrames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--report") == 0 && i + 1 < argc) {
            reportPath = argv[++i];
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            windowWidth = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            windowHeight = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--no-shadows") == 0) {
            shadowsEnabled = false;
        } else if (std::strcmp(argv[i], "--dump-heightfield") == 0 && i + 1 < argc) {
            dumpHeightfieldPrefix = argv[++i];
        } else if (std::strcmp(argv[i], "--shadow-penumbra") == 0 && i + 1 < argc) {
            shadowPenumbra = static_cast<float>(std::atof(argv[++i]));
            if (shadowPenumbra < 0.0f) {
                std::fprintf(stderr,
                             "[Sandbox] --shadow-penumbra: negative scale '%s' "
                             "(use 0 = binary march, or a positive scale)\n",
                             argv[i]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--shadow-step") == 0 && i + 1 < argc) {
            marchStepOverride = static_cast<float>(std::atof(argv[++i]));
            if (marchStepOverride <= 0.0f || marchStepOverride >= 0.19f) {
                std::fprintf(stderr,
                             "[Sandbox] --shadow-step: '%s' out of range "
                             "(0 < step < 0.19, the baffle-thickness invariant)\n",
                             argv[i]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--shadow-refine") == 0 && i + 1 < argc) {
            shadowRefine = std::atoi(argv[++i]);
            if (shadowRefine < 0 || shadowRefine > 1) {
                std::fprintf(stderr,
                             "[Sandbox] --shadow-refine: '%s' out of range "
                             "(0 = exact M4.0.7 soft march, 1 = M4.0.8 bracket "
                             "refinement)\n",
                             argv[i]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--shadow-debug") == 0 && i + 1 < argc) {
            const char* mode = argv[++i];
            if (std::strcmp(mode, "vis") == 0) {
                shadowDebugMode = 1;
            } else if (std::strcmp(mode, "field") == 0) {
                shadowDebugMode = 2;
            } else if (std::strcmp(mode, "uv") == 0) {
                shadowDebugMode = 3;
            } else {
                std::fprintf(stderr,
                             "[Sandbox] --shadow-debug: unknown mode '%s' (use vis|field|uv)\n",
                             mode);
                return 1;
            }
        }
    }

    int cornellVariant = -1;           // index into cornell::kVariants, -1 == demo
    if (sceneName != "demo") {
        for (std::uint32_t v = 0; v < cornell::kVariantCount; ++v) {
            if (sceneName == cornell::kVariants[v]->name) {
                cornellVariant = static_cast<int>(v);
            }
        }
        if (cornellVariant < 0) {
            std::fprintf(stderr, "[Sandbox] unknown scene '%s' (use demo", sceneName.c_str());
            for (std::uint32_t v = 0; v < cornell::kVariantCount; ++v) {
                std::fprintf(stderr, "|%s", cornell::kVariants[v]->name);
            }
            std::fprintf(stderr, ")\n");
            return 1;
        }
    }
    const bool benchMode = benchmarkFrames > 0;
    if (benchMode && screenshotFrames == 0) {
        screenshotFrames = benchmarkFrames;   // a benchmark run also captures its final frame
    }

    // --- window + engine boot ----------------------------------------------
    engine::WindowDesc desc;
    desc.title  = "Sandbox - M3 PBR Lighting + OBJ Import";
    desc.width  = windowWidth;
    desc.height = windowHeight;
    // Benchmarks measure the renderer, not the driver's present queue: VSync off.
    desc.vsync  = !benchMode;

    engine::Application app(desc);
    if (!app.valid()) {
        std::fprintf(stderr, "Engine initialization failed; see messages above.\n");
        return 1;
    }

    engine::Renderer& renderer = app.renderer();

    // HDR pipeline: RGBA16F + 4x MSAA + ACES tonemap. Falls back gracefully
    // (1x MSAA, then direct rendering) on limited drivers.
    int fbw = 0, fbh = 0;
    app.window().getFramebufferSize(fbw, fbh);
    renderer.initHDR(fbw, fbh, /*msaaSamples=*/4);

    // Cornell scenes are a closed light-transport system: black background
    // (rays that leave through the open front see nothing), fixed exposure,
    // and GPU frame timing for the benchmark report.
    if (cornellVariant >= 0) {
        renderer.setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.setExposure(cornell::kExposure);
        renderer.enableGpuTiming(benchMode);
    }

    // --- shaders ------------------------------------------------------------
    engine::Shader pbrShader  = engine::Shader::fromSource(shaders::kPbrVertex, shaders::kPbrFragment);
    engine::Shader unlitShader = engine::Shader::fromSource(shaders::kUnlitVertex, shaders::kUnlitFragment);
    if (!pbrShader.valid() || !unlitShader.valid()) {
        std::fprintf(stderr, "Failed to build shaders; see messages above.\n");
        return 1;
    }

    // --- base geometry: floor + quad (M1/M2 regression targets, now lit) ----
    const engine::Vertex floorVerts[] = {
        { -6.0f, -0.75f, -6.0f,   0.0f, 1.0f, 0.0f,   0.30f, 0.32f, 0.36f },
        {  6.0f, -0.75f, -6.0f,   0.0f, 1.0f, 0.0f,   0.30f, 0.32f, 0.36f },
        {  6.0f, -0.75f,  6.0f,   0.0f, 1.0f, 0.0f,   0.30f, 0.32f, 0.36f },
        { -6.0f, -0.75f,  6.0f,   0.0f, 1.0f, 0.0f,   0.30f, 0.32f, 0.36f },
    };
    const std::uint32_t floorIndices[] = { 0, 1, 2, 0, 2, 3 };

    const engine::Vertex quadVerts[] = {
        { -0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   0.92f, 0.26f, 0.21f },  // bottom-left  red
        {  0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   0.30f, 0.69f, 0.31f },  // bottom-right green
        {  0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   0.26f, 0.58f, 0.97f },  // top-right     blue
        { -0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   0.99f, 0.75f, 0.19f },  // top-left      yellow
    };
    const std::uint32_t quadIndices[] = { 0, 1, 2, 0, 2, 3 };

    engine::Mesh floorMesh;
    engine::Mesh quadMesh;
    if (!floorMesh.create(floorVerts, 4, floorIndices, 6) ||
        !quadMesh.create(quadVerts, 4, quadIndices, 6)) {
        std::fprintf(stderr, "Failed to create base meshes.\n");
        return 1;
    }

    // --- imported models (keys 1-4) ------------------------------------------
    std::string modelSearchLog;
    const std::string modelDir = findModelDir(argc > 0 ? argv[0] : nullptr, &modelSearchLog);
    engine::Mesh modelMeshes[4];
    bool modelLoaded[4] = { false, false, false, false };

    if (modelDir.empty()) {
        std::fprintf(stderr,
                     "[Sandbox] Model assets not found; keys 1-4 disabled, floor + quad only.\n"
                     "[Sandbox] Expected 'sandbox/assets/models/*.obj' somewhere above the exe or CWD. Searched:\n%s"
                     "[Sandbox] Fix: launch from the project root, or keep the extracted zip layout intact.\n",
                     modelSearchLog.c_str());
    } else {
        for (int i = 0; i < 4; ++i) {
            const std::string path = modelDir + "/" + kModelAssets[i].filename;
            engine::LoadObjOptions opts;
            opts.centerToOrigin = true;
            opts.targetRadius   = 0.6f;  // consistent display size for any OBJ

            engine::LoadObjResult model = engine::loadOBJ(path, opts);
            if (!model.ok) {
                std::fprintf(stderr, "[Sandbox] %s: %s\n", path.c_str(), model.error.c_str());
                continue;
            }
            if (!model.warnings.empty()) {
                std::fprintf(stderr, "[Sandbox] %s: %s\n", path.c_str(), model.warnings.c_str());
            }
            modelLoaded[i] = modelMeshes[i].create(model.vertices.data(),
                                                   static_cast<std::uint32_t>(model.vertices.size()),
                                                   model.indices.data(),
                                                   static_cast<std::uint32_t>(model.indices.size()));
            std::printf("[Sandbox] %s (%s): %d vertices, %d triangles\n",
                        kModelAssets[i].label, kModelAssets[i].filename,
                        static_cast<int>(model.vertices.size()), model.triangleCount);
        }
    }

    // --- light bulbs (procedural spheres drawn with the unlit shader) -------
    engine::Mesh bulbMesh = createSphereMesh(0.07f, 16, 12);

    // --- cornell benchmark scene (M3.3) --------------------------------------
    CornellScene cornell;
    engine::ShadowHeightfield cornellHeightfield;   // M4; destroyed before `app`
    bool shadowsActive = false;
    if (cornellVariant >= 0) {
        std::string benchSearchLog;
        const std::string geoDir = findBenchmarkDir(argc > 0 ? argv[0] : nullptr, &benchSearchLog);
        if (geoDir.empty()) {
            std::fprintf(stderr,
                         "[Sandbox] cornell geometry not found; expected "
                         "'benchmarks/cornell_box/geometry' above the exe or CWD. Searched:\n%s"
                         "[Sandbox] Fix: launch from the project root, or keep the extracted zip layout intact.\n",
                         benchSearchLog.c_str());
            return 1;
        }
        if (!setupCornellScene(cornell, cornellVariant, geoDir)) {
            std::fprintf(stderr, "[Sandbox] cornell scene '%s' failed to load.\n", sceneName.c_str());
            return 1;
        }

        // M4: one-time height capture for the shadow march. The capture
        // clobbers GL clear color; re-issue the scene's clear color after.
        if (!shadowsEnabled) {
            std::printf("[Sandbox] shadows disabled (--no-shadows): M3.3 direct-only mode\n");
        } else if (captureCornellHeightfield(cornellHeightfield, cornell, renderer, &shadowsActive)) {
            // shadowsActive carries the FIELD VERIFICATION verdict: an inert
            // field means no shadows, and telemetry must not claim them.
            if (!shadowsActive) {
                std::fprintf(stderr, "[Sandbox] shadow field verification FAILED; continuing without shadows\n");
            } else {
                // M4.0.4: aggregate statistics pass for a displaced, mirrored,
                // or transposed field; the registration probes check WHERE the
                // content is before the march is allowed to claim shadows.
                shadowsActive = runFieldRegistrationProbes(cornell.def, cornellHeightfield);
            }
        } else {
            std::fprintf(stderr, "[Sandbox] heightfield unavailable; continuing without shadows\n");
        }
        // M4.0.4 instrument: dump the captured fields through the march's
        // sampler wiring REGARDLESS of the verdict -- seeing where the
        // content went (or that none made it through the sampler path) is
        // exactly what this mode is for.
        if (!dumpHeightfieldPrefix.empty()) {
            dumpHeightfieldFields(cornell, cornellHeightfield,
                                  dumpHeightfieldPrefix, renderer);
        }
        renderer.setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // Point-light grid radiance for the cornell scenes (kLightCount must fit
    // the engine's uniform array; bench_tests pins this).
    std::array<float, 3 * engine::kMaxPointLights> cornellLightPos{};
    std::array<float, 3 * engine::kMaxPointLights> cornellLightRad{};
    if (cornellVariant >= 0) {
        for (std::uint32_t i = 0; i < cornell::kLightCount && i < engine::kMaxPointLights; ++i) {
            cornellLightPos[3 * i + 0] = cornell::kLightPositions[3 * i + 0];
            cornellLightPos[3 * i + 1] = cornell::kLightPositions[3 * i + 1];
            cornellLightPos[3 * i + 2] = cornell::kLightPositions[3 * i + 2];
            cornellLightRad[3 * i + 0] = cornell::kLightIntensity;
            cornellLightRad[3 * i + 1] = cornell::kLightIntensity;
            cornellLightRad[3 * i + 2] = cornell::kLightIntensity;
        }
    }

    // --- camera ----------------------------------------------------------------
    engine::Camera camera;
    if (cornellVariant >= 0) {
        // Frozen benchmark camera: fixed position/orientation, no input.
        camera.setPosition(engine::Vec3(cornell::kCameraPosition[0],
                                        cornell::kCameraPosition[1],
                                        cornell::kCameraPosition[2]));
        camera.setYaw(cornell::kCameraYawRadians);
        camera.setPitch(cornell::kCameraPitchRadians);
    } else {
        camera.setPosition(engine::Vec3(0.0f, 0.6f, 2.8f));
        camera.setPitch(-0.22f);  // slight downward tilt: floor + quad + model in frame
    }

    constexpr float kMouseSensitivity = 0.0025f;  // radians per pixel
    constexpr float kMoveSpeed        = 3.0f;     // units per second

    // --- lights -----------------------------------------------------------------
    engine::DirectionalLight sun;
    sun.direction = engine::normalize(engine::Vec3(0.40f, 0.75f, 0.30f));  // toward the sun
    sun.color     = engine::Vec3(1.00f, 0.96f, 0.90f);
    sun.intensity = 3.0f;

    engine::AmbientTerms ambient;
    ambient.sky    = engine::Vec3(0.060f, 0.070f, 0.090f);
    ambient.ground = engine::Vec3(0.020f, 0.020f, 0.025f);

    struct OrbitLight {
        engine::Vec3 color;
        float intensity;
    };
    constexpr int kOrbitLightCount = 2;
    const OrbitLight kOrbitLights[kOrbitLightCount] = {
        { engine::Vec3(1.00f, 0.50f, 0.20f), 6.0f },  // warm orange
        { engine::Vec3(0.25f, 0.60f, 1.00f), 5.0f },  // cyan
    };
    std::array<float, 3 * engine::kMaxPointLights> pointPos{};
    std::array<float, 3 * engine::kMaxPointLights> pointRadiance{};

    engine::SpotLight flashlight;
    flashlight.color     = engine::Vec3(1.00f, 0.92f, 0.80f);
    flashlight.intensity = 8.0f;
    flashlight.innerCutoffDegrees = 8.0f;
    flashlight.outerCutoffDegrees = 16.0f;
    bool flashlightOn = true;

    // --- state -----------------------------------------------------------------
    int   heroIndex = 0;      // key 1-4 selection
    int   debugMode = 0;      // key V cycles material debug / validation views
    float exposure  = 1.0f;
    float time      = 0.0f;   // scene clock (deterministic in screenshot mode)
    float spinAngle = 0.0f;   // quad accumulator: pause freezes, does not reset
    bool  spinning  = true;

    constexpr float kOrbitRadius = 2.0f;
    constexpr float kOrbitHeight = 0.9f;

    static const char* kDebugModeNames[] = {
        "shaded (final image)",
        "world normals",
        "albedo (linear)",
        "metalness (R) / roughness (G)",
        "F0 (reflectance at normal incidence)",
        "material validation (red = violation)",
    };

    std::printf("[Sandbox] Controls: click=look, WASD+QE=fly, 1-4=model, F=flashlight, "
                "V=material views, SPACE=pause spin, ESC=quit, scroll=exposure\n");
    if (cornellVariant >= 0) {
        std::printf("[Sandbox] cornell scene '%s': fixed camera/lights/exposure "
                    "(cornell-box/1.0, FROZEN standard; input disabled)\n",
                    cornell.def->title);
    }

    // --- benchmark bookkeeping -------------------------------------------------
    const int warmupFrames = benchMode ? std::max(10, benchmarkFrames / 10) : 0;
    std::vector<float> framePeriods;   // entry-to-entry (includes present)
    std::vector<float> cpuTimes;       // callback work time
    std::vector<float> gpuTimes;       // renderer timer query (one-frame lag)
    framePeriods.reserve(static_cast<std::size_t>(std::max(0, benchmarkFrames)));
    cpuTimes.reserve(framePeriods.capacity());
    gpuTimes.reserve(framePeriods.capacity());
    std::chrono::steady_clock::time_point lastEntry = std::chrono::steady_clock::now();

    std::uint64_t frameIndex = 0;

    app.run([&](float dt) {
        engine::Input& in = app.input();

        const auto entryTime = std::chrono::steady_clock::now();
        const bool measured = benchMode && (frameIndex >= static_cast<std::uint64_t>(warmupFrames));
        if (measured) {
            framePeriods.push_back(
                std::chrono::duration<float, std::milli>(entryTime - lastEntry).count());
        }
        lastEntry = entryTime;

        if (cornellVariant >= 0) {
            // Frozen laboratory: fully deterministic, input-free. ESC still quits.
            if (in.pressed(engine::Key::Escape)) {
                app.window().requestClose();
            }
        } else {
            // Screenshot mode advances on a deterministic 30 Hz timeline so
            // headless runs (no real vsync pacing) still produce a meaningful frame.
            const float step = (screenshotFrames > 0) ? (1.0f / 30.0f) : dt;
            time += step;
            if (spinning) {
                spinAngle += step * 0.8f;
            }

            // --- input -------------------------------------------------------
            if (in.pressed(engine::Key::Escape)) {
                if (in.cursorLocked()) {
                    in.setCursorMode(engine::CursorMode::Normal);
                } else {
                    app.window().requestClose();
                }
            }
            if (!in.cursorLocked() && in.mousePressed(engine::MouseButton::Left)) {
                in.setCursorMode(engine::CursorMode::Locked);
            }
            if (in.pressed(engine::Key::Space)) {
                spinning = !spinning;
            }
            if (in.pressed(engine::Key::F)) {
                flashlightOn = !flashlightOn;
                std::printf("[Sandbox] flashlight %s\n", flashlightOn ? "ON" : "OFF");
            }
            if (in.pressed(engine::Key::V)) {
                debugMode = (debugMode + 1) % 6;
                std::printf("[Sandbox] material view: %s\n", kDebugModeNames[debugMode]);
            }
            for (int k = 0; k < 4; ++k) {
                if (in.pressed(static_cast<engine::Key>(static_cast<int>(engine::Key::Digit1) + k))) {
                    if (modelLoaded[k]) {
                        heroIndex = k;
                        std::printf("[Sandbox] display model: %s\n", kModelAssets[k].label);
                    } else {
                        std::printf("[Sandbox] model %d failed to load; keeping current\n", k + 1);
                    }
                }
            }

            if (const float scroll = in.scrollDelta(); scroll != 0.0f) {
                exposure *= std::pow(1.15f, scroll);
                exposure = std::min(std::max(exposure, 0.05f), 20.0f);
                renderer.setExposure(exposure);
                std::printf("[Sandbox] exposure = %.3f\n", static_cast<double>(exposure));
            }

            if (in.cursorLocked()) {
                camera.addYaw(in.mouseDX() * kMouseSensitivity);
                camera.addPitch(-in.mouseDY() * kMouseSensitivity);
            }

            // --- fly movement ------------------------------------------------
            engine::Vec3 move(0.0f, 0.0f, 0.0f);
            if (in.isKeyDown(engine::Key::W)) move = move + camera.forward();
            if (in.isKeyDown(engine::Key::S)) move = move - camera.forward();
            if (in.isKeyDown(engine::Key::D)) move = move + camera.right();
            if (in.isKeyDown(engine::Key::A)) move = move - camera.right();
            if (in.isKeyDown(engine::Key::E)) move = move + engine::Vec3(0.0f, 1.0f, 0.0f);
            if (in.isKeyDown(engine::Key::Q)) move = move - engine::Vec3(0.0f, 1.0f, 0.0f);
            if (engine::length(move) > 0.0f) {
                camera.setPosition(camera.position() + engine::normalize(move) * (kMoveSpeed * dt));
            }

            // --- orbiting point lights ----------------------------------------
            for (int i = 0; i < kOrbitLightCount; ++i) {
                const float angle = time * 0.7f + (i * 3.14159265f);
                const float x = std::cos(angle) * kOrbitRadius;
                const float z = std::sin(angle) * kOrbitRadius;
                const float y = kOrbitHeight + std::sin(time * 1.3f + i) * 0.15f;
                pointPos[3 * i + 0] = x;
                pointPos[3 * i + 1] = y;
                pointPos[3 * i + 2] = z;
                pointRadiance[3 * i + 0] = kOrbitLights[i].color.x * kOrbitLights[i].intensity;
                pointRadiance[3 * i + 1] = kOrbitLights[i].color.y * kOrbitLights[i].intensity;
                pointRadiance[3 * i + 2] = kOrbitLights[i].color.z * kOrbitLights[i].intensity;
            }
            for (int i = kOrbitLightCount; i < engine::kMaxPointLights; ++i) {
                pointPos[3 * i + 0] = 0.0f; pointPos[3 * i + 1] = 0.0f; pointPos[3 * i + 2] = 0.0f;
                pointRadiance[3 * i + 0] = 0.0f; pointRadiance[3 * i + 1] = 0.0f; pointRadiance[3 * i + 2] = 0.0f;
            }
        }

        // --- camera projection (resize-safe) ------------------------------------
        app.window().getFramebufferSize(fbw, fbh);
        const float aspect = fbh > 0 ? static_cast<float>(fbw) / static_cast<float>(fbh) : 1.0f;
        if (cornellVariant >= 0) {
            camera.setPerspective(cornell::kCameraFovYRadians, aspect,
                                  cornell::kCameraNear, cornell::kCameraFar);
        } else {
            camera.setPerspective(0.7854f /* 45 deg */, aspect, 0.1f, 100.0f);
        }
        const engine::Mat4 vp = camera.viewProjection();

        renderer.setViewport(0, 0, fbw, fbh);
        if (fbw > 0 && fbh > 0) {
            renderer.resizeHDR(fbw, fbh);  // no-op unless the size changed
        }

        // --- frame ---------------------------------------------------------------
        renderer.beginFrame();

        pbrShader.bind();
        pbrShader.setMat4("uViewProj", vp);
        pbrShader.setFloat3("uViewPos", camera.position());
        pbrShader.setInt("uDebugMode", cornellVariant >= 0 ? 0 : debugMode);

        if (cornellVariant >= 0) {
            // Closed light-transport system: no sun, no ambient, no flashlight.
            // All illumination comes from the frozen point-light grid that
            // approximates the area emitter (see scene.json for the flux model).
            pbrShader.setFloat3("uSunDirection", 0.0f, 1.0f, 0.0f);
            pbrShader.setFloat3("uSunColor", 0.0f, 0.0f, 0.0f);
            pbrShader.setFloat("uSunIntensity", 0.0f);
            pbrShader.setFloat3("uAmbientSky", 0.0f, 0.0f, 0.0f);
            pbrShader.setFloat3("uAmbientGround", 0.0f, 0.0f, 0.0f);
            pbrShader.setInt("uPointCount", static_cast<int>(cornell::kLightCount));
            pbrShader.setFloat3Array("uPointPos", cornellLightPos.data(), engine::kMaxPointLights);
            pbrShader.setFloat3Array("uPointRadiance", cornellLightRad.data(), engine::kMaxPointLights);
            pbrShader.setInt("uSpotOn", 0);
            pbrShader.setFloat3("uSpotPos", 0.0f, 0.0f, 0.0f);
            pbrShader.setFloat3("uSpotDir", 0.0f, -1.0f, 0.0f);
            pbrShader.setFloat3("uSpotRadiance", 0.0f, 0.0f, 0.0f);
            pbrShader.setFloat("uSpotInnerCos", 1.0f);
            pbrShader.setFloat("uSpotOuterCos", 0.5f);

            // M4 heightfield shadows (see shaders.h for the march contract).
            pbrShader.setInt("uShadowOn", shadowsActive ? 1 : 0);
            // M4.0.5 debug instrument, independent of shadowsActive so the
            // panel stays readable even when the march is disabled. uShadowDebugNorm
            // is the frozen capture top (field-mode R normalizer).
            pbrShader.setInt("uShadowDebug", shadowDebugMode);
            pbrShader.setFloat("uShadowDebugNorm",
                               variantHasBaffle(cornell.def) ? kOccluderTopBaffle
                                                             : kOccluderTopBoxes);
            if (shadowsActive) {
                cornellHeightfield.bindTextures(/*maxUnit=*/0, /*minUnit=*/1);
                pbrShader.setInt("uShadowHeightsMax", 0);
                pbrShader.setInt("uShadowHeightsMin", 1);
                // XZ packed into vec3 (loader carries no Uniform2f entry).
                pbrShader.setFloat3("uShadowFootprintMin", -kRoomHalfX, -kRoomHalfZ, 0.0f);
                pbrShader.setFloat3("uShadowFootprintMax",  kRoomHalfX,  kRoomHalfZ, 0.0f);
                pbrShader.setFloat("uShadowMaxHeight",
                                   variantHasBaffle(cornell.def) ? kOccluderTopBaffle : kOccluderTopBoxes);
                pbrShader.setFloat("uShadowStep", marchStepOverride);
                pbrShader.setFloat("uShadowBias", kShadowBias);
                pbrShader.setFloat("uShadowPenumbra", shadowPenumbra);
                pbrShader.setInt("uShadowRefine", shadowRefine);
            }
        } else {
            pbrShader.setFloat3("uSunDirection", sun.direction);
            pbrShader.setFloat3("uSunColor", sun.color);
            pbrShader.setFloat("uSunIntensity", sun.intensity);

            pbrShader.setFloat3("uAmbientSky", ambient.sky);
            pbrShader.setFloat3("uAmbientGround", ambient.ground);

            pbrShader.setInt("uPointCount", kOrbitLightCount);
            pbrShader.setFloat3Array("uPointPos", pointPos.data(), engine::kMaxPointLights);
            pbrShader.setFloat3Array("uPointRadiance", pointRadiance.data(), engine::kMaxPointLights);

            pbrShader.setInt("uSpotOn", flashlightOn ? 1 : 0);
            pbrShader.setFloat3("uSpotPos", camera.position());
            pbrShader.setFloat3("uSpotDir", camera.forward());
            pbrShader.setFloat3("uSpotRadiance",
                                flashlight.color * flashlight.intensity);
            pbrShader.setFloat("uSpotInnerCos", std::cos(flashlight.innerCutoffDegrees * (3.14159265f / 180.0f)));
            pbrShader.setFloat("uSpotOuterCos", std::cos(flashlight.outerCutoffDegrees * (3.14159265f / 180.0f)));
            pbrShader.setInt("uShadowOn", 0);   // demo scene: no heightfield rig
        }

        if (cornellVariant >= 0) {
            // FROZEN geometry: identity transforms -- the OBJs are authored in
            // world space and must never be moved, rescaled, or re-lit.
            pbrShader.setMat4("uModel", engine::Mat4::identity());
            pbrShader.setMat4("uNormalMat", engine::Mat4::identity());
            pbrShader.setFloat("uUseVertexColor", 0.0f);
            for (std::uint32_t i = 0; i < cornell.def->meshCount; ++i) {
                if (!cornell.meshLoaded[i]) {
                    continue;
                }
                const cornell::MaterialDef* mat = findCornellMaterial(cornell.def->meshes[i].material);
                if (mat == nullptr) {
                    continue;
                }
                pbrShader.setFloat3("uAlbedo", mat->albedo[0], mat->albedo[1], mat->albedo[2]);
                pbrShader.setFloat("uRoughness", mat->roughness);
                pbrShader.setFloat("uMetalness", mat->metalness);
                renderer.drawIndexed(cornell.meshes[i]);
            }

            // The emitter itself: unlit emissive quad at radiance L_e.
            unlitShader.bind();
            unlitShader.setMat4("uViewProj", vp);
            unlitShader.setMat4("uModel", engine::Mat4::identity());
            unlitShader.setFloat3("uTint", cornell::kEmitterRadiance,
                                  cornell::kEmitterRadiance, cornell::kEmitterRadiance);
            renderer.drawIndexed(cornell.emitterQuad);
        } else {
            // Floor: identity model, vertex-color albedo, rough dielectric.
            pbrShader.setMat4("uModel", engine::Mat4::identity());
            pbrShader.setMat4("uNormalMat", engine::Mat4::identity());
            pbrShader.setFloat3("uAlbedo", 1.0f, 1.0f, 1.0f);
            pbrShader.setFloat("uRoughness", 0.85f);
            pbrShader.setFloat("uMetalness", 0.0f);
            pbrShader.setFloat("uUseVertexColor", 1.0f);
            renderer.drawIndexed(floorMesh);

            // Spinning quad (M1 hero): vertex-color albedo, glossy dielectric --
            // shows colored speculars from the two orbiting point lights.
            const engine::Mat4 quadModel = engine::Mat4::translate(engine::Vec3(0.0f, 0.1f, 0.0f))
                                         * engine::Mat4::rotateY(spinAngle);
            pbrShader.setMat4("uModel", quadModel);
            pbrShader.setMat4("uNormalMat", quadModel.normalMatrix());
            pbrShader.setFloat("uRoughness", 0.30f);
            renderer.drawIndexed(quadMesh);

            // Hero model: imported OBJ + material preset, floating and turning.
            if (modelLoaded[heroIndex]) {
                const engine::Mat4 modelT = engine::Mat4::translate(engine::Vec3(0.0f, 0.35f + std::sin(time * 1.2f) * 0.08f, -2.4f))
                                          * engine::Mat4::rotateY(time * 0.4f);
                const engine::PbrMaterial& mat = kModelAssets[heroIndex].material;
                pbrShader.setMat4("uModel", modelT);
                pbrShader.setMat4("uNormalMat", modelT.normalMatrix());
                pbrShader.setFloat3("uAlbedo", mat.albedo);
                pbrShader.setFloat("uRoughness", mat.roughness);
                pbrShader.setFloat("uMetalness", mat.metalness);
                pbrShader.setFloat("uUseVertexColor", mat.useVertexColor ? 1.0f : 0.0f);
                renderer.drawIndexed(modelMeshes[heroIndex]);
            }

            // Light bulbs (unlit, HDR-bright so the tonemap blooms them).
            unlitShader.bind();
            unlitShader.setMat4("uViewProj", vp);
            for (int i = 0; i < kOrbitLightCount; ++i) {
                const engine::Mat4 bulbT = engine::Mat4::translate(
                    engine::Vec3(pointPos[3 * i + 0], pointPos[3 * i + 1], pointPos[3 * i + 2]));
                unlitShader.setMat4("uModel", bulbT);
                unlitShader.setFloat3("uTint", pointRadiance[3 * i + 0],
                                      pointRadiance[3 * i + 1],
                                      pointRadiance[3 * i + 2]);
                renderer.drawIndexed(bulbMesh);
            }
        }

        renderer.endFrame();

        if (measured) {
            const auto exitTime = std::chrono::steady_clock::now();
            cpuTimes.push_back(std::chrono::duration<float, std::milli>(exitTime - entryTime).count());
            if (renderer.gpuTimingActive() && renderer.lastGpuFrameMs() >= 0.0f) {
                gpuTimes.push_back(renderer.lastGpuFrameMs());
            }
        }

        // --- verification screenshot hook ---------------------------------------
        ++frameIndex;
        if (screenshotFrames > 0 && frameIndex == static_cast<std::uint64_t>(screenshotFrames)) {
            std::vector<unsigned char> pixels(static_cast<std::size_t>(fbw) * fbh * 4);
            if (renderer.readBackbufferPixels(fbw, fbh, pixels.data())
                && writePpm(screenshotPath, pixels.data(), fbw, fbh)) {
                std::printf("[Sandbox] wrote screenshot: %s (%dx%d)\n",
                            screenshotPath.c_str(), fbw, fbh);
            } else {
                std::fprintf(stderr, "[Sandbox] screenshot capture failed.\n");
            }
            app.window().requestClose();
        }
    });

    // --- benchmark report -------------------------------------------------------
    if (benchMode) {
        auto summarize = [](const std::vector<float>& v,
                            double& avg, double& mn, double& p95) {
            if (v.empty()) { avg = mn = p95 = -1.0; return; }
            double sum = 0.0;
            float lo = v[0];
            for (float x : v) { sum += x; lo = std::min(lo, x); }
            avg = sum / static_cast<double>(v.size());
            mn = lo;
            std::vector<float> sorted(v);
            std::sort(sorted.begin(), sorted.end());
            p95 = sorted[std::min(sorted.size() - 1,
                                  static_cast<std::size_t>(sorted.size() * 95 / 100))];
        };
        double fAvg, fMin, f95, cAvg, cMin, c95, gAvg, gMin, g95;
        summarize(framePeriods, fAvg, fMin, f95);
        summarize(cpuTimes, cAvg, cMin, c95);
        summarize(gpuTimes, gAvg, gMin, g95);
        const engine::RenderStats& st = renderer.stats();
        const std::uint32_t lightCount = (cornellVariant >= 0)
            ? cornell.def->lightCount : static_cast<std::uint32_t>(kOrbitLightCount);

        char lines[14][160];   // M4.0.7: +1 for the "shadow penumbra" line
        int n = 0;
        std::snprintf(lines[n++], 160, "=== benchmark report ===");
        std::snprintf(lines[n++], 160, "standard: %s", cornellVariant >= 0 ? cornell::kStandard : "n/a (demo scene)");
        std::snprintf(lines[n++], 160, "scene: %s", sceneName.c_str());
        std::snprintf(lines[n++], 160, "resolution: %dx%d  vsync: %s", fbw, fbh, benchMode ? "off" : "on");
        std::snprintf(lines[n++], 160, "frames: %d (warmup %d discarded)", benchmarkFrames, warmupFrames);
        if (fAvg >= 0.0) {
            std::snprintf(lines[n++], 160, "fps avg: %.1f", 1000.0 / fAvg);
            std::snprintf(lines[n++], 160, "frame ms: avg %.3f  min %.3f  p95 %.3f", fAvg, fMin, f95);
        } else {
            std::snprintf(lines[n++], 160, "fps avg: n/a (no frames measured)");
        }
        if (cAvg >= 0.0) {
            std::snprintf(lines[n++], 160, "cpu ms: avg %.3f  min %.3f  p95 %.3f", cAvg, cMin, c95);
        }
        if (gAvg >= 0.0) {
            std::snprintf(lines[n++], 160, "gpu ms: avg %.3f  min %.3f  p95 %.3f (timer query)", gAvg, gMin, g95);
        } else {
            std::snprintf(lines[n++], 160, "gpu ms: n/a (timer query unavailable or disabled)");
        }
        std::snprintf(lines[n++], 160, "draw calls: %llu  triangles: %llu  lights: %u",
                      static_cast<unsigned long long>(st.drawCalls),
                      static_cast<unsigned long long>(st.triangles), lightCount);
        std::snprintf(lines[n++], 160, "exposure: %.2f (fixed)  vram: not reported (no core GL query)",
                      cornellVariant >= 0 ? static_cast<double>(cornell::kExposure) : 1.0);
        std::snprintf(lines[n++], 160, "shadows: %s",
                      (cornellVariant < 0) ? "n/a (demo scene)"
                      : (!shadowsEnabled) ? "off (--no-shadows: M3.3 direct-only)"
                      : shadowsActive ? "on (heightfield march, field verified + registered)"
                                      : "off (capture failed, verify or registration FAILED)");
        if (cornellVariant >= 0 && shadowsEnabled && shadowsActive) {
            std::snprintf(lines[n++], 160,
                          "shadow penumbra: %.4f (%s)  march step: %.3f m%s  "
                          "refine: %s",
                          shadowPenumbra,
                          shadowPenumbra > 0.0f
                              ? "soft, derived from grid pitch/height"
                              : "0 = binary march (M4.0.6 regression mode)",
                          marchStepOverride,
                          marchStepOverride == kMarchStep ? " (default)" : " (override)",
                          shadowRefine ? "on (M4.0.8)" : "off (exact M4.0.7)");
        }
        if (shadowDebugMode > 0) {
            std::snprintf(lines[n++], 160, "shadow-debug: %s (M4.0.5 instrument -- NOT a ledger image)",
                          shadowDebugMode == 1 ? "vis" : shadowDebugMode == 2 ? "field" : "uv");
        }

        std::printf("[Sandbox] ");
        for (int i = 0; i < n; ++i) {
            std::printf("%s\n", lines[i]);
        }
        if (!reportPath.empty()) {
            FILE* f = std::fopen(reportPath.c_str(), "w");
            if (f != nullptr) {
                for (int i = 0; i < n; ++i) {
                    std::fprintf(f, "%s\n", lines[i]);
                }
                std::fclose(f);
                std::printf("[Sandbox] report written: %s\n", reportPath.c_str());
            } else {
                std::fprintf(stderr, "[Sandbox] cannot write report '%s'\n", reportPath.c_str());
            }
        }
    }

    // shaders/meshes destroyed here, BEFORE `app` (and its GL context).
    return 0;
}
