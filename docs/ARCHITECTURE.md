# Architecture

Design overview of the engine core. Covers module boundaries, key decisions,
GL lifetime rules, the main-loop frame contract, and the public API surface
that gameplay codes against.

**Current milestone:** M3.3 — *Cornell Box renderer benchmark (frozen two-axis laboratory)*.

---

## Module map

```
benchmarks/
└── cornell_box/            frozen cornell-box/1.0 standard (scene.json, geometry/, reference/, ledger)
sandbox/
├── assets/models/          bundled OBJ props (regenerable via tools/)
└── src/                    main.cpp (M3 demo + cornell benchmark scenes), shaders.h,
                            cornell_scene_gen.h (GENERATED from the frozen standard)
engine/
├── src/
│   ├── assets/             OBJ import — loadOBJ: parse, triangulate, dedupe, normalize
│   ├── core/               Application — boot order, main loop, frame timing, telemetry
│   ├── platform/           Window (GLFW, GLFW_INCLUDE_NONE), Input (callbacks/edge state), KeyCodes
│   ├── rendering/          GL (scoped loader), Shader, Mesh (VAO/VBO/EBO),
│   │                       Renderer (frame state + HDR/tonemap pipeline + GPU timer
│   │                       queries), Camera, Lighting.h (light + material model structs)
│   └── math/               Vec3, Mat4 (column-major, transpose/inverse/normalMatrix)
└── tests/                  math/obj/brdf/bench tests — ctest targets
tools/
├── generate_models.py      deterministic asset generation (torus/sphere/rock/ship)
├── generate_cornell.py     emits the frozen cornell geometry + scene.json + C++ header
├── reference_pathtracer.py ground-truth reference renders (same BRDF + display transform)
├── benchmark_compare.py    RMSE + SSIM -> Cornell similarity (two-axis metric)
└── brdf_reference.py       float64 re-derivation of the BRDF anchors used by brdf_tests
```

Dependency rule: **`sandbox → engine → glfw`**. GL types do not leak into
`core` or `platform` public headers. Scene shaders live app-side
(`sandbox/src/shaders.h`); the engine-owned tonemap program is the one
deliberate exception — it *is* renderer infrastructure.

---

## Decisions

