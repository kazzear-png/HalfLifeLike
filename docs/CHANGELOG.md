# Changelog

One entry per change to **interfaces gameplay can see**. Written **before** the
change lands. Format: `MAJOR.MINOR.PATCH — <short title>` followed by the
sections Added / Changed / Removed / Deprecated / Fixed as needed.

---

## 0.3.1 — M3.1: real-hardware hotfixes (flashlight artifacts, Windows runs)

First M3 feedback round on real Windows hardware surfaced three defects that
headless CI could not catch: NaN speckle/banding inside the flashlight beam,
imported models never loading under MSVC multi-config build trees (keys 1-4
appeared dead), and the test executables being unusable when double-clicked.

### Fixed
- **Flashlight NaN speckle + banding rings** (`sandbox/src/shaders.h`):
  * `F_Schlick` no longer calls `pow(1.0 - VoH, 5.0)`. With the flashlight
    mounted on the camera, `L ≈ V` across the whole beam, so `VoH` hugs 1.0
    to within rounding noise; `pow` of a slightly negative base is undefined
    in GLSL and produced per-pixel NaN speckle, and low-precision driver
    `pow` paths quantized the Fresnel into visible concentric rings.
    Replaced with clamped multiplies (the UE4/Filament form).
  * `shadeLight` clamps `NoV/NoH/VoH` to their mathematical domains and
    guards the degenerate half-vector (`V + L ≈ 0` falls back to `N`).
  * GGX `alpha` floored at `2e-3` (`alpha == 0` divides by zero at
    `NoH == 1`); spot cone `theta` clamped to `[0,1]`.
- **8-bit gradient banding** (`engine/src/rendering/Renderer.cpp`): the
  tonemap pass now applies triangular-PDF dither (+-1 LSB, stateless
  per-pixel hash) before presentation. Smooth HDR gradients — flashlight
  pools especially — no longer quantize into concentric bands after sRGB.
- **Models not found under IDE build trees** (`sandbox/src/main.cpp`):
  the asset search now walks every ancestor of BOTH the CWD and the exe
  directory, so `build/bin/Debug/sandbox.exe` (MSVC multi-config) and
  `out/build/x64-Release` (CMake presets) both resolve the bundled models.
  On failure the sandbox prints every probed path instead of failing
  silently (which is what made keys 1-4 look dead).
