# Verification

Checklist for verifying that a given build behaves correctly. Run through it
after every milestone that touches build / window / input / rendering / assets
/ math.

**Current milestone:** M3.1 — *PBR lighting + HDR pipeline + model import (real-hardware hotfixes)*.

---

## Build

- [ ] `cmake -B build` succeeds. First run downloads GLFW into `build/_deps/`;
      third-party CMake deprecation warnings are harmless.
- [ ] `cmake --build build` succeeds with zero errors on the configured compiler:
      - MSVC (Windows VS generator)
      - GCC / Clang (Ninja or Make)
- [ ] `ctest --test-dir build --output-on-failure` runs both suites and passes:
      - `math_tests` — **73 checks, 0 failure(s)**
      - `obj_tests` — **24 checks, 0 failure(s)**
- [ ] (Windows) Double-clicking `engine_math_tests.exe` / `engine_obj_tests.exe`
      in Explorer runs the suite and holds the console open with
      "Press Enter to close..." — results stay readable; `ctest` runs are
      unaffected (no pause when stdio is piped).

## Runtime

- [ ] Console prints `[Engine] Initialized. OpenGL: <version>` where `<version>`
      is `3.3` or newer, followed by `[Renderer] HDR active: RGBA16F, 4x MSAA, ...`.
- [ ] A `1280×720` window opens titled `Sandbox - M3 PBR Lighting + OBJ Import`.
- [ ] Console lists all four imported models with vertex/triangle counts:
      gold torus, chrome sphere, asteroid, painted hull. No `[Sandbox] ...`
      warnings about the asset directory.
- [ ] The floor and the color-vertex quad are lit: the quad's red/green/blue/
      yellow faces still read as colored (vertex-color albedo preserved), but
      now respond to light — glossy highlights from the orbiting lights.
- [ ] The gold **torus** floats behind the quad, rotating: bright sun highlight
      with metallic falloff, orange/cyan tints on the side facing each bulb.
- [ ] Two **glowing bulbs** (orange + cyan) orbit the scene; the floor picks up
      their colored light with inverse-square falloff (bright near the bulb,
      fading with distance).
- [ ] Highlights do NOT clip harshly: the ACES tonemap rolls bright light into
      white smoothly (compare with `--frames 1 --out low.ppm` after lowering
      exposure — no banding, no hard saturation).
- [ ] Resizing the window keeps the scene correct (HDR targets are recreated
      for the new size; MSAA fallback messages may appear but rendering continues).

## Lighting / HDR checks

- [ ] **F** toggles the camera flashlight: a warm, smooth-edged cone follows
      the view direction; surfaces brighten where the cone lands, with a soft
      outer falloff (no hard circle edge).
- [ ] **The flashlight pool is CLEAN**: no dark speckle dots, no concentric
      noise/banding rings inside the lit circle (M3.1 regression target — the
      original `pow`-based Fresnel produced NaN speckle exactly there).
      Aim the cone at the floor from a few units up and sweep the view: the
      falloff must stay a smooth gradient end to end.
- [ ] Smooth gradients show **dither, not bands**: in a dim flashlight pool,
      the 8-bit backbuffer carries fine noise grain (±1 LSB) instead of
      stepped concentric rings.
- [ ] **Mouse wheel** changes exposure; each change prints
      `exposure = <value>`. Scrolling down goes toward black, up toward a
      blown-out white — motion stays visible during both (proves exposure is
      applied *after* lighting, at the tonemap stage).
- [ ] Metals behave differently from dielectrics: the torus/sphere (metalness
      1) show no white diffuse term under the sun; the rock/ship do.
- [ ] Rough vs. smooth: the chrome sphere has a tight sun highlight; the
      asteroid's facets have broad, dim responses.

## Model import

- [ ] **1 / 2 / 3 / 4** swap the hero model instantly (torus / sphere / rock /
      ship); each keeps its own material preset; the swap is printed to stdout.
- [ ] Model discovery works from ANY launch style (M3.1): running the exe by
      double-click from `build/bin/Debug`, from VS (F5), and from the project
      root all find the bundled assets. With models intentionally missing,
      the console lists every probed path (see Diagnostics).
- [ ] All four display at a consistent size (loader normalizes to radius 0.6)
      and rotate around their own center (loader centers to origin).
- [ ] Dropping any additional `.obj` into `sandbox/assets/models/` and wiring
      it into `kModelAssets` (one line) loads and displays it — including
      exports without normals (flat shading + warning).

## Input / controls

- [ ] **Left click** locks the cursor; mouse motion rotates the camera.
- [ ] **ESC** when locked unlocks; when unlocked, requests window close
      (exit code 0).
- [ ] **W / A / S / D** fly (W follows full view direction); **E / Q** up/down.
- [ ] **SPACE** freezes the quad's spin at its current angle (not a reset to 0).
- [ ] Closing the window via the close button exits cleanly with code 0.

