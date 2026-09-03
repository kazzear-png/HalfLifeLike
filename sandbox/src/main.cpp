//
// Milestone 3: PBR lighting + HDR pipeline + OBJ model import.
//
// Scene (M1/M2 objects remain the regression foundation -- relit, not replaced):
//   - reference floor + spinning quad, now shaded with the PBR pipeline
//   - imported OBJ "hero" model on display; keys 1-4 swap between the
//     bundled assets, each with its own material preset
//   - a directional sun + two orbiting point lights (drawn as glowing bulbs)
//   - a camera flashlight (F key) and scroll-wheel exposure control
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
#include "shaders.h"

#include <algorithm>
#include <array>
#include <cmath>
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
    { "ship.obj",   "painted hull",         { { 0.82f, 0.24f, 0.18f }, 0.32f, 0.15f, false } },
};

} // namespace

int main(int argc, char** argv) {
    // --- CLI: verification screenshot mode ---------------------------------
    int  screenshotFrames = 0;
    std::string screenshotPath = "sandbox.ppm";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            screenshotFrames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            screenshotPath = argv[++i];
        }
    }

    // --- window + engine boot ----------------------------------------------
    engine::WindowDesc desc;
    desc.title  = "Sandbox - M3 PBR Lighting + OBJ Import";
    desc.width  = 1280;
    desc.height = 720;
    desc.vsync  = true;

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

    // --- camera ----------------------------------------------------------------
    engine::Camera camera;
    camera.setPosition(engine::Vec3(0.0f, 0.6f, 2.8f));
    camera.setPitch(-0.22f);  // slight downward tilt: floor + quad + model in frame

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
    float exposure  = 1.0f;
    float time      = 0.0f;   // scene clock (deterministic in screenshot mode)
    float spinAngle = 0.0f;   // quad accumulator: pause freezes, does not reset
    bool  spinning  = true;

    constexpr float kOrbitRadius = 2.0f;
    constexpr float kOrbitHeight = 0.9f;

    std::printf("[Sandbox] Controls: click=look, WASD+QE=fly, 1-4=model, F=flashlight, "
                "SPACE=pause spin, ESC=quit, scroll=exposure\n");

    std::uint64_t frameIndex = 0;

    app.run([&](float dt) {
        engine::Input& in = app.input();

        // Screenshot mode advances on a deterministic 30 Hz timeline so headless
        // runs (no real vsync pacing) still produce a meaningful frame.
        const float step = (screenshotFrames > 0) ? (1.0f / 30.0f) : dt;
        time += step;
        if (spinning) {
            spinAngle += step * 0.8f;
        }

        // --- input -----------------------------------------------------------
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

        // --- fly movement ------------------------------------------------------
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

        // --- orbiting point lights ----------------------------------------------
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

        // --- camera projection (resize-safe) ------------------------------------
        app.window().getFramebufferSize(fbw, fbh);
        const float aspect = fbh > 0 ? static_cast<float>(fbw) / static_cast<float>(fbh) : 1.0f;
        camera.setPerspective(0.7854f /* 45 deg */, aspect, 0.1f, 100.0f);
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

        renderer.endFrame();

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

    // shaders/meshes destroyed here, BEFORE `app` (and its GL context).
    return 0;
}