- **`engine_obj_tests` crashed on Windows** (`engine/tests/obj_tests.cpp`):
  fixture setup used `system("mkdir -p ...")`, which cmd.exe rejects ("The
  syntax of the command is incorrect"); loads then failed and indexed access
  into the empty result tripped MSVC's vector-bounds assert. Fixture
  creation now uses `std::filesystem::create_directories`, write failures
  are reported, and indexed inspections are guarded (a failed load reports
  FAILs instead of crashing).

### Changed
- **Test executables pause for interactive runs** (both `engine/tests/*`):
  on Windows, when stdin is a real console (double-click launch), the tests
  print "Press Enter to close..." and wait; ctest/CI runs (piped stdio) are
  unaffected.

---

## 0.3.0 — M3: PBR lighting + HDR pipeline + model import

### Added
- **`engine::Renderer` HDR pipeline** — the frame is now:
  *linear HDR scene → RGBA16F (4x MSAA) target → resolve → exposure → ACES
  tonemap → sRGB → present*.
  - `initHDR(width, height, msaaSamples = 4)`, `resizeHDR(width, height)`,
    `setExposure(float)`, `exposure()`, `hdrActive()`.
  - Graceful degradation chain: MSAA → no-MSAA FBO → direct backbuffer
    rendering (legacy behavior), each step logged to stderr.
  - The tonemap program is engine-owned (GLSL lives in `Renderer.cpp`);
    scene shaders remain app-side.
  - `~Renderer` releases all owned GL objects while the context is alive.
- **`engine::loadOBJ`** (`assets/OBJ.h`) — dependency-free Wavefront OBJ
  importer:
  - `v` / `vn` / `vt` (texcoords parsed + reported as ignored), faces with
    3+ corners in `v`, `v/vt`, `v//vn`, `v/vt/vn` forms, negative indices.
  - Fan triangulation with **degeneracy rejection** (zero-area triangles
    dropped and counted, never NaN normals).
  - Flat-normal fallback when a file (or a corner) lacks normals; warnings
    returned in `LoadObjResult::warnings`.
  - `(position, normal)` corner deduplication; optional center-to-origin +
    uniform scale to `targetRadius`.
- **`engine::Lighting`** (`rendering/Lighting.h`) — `DirectionalLight`,
  `PointLight`, `SpotLight`, `PbrMaterial` (metallic-roughness),
  `AmbientTerms` (hemispheric stand-in for IBL), `kMaxPointLights = 8`.
- **`engine::Shader`** — `setFloat`, `setInt`, `setFloat3` (x2 overloads),
  `setFloat3Array` (vec3 array uploads for light loops), `nativeHandle()`.
- **`engine::Mat4`** — `transpose()`, `inverse()` (general, singular-safe),
  `scale(Vec3)` (non-uniform), `normalMatrix()` (inverse-transpose).
- **GL loader** — 24 new entry points (framebuffer objects, renderbuffers,
  MSAA, textures, extra uniforms, `DrawArrays`, `Disable`, `ReadPixels`).
  Still hand-scoped; ~55/60 before the glad2 exit condition triggers.
- **`engine::Key`** — `F` (flashlight), `Digit4` (model swap), mapped in
  `platform/Input.cpp`.
- **Tests** — `math_tests` grows transpose / inverse / normal-matrix suites
  (73 checks total); new **`obj_tests`** ctest target (23 checks: corner
  forms, fan triangulation, negative indices, degenerate faces, missing
  files, normalization).
- **Bundled assets** — `sandbox/assets/models/{torus,sphere,rock,ship}.obj`,
  regenerable via `tools/generate_models.py` (deterministic output).
- **Sandbox M3 demo** — PBR shaders (Cook-Torrance: GGX distribution, Smith
  height-correlated visibility, Schlick Fresnel; Lambert diffuse), relit
  floor + spinning quad (vertex-color albedo preserved), hero OBJ model
  with per-model material presets (keys 1-4), directional sun, two orbiting
  point lights rendered as glowing bulbs, camera flashlight (F), scroll-wheel
  exposure, `--frames N --out shot.ppm` verification screenshot hook.

### Changed
- **`engine::Renderer::clear()`** replaced by `beginFrame()` / `endFrame()`
  (target binding + tonemap bracket the scene). Callers: see sandbox.
- **`engine::Vertex`** layout grew a normal channel:
  `{pos, normal, color}` — locations 0 / 1 / 2. Mesh creation and the M1/M2
  scene meshes updated accordingly.
- **`GLFW_INCLUDE_NONE`** defined before every `<GLFW/glfw3.h>` include —
  the engine ships its own scoped GL loader and no longer depends on system
  GL headers being present (fixes builds where only GL runtime exists).
- Engine CMake: `src/assets/OBJ.cpp` added to the lib; `obj_tests` wired
  into CTest.
- Window title telemetry unchanged; triangle counts now include imported
  geometry.

### Removed
- Nothing public. (`Renderer::clear()` semantics moved into `beginFrame()`.)

---

## 0.2.0 — M2: Event-driven input + fly camera

### Added
- **`engine::Input`** (`platform/Input.h`) — event-driven input system. Owns
  keyboard / mouse state and cursor modes.
  - Edge-detection API: `pressed(Key)`, `released(Key)`,
    `mousePressed(MouseButton)`, `mouseReleased(MouseButton)`.
  - Per-frame deltas: `mouseDX()`, `mouseDY()`, `scrollDelta()`.
  - `CursorMode { Normal, Hidden, Locked }`; raw mouse motion enabled where
    supported when locked.
  - Focus-loss handling: key + mouse state cleared on focus loss to avoid
    stuck-key artifacts.
- **`engine::Key`** enum, with single source of truth in
  `platform/KeyCodes.h` (see Fixed below).
- **`engine::MouseButton`** and **`engine::CursorMode`** enums.
- **`engine::Camera`** (`rendering/Camera.h`) — FPS-style yaw/pitch fly camera
  with clamped pitch (`kMaxPitch ≈ 89.95°`), perspective projection, and
  derived `forward` / `right` / `up` basis vectors.
- **`engine::Application::input()`** accessor.
- **`engine::Application::kMaxDeltaTime`** (`0.1 s`) — `dt` passed to the frame
  callback is clamped; telemetry uses unclamped time.
- Math test target **`engine_math_tests`**, wired into CTest. Covers `Vec3`
  basics, identity laws, translation, rotation, perspective depth mapping,
  `lookAt`, and camera yaw/pitch clamping.

### Changed
- **`engine::Application::run`** now invokes `input.newFrame()` before
  `window.pollEvents()` so edges reflect this frame's events.
- **`platform/Window`** no longer owns keyboard input. The M1 `Window::isKeyDown`
  method and the `toGLFWKey` / `toGLFWKey` helpers have been removed; gameplay
  uses `Input::isKeyDown` / `pressed` / `released` instead.
- Sandbox (`sandbox/src/main.cpp`) updated to the M2 demo: spinning quad +
  reference floor, fly camera with locked-cursor mouse look.
- Engine `CMakeLists.txt` registers the `math_tests` ctest target
  (`BUILD_TESTING`, default ON).

### Removed
- M1 `engine::Window::isKeyDown(Key)` — superseded by `engine::Input`.
- M1 `engine::Key` 6-key enum — replaced by the full `Key` enum in
  `platform/KeyCodes.h`.

### Fixed
- `engine/src/platform/Window.cpp` no longer fails to compile due to leftover
  M1 `Window::isKeyDown` / `toGLFWKey` dead code that referenced an undefined
  `Key` symbol.
- `engine/tests/math_test.cpp` renamed to `engine/tests/math_tests.cpp` to
  match the filename expected by `engine/CMakeLists.txt` (CMake previously
  printed `Skipping engine_math_tests: ... not found` and silently skipped
  tests).

---

## 0.1.0 — M1: Foundation (Hello Quad)

### Added
- **`engine::Application`** (`core/Application.h`) — owns the boot sequence
  and the main loop; GL lifetime contract documented in `docs/ARCHITECTURE.md`.
- **`engine::Window`** (`platform/Window.h`) — GLFW wrapper exposing
  `shouldClose`, `requestClose`, `pollEvents`, `swapBuffers`,
  `getFramebufferSize`, `setTitle`, `setVSync`.
- **`engine::Renderer`** (`rendering/Renderer.h`) — global GL state per frame:
  `setViewport`, `setClearColor`, `clear`, `drawIndexed`, `stats`.
- **`engine::Shader`** (`rendering/Shader.h`) — move-only GLSL program wrapper
  with `fromSource(vs, fs)`, `bind`, `setMat4`, `setFloat4`.
- **`engine::Mesh`** (`rendering/Mesh.h`) — move-only VAO + VBO + EBO for a
  fixed `{position, color}` vertex layout.
- **`engine::Vertex`** layout `{x, y, z, r, g, b}`.
- **`engine::Vec3`** / **`engine::Mat4`** in-house math (column-major,
  OpenGL uniform layout).
- Hand-scoped **GL 3.3 core loader** (`rendering/GL.h`) — no glad/GLEW
  dependency; entry points loaded explicitly on demand.