## Frame timing / robustness

- [ ] Pausing in a debugger for >0.1 s does not explode simulation state
      (proves `dt` is clamped to `Application::kMaxDeltaTime`).
- [ ] Losing window focus mid-press does not leave a key stuck down.

## Headless / CI verification

- [ ] `./build/bin/sandbox --frames 60 --out shot.ppm` renders 60 frames on a
      deterministic 30 Hz timeline, writes a binary PPM (1280×720), and exits 0.
      Under a headless X server (e.g. `xvfb-run -s "-screen 0 1280x720x24"
      ./build/bin/sandbox --frames 60 --out shot.ppm` with Mesa llvmpipe), the
      PPM must contain the fully lit scene — usable as a pixel-diff regression
      artifact.
- [ ] In an environment with no display at all, the sandbox exits gracefully:
      `[GLFW] ...`, `[Window] glfwInit() failed`,
      `Engine initialization failed` — no crash.

## Diagnostics

| Symptom                                          | Likely cause                                                                                       |
|--------------------------------------------------|----------------------------------------------------------------------------------------------------|
| `glfwCreateWindow failed`                        | No GL 3.3+ driver (common over Remote Desktop; run on the physical GPU).                           |
| `[GLLoader] Missing entry point: glXxx`          | Broken driver install. If persistent, switch the loader to glad2 (see `ARCHITECTURE.md` → Decisions). |
| `[Renderer] HDR target incomplete`               | Driver cannot render to RGBA16F at the requested MSAA level; the renderer retries without MSAA automatically. |
| `[Renderer] Tonemap program failed`              | GLSL compiler issue; file a bug with the log (shaders are bundled, should never fail).             |
| `[Renderer] HDR targets unavailable; continuing in direct mode` | Very old driver: scene still renders, but expects a real HDR target for correct tonemapping — colors will look dark. Update the driver. |
| `[Sandbox] Model assets not found ... Searched:` | The zip layout was modified (e.g. `sandbox/assets/models/` moved or renamed). The log lists every probed path — restore the layout or launch from the project root. |
| Test exe window closes instantly (Windows)       | Pre-M3.1 builds only; current builds hold the console open on double-click. Run tests via `ctest` for automation. |
| `The syntax of the command is incorrect.` + `vector subscript out of range` | Pre-M3.1 `obj_tests` bug (`system("mkdir -p")` on cmd.exe); fixed in M3.1 — rebuild. |
| `[Sandbox] ...: no normals in file; flat shading generated` | Informational: the imported OBJ lacked normals and got flat-shaded. Re-export with normals to smooth it. |
| `[Sandbox] ...: skipped N invalid face(s)`       | The OBJ contained degenerate polygons; they were dropped safely.                                    |
| `[Shader] ... compile error`                     | Should be unreachable with the bundled shaders; file a bug with the log.                          |
| `[Renderer] GL error 0x%04x raised during drawIndexed` | Unexpected GL state. Should not appear during a clean run.                                    |
| `Skipping engine_math_tests: ... not found`      | The CMakeLists expected `engine/tests/math_tests.cpp` (plural); the file was renamed or never renamed. |

## Math + asset pipeline

Verified two ways:

1. **Transitively at runtime** — broken lighting shows up immediately: wrong
   normal matrices shear highlights off surfaces, wrong falloff kills the
   bulb pools, wrong tonemap order (sRGB before tonemap) looks washed out.
2. **Directly via CTest:**
   - `math_tests` (73 checks) — `Vec3` basics, identity laws, translation,
     rotation (incl. length preservation), perspective depth mapping,
     `lookAt` mapping, camera clamping, plus M3: transpose laws
     (`(AB)^T = B^T A^T`), inverse round-trip (`M·M⁻¹ = I` on TRS matrices),
     rotation-inverse-is-transpose, normal-matrix behavior (strips
     scale/translation, preserves perpendicularity), singular-matrix safety.
   - `obj_tests` (24 checks) — `v//vn` triangles, `v/vt/vn` quads,
     texcoord-ignored warnings, pentagon fan triangulation, negative indices,
     flat-normal fallback, degenerate-face rejection, missing-file failure
     with error string, center+radius normalization; fixture setup is
     portable (`std::filesystem`, no shell) and indexed inspections are
     guarded (a failed load reports FAILs instead of crashing).

Expected output:

```
[math] Vec3 basics
[math] identity laws
[math] translation
[math] rotation
[math] perspective
[math] lookAt
[math] camera
[math] transpose
[math] inverse
[math] 73 checks, 0 failure(s)
[obj] basic triangle (v//vn)
[obj] quad with texcoords (v/vt/vn)
[obj] pentagon, negative indices, no normals
[obj] degenerate face skipped
[obj] missing file + normalization option
[obj] 24 checks, 0 failure(s)
```