| Decision                                | Rationale                                                        | Exit condition                                          |
|-----------------------------------------|------------------------------------------------------------------|---------------------------------------------------------|
| GLFW as the only third-party dependency | Mature, tiny, permissive; window + GL context + input in one.    | Keep.                                                   |
| Hand-scoped GL loader (`rendering/GL.h`) | No glad/GLEW/generator dependency; GL surface stays explicit and reviewable. | Replace with glad2 when >~60 entry points or extensions are needed. **M3.3 count: ~60 — AT the exit condition. Timer queries were accepted as the last addition; the next extension need triggers the glad2 migration.** |
| `GLFW_INCLUDE_NONE` before every GLFW include | The engine ships its own scoped GL loader; pulling in system GL headers is redundant and breaks builds where only the GL runtime exists. | Keep.                                                   |
| Own `Vec3` / `Mat4`                     | M1-M3 need ~15 ops (plus inverse/transpose for normal matrices); no GLM dependency yet. | Adopt GLM (or extend) when math demands grow.           |
| Established PBR math (Cook-Torrance GGX / Smith / Schlick, Lambert) | Research goal is innovation *above* the BRDF (lighting strategy, approximation); new shading equations are explicitly out of scope. | Revisit only when profiling shows the BRDF itself is the bottleneck. |
| Reference-value BRDF tests (`brdf_tests` + `tools/brdf_reference.py`) | M3.2 lesson from external review: the M3.0/M3.1 visibility form deviated up to ~13-15% from the exact height-correlated Smith and nothing caught it — "looks plausible" is not a correctness argument. The shader forms are now pinned to an independent Lambda-based derivation plus float64 quadrature anchors; any edit to D or V shifts the furnace integrals immediately. | Keep for every future shading equation; extend anchors before extending shaders. |
| Frozen Cornell Box benchmark (`cornell-box/1.0`) with a two-axis ledger | A renderer research project needs an instrument, not opinions: fixed geometry/camera/lights/exposure, a reference path trace with the same BRDF + display transform, and a deterministic benchmark mode. Every change records (similarity, FPS) — the research question IS the trade between the two axes. Scene changes are forbidden; only milestone rows are appended. | The standard never changes. Larger scenes (Sponza/San Miguel/Bistro) may JOIN the rotation later, per the same freeze-and-ledger rules. |
| Cornell area emitter approximated by a deterministic point-light grid | The engine has no area lights yet. A 4x4 flux-preserving grid (I = A·L_e/N, derived in scene.json, pinned in bench_tests) keeps the current pipeline dependency-free. The grid-vs-area-light discrepancy is part of the measured similarity gap — an honest cost documented in the ledger. | Retire at M4/M5 when real area lights land; the grid math stays as the documented fallback. |
| One-sided shading contract: OBJ normals AND winding face the visible side, enforced at the source (M3.3.1) | Real-hardware lesson: r1 Cornell geometry authored every room normal outward (3 of 5 quads contradicting their own vn; spheres wound inward too) and the one-sided rasterizer rendered the whole room black — 96.5% black pixels. The reference path tracer shades two-sided and masked the bug offline. Rule: no automatic two-sided normal flip in the shader (it would silently mask asset bugs in a laboratory built to expose them); instead the generator fail-loud-validates orientation on every run and bench_tests pins normals/winding on every build (100 checks). | Keep for benchmark geometry; the M5 scene loader adopts the same validation for user-authored scenes. |
| Heightfield shadow capture (M4): two ortho R16F targets store the per-texel occluder INTERVAL [minY, maxY] (max surface from above, min from below); the PBR shader marches each point-light's receiver→light segment in XZ against it | The Cornell rig is 16 near-co-located lights: one centroid shadow map would misplace each light's shadow by up to ~0.4 m, 16 cube maps is not an incremental milestone. The interval field is EXACT for convex occluders — the entire frozen scene (blocks, baffle, spheres): a convex solid's column interval IS its vertical extent. The hanging cbox03 baffle is why max-only is not enough (rays under y=3.4 must pass). 16 superposed hard shadows form a 16-level penumbra for free. Static scene → captured once at init (precompute is a legitimate solver strategy, not a hack). | Exact while occluders are convex and static (the benchmark's contract); general omnidirectional shadow maps land with the M5 scene abstraction for dynamic/non-convex scenes; per-pixel emitter visibility integration (M5) retires the 16-superposition penumbra approximation. |
| Clean 320-spp reference as the measurement target (M4); raw 64/80-spp renders archived as provenance | The MC noise in the raw set measurably depressed SSIM (raw-vs-clean self-check: similarity 54.32) — measuring a deterministic rasterizer against a noisy reference caps the achievable score below what the algorithm actually earns. "Clean" = 5× the samples of the SAME trusted estimator; no denoiser, no invented post-processing. Per-spp sample streams make chunked generation byte-exact. | Re-baseline only via the ledger's own rule: new references require a documented re-baselining note and rows measured before it keep their note. |
| Generated Cornell header (`cornell_scene_gen.h`) until the M5 scene loading | Zero JSON-parser risk in the C++ path while still keeping scene.json the authoritative, versioned description; the generator is deterministic and the header is pinned by tests. | Replace at M5 with a real scene loader reading scene.json; delete the codegen step. |
| GPU frame timing via GL TIME_ELAPSED timer queries (one-frame-lag readback) | The benchmark report needs a GPU axis that VSync-free wall time cannot provide. Query results are read one frame later (guaranteed complete); drivers that misbehave self-disable the feature to "n/a". | Keep. VRAM stays unreported (no core GL query); revisit if an extension telemetry line is ever justified. |
| Ambient splits energy with the SAME rules as direct light (kD = (1-F)(1-metalness); placeholder env specular off the same gradient) | M3.2 review finding: metals received fake diffuse ambient (direct diffuse correctly vanished with metalness, ambient did not) — energy-inconsistent and the main "game-engine fake" tell on chrome. The old gradient×albedo ambient is replaced by a Fresnel/metalness-weighted split whose specular half is the explicit placeholder for M5 prefiltered IBL. | Replace the specular half wholesale when IBL lands (M5); keep the split convention. |
| Material debug + validation views (V key, `uDebugMode`) | Valve-style practice (Dota 2): metalness is binary, dielectric albedo must not act like an emitter, metal F0 stays in a plausible band, no accidental mirrors. Cheap views bail before the light loop — never compute what is not shown. Catches material authoring mistakes visually instead of debugging shading by eye. | Extend views when textures land (per-texel validation becomes meaningful). |
| BRDF inputs clamped to their mathematical domains; Fresnel pow5 via multiplies | Real-hardware lesson (M3.1): a camera-mounted light makes `VoH` hug 1.0 within rounding noise; unclamped `pow(negative)` is undefined GLSL and produced NaN speckle + driver-pow banding rings. Clamps and explicit multiplies are the UE4/Filament form of the SAME established math. | Keep; extend to any new term that divides or takes `pow` of a dot product. |
| Triangular-PDF dither (3 LSB peak-to-peak, ±1.5 LSB tails) in the tonemap pass | 8-bit presentation quantizes smooth HDR gradients into concentric bands once sRGB stretches the darks; two-hash dither trades bands for imperceptible grain — a perceptual shortcut, not a format change. (Amplitude/corrected comment in M3.2: the triangular sum's tails reach ±1.5 LSB by design, slightly beyond ±1 LSB, so the heavy low-end steps stay decorrelated.) | Drop if the backbuffer ever goes 10-bit+; revisit with temporal accumulation (needs blue noise). |
| HDR pipeline engine-side (RGBA16F + MSAA4 + ACES tonemap in `Renderer`) | The scene → HDR → exposure → tonemap chain is renderer infrastructure, not scene content; apps get it for free and tune exposure only. | Split into a pass system when more post effects land.   |
| MSAA 4x with graceful fallback (4x → 1x FBO → direct mode) | Wide driver compatibility without a settings UI; every fallback is logged. | Revisit when a render-settings surface exists.          |
| Hand-rolled OBJ importer (`assets/OBJ.h`) | Minimal-dependency philosophy; OBJ is text, well-specified, and covers prop geometry. Degeneracy rejection + flat-normal fallback make arbitrary exports load safely. | Adopt a full asset library (e.g. Assimp) when glTF/FBX/animations are actually needed. |
| Hemispheric ambient as IBL stand-in | Cheap, looks decent, and occupies the exact interface slot where probe/IBL research lands later. | Replace when image-based lighting lands (research track). |
| Forward renderer, lights enumerated per fragment (uniform loop, kMaxPointLights = 16) | A handful of lights and objects; simplest correct thing. The renderer still only knows "draw this mesh" — scene/material abstraction (M5) is the prerequisite for renderer-owned decisions. | Clustered light assignment when light counts grow (research track); renderer-known scene graph at M5. |
| Immediate bind-and-draw submission      | A handful of objects on screen.                                  | Batched submission when object counts grow (M4).        |
| Variable-delta loop, `dt` clamped to 0.1 s | No fixed-step simulation yet.                                  | Fixed timestep with the physics milestone.              |
| State-snapshot input (not an event queue) | Sufficient for camera controls; press/release edges captured per frame. | Event queue when text input / rebinding / fast-tap fidelity is needed. |
| Minimal test harnesses (math + obj + brdf + bench) | Project rule: minimize dependencies.                             | Switch to doctest/Catch2 when test surface grows.       |
| `Key` enum lives in `platform/KeyCodes.h` (single source of truth) | Avoids MSVC C2011 redefinition; one place to edit when adding keys. | Keep.                                                   |

---

## Lifetime rules

- `Application` owns the GL context. Create `Shader` / `Mesh` / loaded models
  **after** the `Application` and destroy them **before** it (normal C++
  scoping handles this — see `sandbox/src/main.cpp`).
- One `Window` per process: GLFW is terminated in `Window`'s destructor.
- `Shader` and `Mesh` are move-only; their GL handles are released on
  destruction. Copy is deleted to prevent double-frees.
- `Renderer`'s HDR targets, empty VAO, and tonemap program are released in
  `~Renderer`, which runs while the context is still alive (member
  destruction order inside `Application`).
- Model data (`LoadObjResult`) is plain CPU memory — no GL lifetime rules
  apply until `Mesh::create` uploads it.

---

## Frame contract

Driven by `Application::run`:

1. `input.newFrame()` — `previous := current`; reset per-frame mouse / scroll deltas.
2. `window.pollEvents()` — GLFW callbacks update current state.
3. Gameplay queries `isKeyDown` / `pressed` / `released` / `mouseDX` / `mouseDY` / `scrollDelta`.
4. Frame callback receives `dt`, clamped to `Application::kMaxDeltaTime` (`0.1 s`).
   Telemetry (title-bar FPS) uses **unclamped** time so visible FPS isn't artificially capped by the clamp.

Within the frame callback (M3 render contract):

1. `renderer.resizeHDR(w, h)` — no-op unless the framebuffer size changed.
2. `renderer.beginFrame()` — binds the HDR target (or backbuffer in fallback
   mode), clears color+depth, resets stats.
3. Scene draws: bind shaders, set per-frame light uniforms, per-object
   model/normal-matrix/material uniforms, `renderer.drawIndexed(...)`.
4. `renderer.endFrame()` — MSAA resolve + exposure + ACES tonemap to the
   backbuffer. Everything below this line is presentational.
5. `Application` swaps buffers after the callback returns.

Edge semantics:

- `pressed(Key)`  = down this frame AND up last frame (rising edge).
- `released(Key)` = up this frame AND down last frame (falling edge).
- `mousePressed` / `mouseReleased` follow the same pattern for mouse buttons.

Known limitation: a press + release fully contained inside one frame is **not**
reported (state snapshot, not an event queue). Fine for camera controls;
revisit when text input / rebinding / fast-tap fidelity is needed.

---

## Cursor modes

`Input::setCursorMode` controls cursor visibility and motion capture:

| Mode       | Cursor  | Mouse motion                                                  |
|------------|---------|---------------------------------------------------------------|
| `Normal`   | Visible | Reported as deltas while the cursor is over the window.       |
| `Hidden`   | Hidden  | Same as Normal — deltas still reported.                        |
| `Locked`   | Captured | Deltas accumulate regardless of screen edges. Raw motion enabled where supported (not on macOS). |

Mode transitions re-baseline the cursor position so the teleport is not
reported as a mouse delta. When locked, the engine requests raw mouse motion
(via GLFW) if the platform supports it.

---

## Public API

The contract gameplay codes against. Any change to the signatures below is
written up in `docs/CHANGELOG.md` **before** it lands.

```cpp
// core/Application.h
class Application {
public:
    static constexpr float kMaxDeltaTime = 0.1f;
    explicit Application(const WindowDesc& desc = {});
    bool valid() const;
    Window&   window();
    Renderer& renderer();
    Input&    input();
    void run(const std::function<void(float dt)>& onFrame);
    double fps() const;
};

// platform/Window.h
struct WindowDesc {
    std::string title  = "Engine";
    int  width         = 1280;
    int  height        = 720;
    bool vsync         = true;
    int  glVersionMajor = 3;
    int  glVersionMinor = 3;
};
class Window {
public:
    explicit Window(const WindowDesc&);
    bool shouldClose() const;
    void requestClose();
    void pollEvents();
    void swapBuffers();
    void getFramebufferSize(int& outWidth, int& outHeight) const;
    void setTitle(const std::string& title);
    bool vsync() const;
    void setVSync(bool enabled);
};

// platform/Input.h
class Input {
public:
    void attach(Window& window);
    void newFrame();

    bool isKeyDown(Key) const;
    bool wasKeyDown(Key) const;
    bool pressed(Key) const;
    bool released(Key) const;

    bool isMouseButtonDown(MouseButton) const;
    bool wasMouseButtonDown(MouseButton) const;
    bool mousePressed(MouseButton) const;
    bool mouseReleased(MouseButton) const;

    float mouseDX() const;       // accumulated since last newFrame()
    float mouseDY() const;
    float scrollDelta() const;

    void setCursorMode(CursorMode);
    CursorMode cursorMode() const;
    bool cursorLocked() const;
};

// platform/KeyCodes.h — single source of truth for `enum class Key`.

// rendering/Renderer.h
struct RenderStats {
    std::uint64_t drawCalls = 0;
    std::uint64_t triangles = 0;
};
class Renderer {
public:
    void init();
    ~Renderer();                                  // releases owned GL objects

    // HDR pipeline (M3): linear HDR scene -> exposure -> ACES -> sRGB.
    bool initHDR(int width, int height, int msaaSamples = 4);
    void resizeHDR(int width, int height);        // no-op unless size changed
    bool hdrActive() const;
    void setExposure(float linearExposure);       // scroll-driven in sandbox
    float exposure() const;

    // GPU frame timing (M3.3, benchmark telemetry).
    void enableGpuTiming(bool enable);            // TIME_ELAPSED, one-frame lag
    float lastGpuFrameMs() const;                 // < 0 == "n/a"
    bool gpuTimingActive() const;

    void setViewport(int x, int y, int width, int height);
    void setClearColor(float r, float g, float b, float a);
    void beginFrame();                   // binds+clears target; resets stats
    void endFrame();                     // resolve + tonemap to backbuffer
    void drawIndexed(const Mesh& mesh);
    const RenderStats& stats() const;
    bool readBackbufferPixels(int width, int height, unsigned char* outRgba);
};

// rendering/Shader.h — move-only
class Shader {
public:
    static Shader fromSource(const char* vertexSource, const char* fragmentSource);
    void bind() const;
    void setMat4(const char* name, const Mat4& value);
    void setFloat4(const char* name, float x, float y, float z, float w);
    void setFloat(const char* name, float value);              // M3
    void setInt(const char* name, int value);                  // M3
    void setFloat3(const char* name, float x, float y, float z); // M3
    void setFloat3(const char* name, const Vec3& v);           // M3
    void setFloat3Array(const char* name, const float* xyz, int count); // M3
    bool valid() const;
    gl::GLuint nativeHandle() const;                           // M3
};

// rendering/Lighting.h — plain structs; upload via Shader setters
inline constexpr int kMaxPointLights = 16;   // M3.3: 8 -> 16 (Cornell emitter grid)
struct DirectionalLight { Vec3 direction; Vec3 color; float intensity; };
struct PointLight      { Vec3 position;  Vec3 color; float intensity; };
struct SpotLight       { Vec3 position, direction; Vec3 color; float intensity;
                         float innerCutoffDegrees, outerCutoffDegrees; };
struct PbrMaterial     { Vec3 albedo; float roughness; float metalness;
                         bool useVertexColor; };
struct AmbientTerms    { Vec3 sky, ground; };

// assets/OBJ.h — dependency-free model import (M3)
struct LoadObjOptions { bool centerToOrigin = true; float targetRadius = 0.0f; };
struct LoadObjResult {
    bool ok; std::string error, warnings;
    std::vector<Vertex> vertices; std::vector<std::uint32_t> indices;
    int triangleCount, skippedFaces;
};
LoadObjResult loadOBJ(const std::string& path, const LoadObjOptions& options = {});

// rendering/Mesh.h — move-only
struct Vertex { float x, y, z; float nx, ny, nz; float r, g, b; };  // M3: +normal
class Mesh {
public:
    bool create(const Vertex* vertices,   std::uint32_t vertexCount,
                const std::uint32_t* indices, std::uint32_t indexCount);
    void bind() const;
    void release();
    bool valid() const { return m_vao != 0; }
    std::uint32_t indexCount() const;
};

// rendering/Camera.h
class Camera {
public:
    void setPerspective(float fovYRadians, float aspect, float nearZ, float farZ);
    const Mat4& projection() const;
    const Vec3& position() const;
    void setPosition(const Vec3& p);
    float yaw() const;
    float pitch() const;
    void setYaw(float radians);
    void setPitch(float radians);   // clamped to ±kMaxPitch (~89.95°)
    void addYaw(float radians);
    void addPitch(float radians);
    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;
    Mat4 view() const;
    Mat4 viewProjection() const;
    static constexpr float kMaxPitch = 1.5703f;
};

// math/Mat4.h — M3 additions to the existing struct:
//   static Mat4 scale(const Vec3& s);  // non-uniform
//   Mat4 transpose() const;
//   Mat4 inverse() const;              // singular-safe: identity fallback
//   Mat4 normalMatrix() const;         // inverse().transpose()
```

---

## GL surface (currently loaded entry points)

The scoped loader in `rendering/GL.h` resolves only what the engine uses.
Upgrade path: replace with glad2 once coverage grows beyond ~60 entry points
(M3 count: ~55 — approaching the exit condition).

Currently loaded groups:

- **Queries:** `GetString`, `GetError`
- **State:** `Enable`, `Disable` (M3), `Viewport`, `ClearColor`, `Clear`
- **Drawing:** `DrawElements`, `DrawArrays` (M3, attribute-less tonemap triangle)
- **Shader / program:** `CreateShader`, `ShaderSource`, `CompileShader`,
  `GetShaderiv`, `GetShaderInfoLog`, `DeleteShader`, `CreateProgram`,
  `AttachShader`, `LinkProgram`, `GetProgramiv`, `GetProgramInfoLog`,
  `DeleteProgram`, `UseProgram`
- **Uniforms:** `GetUniformLocation`, `UniformMatrix4fv`, `Uniform4f`,
  `Uniform1f`, `Uniform1i`, `Uniform3f`, `Uniform3fv` (M3)
- **Buffers / vertex arrays:** `GenVertexArrays`, `BindVertexArray`,
  `DeleteVertexArrays`, `GenBuffers`, `BindBuffer`, `BufferData`,
  `DeleteBuffers`, `EnableVertexAttribArray`, `VertexAttribPointer`
- **Textures (M3):** `GenTextures`, `DeleteTextures`, `BindTexture`,
  `ActiveTexture`, `TexImage2D`, `TexParameteri`
- **Framebuffer objects (M3):** `GenFramebuffers`, `DeleteFramebuffers`,
  `BindFramebuffer`, `FramebufferTexture2D`, `GenRenderbuffers`,
  `DeleteRenderbuffers`, `BindRenderbuffer`, `RenderbufferStorage`,
  `RenderbufferStorageMultisample`, `FramebufferRenderbuffer`,
  `CheckFramebufferStatus`, `BlitFramebuffer`
- **Readback (M3):** `ReadPixels` (verification screenshots)
- **Timer queries (M3.3):** `GenQueries`, `DeleteQueries`, `BeginQuery`,
  `EndQuery`, `GetQueryObjectui64v` (benchmark GPU frame time)

On Windows, the loader falls back to `opengl32.dll` (via `GetProcAddress`) for
any entry point `glfwGetProcAddress` does not return — some Windows drivers
do not expose GL 1.1 entry points through `wglGetProcAddress`.
