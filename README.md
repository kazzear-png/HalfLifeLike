# Engine

A small, dependency-light C++17 3D engine core for Windows, macOS, and Linux.
Renders a physically based, HDR-lit scene through a real 3D pipeline
(perspective camera, view transform, depth test, indexed drawing), imports
Wavefront OBJ models at runtime, and ships with an event-driven input system
and an FPS-style fly camera.

**Current milestone:** M3.3.1 — *Cornell Box benchmark hotfix: the box renders black no more (geometry r2 + acceptance harness)*.

**Research direction:** *"How far can a small renderer push perceptual realism
through intelligent approximation rather than brute-force computation?"*
Stated as a goal: **build a renderer that knows enough about light, materials,
geometry, and perception to spend computation only where it produces visible
benefit.** PBR is the foundation, not the destination. The foundation below is
deliberately conventional, established math — now pinned to independently
derived reference values so "correct" is a test, not an opinion; the
innovation slot sits above it — in how the renderer chooses to solve lighting
(baked / probe / screen-space / hybrid) rather than in new shading equations.

---

## Features

- Cross-platform (Windows / macOS / Linux) C++17 core.
- **Physically based shading** — Cook-Torrance specular (GGX distribution,
  Smith height-correlated visibility, Schlick Fresnel), Lambert diffuse,
  metallic-roughness workflow. Established math only; no invented BRDF —
  and the equations are **reference-verified**: `brdf_tests` cross-checks the
  shader forms against an independent Lambda-based derivation of Heitz's
  exact height-correlated Smith and pins float64 white-furnace integrals.
- **Material debug + validation views** (V key) — normals / albedo /
  metal-rough / F0 inspection, plus a Valve-style material validation view
  that flags intermediate metalness, emitter-bright dielectrics, and
  implausible metal F0 in red.
- **HDR pipeline** — scene rendered linear into an RGBA16F target, 4x MSAA
  with graceful fallback, then exposure → ACES tone mapping → sRGB encode.
  The scroll wheel drives exposure live.
- **Lighting rig** — directional sun, up to 8 point lights with physical
  inverse-square falloff, smooth-edged camera spotlight, hemispheric ambient
  split with the SAME Fresnel/metalness energy rules as the direct term
  (metals get no fake diffuse; the diffuse energy comes back as a placeholder
  environment specular — the deliberate stand-in for future IBL/probe research).
- **Model import** — hand-rolled Wavefront OBJ loader: polygons → triangles,
  negative indices, `(position, normal)` deduplication, flat-normal fallback,
  degeneracy rejection, optional normalize-to-radius. Zero new dependencies.
- Real 3D pipeline: perspective projection, look-at view, depth test, indexed draws.
- Hand-rolled OpenGL 3.3 core loader — no glad/GLEW dependency.
- In-house `Vec3` / `Mat4` math (column-major, OpenGL conventions) — no GLM yet.
- Event-driven input with edge detection, mouse motion, scroll, locked-cursor mode.
- FPS-style fly camera with clamped pitch, resize-safe aspect, frame-rate-independent movement.
- Per-frame telemetry in the title bar: resolution, FPS, triangle count.
- Variable-delta main loop with `dt` clamped to `0.1 s`.
- **Benchmark mode + Cornell Box laboratory** — a frozen `cornell-box/1.0`
  standard in three variants, a deterministic `--benchmark` mode (VSync off,
  fixed camera/lights/exposure, GPU timer queries, full telemetry), a
  reference path tracer, and an SSIM-based similarity metric. Every renderer
  change gets measured against the same room. See
  `benchmarks/cornell_box/README.md`.
- Test harnesses (`engine_math_tests`, `engine_obj_tests`,
  `engine_brdf_tests`, `engine_bench_tests`) wired into CTest.

---

## Project layout

```
.
├── CMakeLists.txt          # top-level: project, GLFW fetch, CTest
├── benchmarks/             # the frozen cornell-box/1.0 laboratory (M3.3)
│   └── cornell_box/
│       ├── scene.json      # authoritative scene description
│       ├── geometry/       # frozen OBJ solids (generated, deterministic)
│       ├── reference/      # reference path traces (never regenerate)
│       ├── README.md       # the standard + two-axis protocol
│       └── BASELINE.md     # the ledger: (similarity, FPS) per milestone
├── docs/                   # architecture, verification, changelog
│   ├── ARCHITECTURE.md
│   ├── VERIFICATION.md
│   └── CHANGELOG.md
├── tools/
│   ├── generate_models.py  # regenerates the bundled OBJ assets (deterministic)
│   ├── generate_cornell.py # emits the frozen cornell geometry + scene + C++ header
│   ├── reference_pathtracer.py # ground-truth reference renders (same BRDF)
│   ├── benchmark_compare.py    # RMSE + SSIM -> Cornell similarity
│   ├── verify_cornell_shot.py  # M3.3 acceptance checklist as code (M3.3.1)
│   └── brdf_reference.py   # re-derives the float64 BRDF anchors for brdf_tests
├── engine/                 # the engine core (static lib)
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── assets/         # OBJ import (loadOBJ)
│   │   ├── core/           # Application — boot, main loop, telemetry
│   │   ├── platform/       # Window (GLFW), Input (callbacks/edge state), KeyCodes
│   │   ├── rendering/      # GL loader, Shader, Mesh (VAO/VBO/EBO), Renderer
│   │   │                   #   (+ HDR pipeline + GPU timer queries), Camera,
│   │   │                   #   Lighting model structs
│   │   └── math/           # Vec3, Mat4 (column-major, inverse/transpose)
│   └── tests/              # math/obj/brdf/bench tests — ctest targets
├── sandbox/                # thin client that drives the engine
│   ├── CMakeLists.txt
│   ├── assets/models/      # bundled OBJs: torus, sphere, rock, ship
│   └── src/
│       ├── main.cpp        # M3 demo scene + cornell benchmark scenes
│       ├── shaders.h       # PBR + unlit GLSL (scene shaders live app-side)
│       └── cornell_scene_gen.h # GENERATED from the frozen standard (do not edit)
└── README.md
```

`tools/brdf_reference.py` re-derives the float64 BRDF anchor constants used by
`brdf_tests` — run it whenever a shader equation changes, then update the
anchors in `engine/tests/brdf_tests.cpp`.

Dependency rule: `sandbox → engine → glfw`. GL types do not leak into the
`core` or `platform` public headers.

---

## Prerequisites

- C++17 compiler:
  - **Windows**: Visual Studio 2022 Build Tools (MSVC) or MinGW-w64.
  - **macOS**: `xcode-select --install`.
  - **Linux**: `sudo apt install build-essential cmake libx11-dev libxkbcommon-dev libwayland-dev libgl1-mesa-dev`.
- CMake 3.16+.
- Internet access for the **first** configure — GLFW is fetched once via
  `FetchContent` into `build/_deps/`.

On Windows/macOS/Linux desktops the engine uses `GLFW_INCLUDE_NONE`, so no
system OpenGL development headers are required beyond the above.

---

## Build & run

### Terminal

```bash
cmake -B build
cmake --build build
./build/bin/sandbox            # Windows VS generator: build\bin\Debug\sandbox.exe
```

### VS Code

1. `File → Open Folder…` (this folder).
2. `Ctrl+Shift+P → CMake: Select a Kit → choose your compiler`.
3. Build: `F7`. Run: `Shift+F5` (target `sandbox`).

### Run the tests

```bash
ctest --test-dir build --output-on-failure
# math_tests: 73 checks, 0 failure(s)
# obj_tests:  24 checks, 0 failure(s)
# brdf_tests: 38 checks, 0 failure(s)
# bench_tests: 100 checks, 0 failure(s)
```

### Run the Cornell Box benchmark

```bash
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 \
    --out /tmp/cbox01.ppm --report /tmp/cbox01_report.txt
python3 tools/benchmark_compare.py /tmp/cbox01.ppm \
    benchmarks/cornell_box/reference/cbox01_reference.ppm
```

Prints FPS / frame time / CPU time / GPU time / draw calls / triangles /
lights, and a "Cornell similarity" score (0–100, SSIM against the reference
path trace). Record both axes per milestone in
`benchmarks/cornell_box/BASELINE.md`. Full protocol:
`benchmarks/cornell_box/README.md`.

**Before recording a row, the frame must pass acceptance** — the first
on-hardware Cornell render was 96.5% black (geometry r1 authored every room
normal outward; the one-sided rasterizer shaded the room to zero). The
acceptance checklist is code now:

```bash
python3 tools/verify_cornell_shot.py /tmp/cbox01.ppm
# every criterion PASS + "verdict: ACCEPTED" (exit 0) -> save as baseline
```

### Headless screenshot (verification)

```bash
./build/bin/sandbox --frames 60 --out shot.ppm
```

Renders 60 frames on a deterministic 30 Hz timeline, writes the final
post-tonemap frame as a binary PPM (any image viewer accepts it), and exits.

---

## Controls

| Input             | Action                                                       |
|-------------------|--------------------------------------------------------------|
| Left click        | Lock the cursor and look with the mouse.                     |
| ESC               | Unlock the cursor (when locked); quit (when already unlocked). |
| W / A / S / D     | Fly forward / left / back / right (`W` follows the view direction, including pitch). |
| E / Q             | Fly up / down (world-space).                                 |
| 1 – 4             | Swap the displayed imported model (gold torus / chrome sphere / asteroid / ship). |
| F                 | Toggle the camera flashlight (smooth-edged spotlight).       |
| V                 | Cycle material views: shaded → normals → albedo → metal-rough → F0 → validation. |
| Mouse wheel       | Exposure (linear multiplier into the ACES tonemap). Printed to stdout when changed. |
| SPACE             | Pause / resume the quad's spin (freezes in place).           |

The window title bar reports `width × height`, current FPS, and triangle count.

### What to look for

- The **gold torus** (key 1): metallic — no diffuse, pure shaped reflection.
- The **chrome sphere** (key 2): the sun and both orbiting lights read as
  distinct highlights on one smooth surface.
- The **asteroid** (key 3): rough dielectric — flat-shaded facets catch light.
- The **ship** (key 4): painted hard-surface dielectric (paint is a
  clearcoat over pigment — metalness 0, the pre-PBR "metal hint" habit now
  trips the validation view).
- **V** steps through the material views: validation shows every bundled
  preset green (try metalness 0.5 to see red), and the F0 view makes the
  4%-dielectric vs. metal reflectance split visible at a glance.
- The two **glowing bulbs** orbit the scene; their light colors splash across
  the floor and the quad's glossy faces.
- **Scroll** down to near-black, up to blown-out white — exposure lives
  *above* the lighting math, exactly where an auto-exposure system will go.

---

## Documentation

| File                       | Contents                                                                            |
|----------------------------|-------------------------------------------------------------------------------------|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Module map, key decisions with exit conditions, GL lifetime rules, frame contract, public API surface. |
| [`docs/VERIFICATION.md`](docs/VERIFICATION.md) | Build + runtime + lighting / import / frame-timing checklists; expected test output. |
| [`docs/CHANGELOG.md`](docs/CHANGELOG.md)       | One entry per change to gameplay-facing interfaces. Written before the change lands. |

---

## Status against operating requirements

- **Compiles** — full CMake build config provided; single external dependency (GLFW) fetched automatically on first configure.
- **Test/verification procedure** — `ctest --test-dir build --output-on-failure` for math + asset pipeline; full checklist in [`docs/VERIFICATION.md`](docs/VERIFICATION.md).
- **Documentation updated** — README, ARCHITECTURE, VERIFICATION, CHANGELOG maintained per milestone.
- **Minimal dependencies** — GLFW only; GL loader, math, and OBJ import are in-house with documented upgrade paths (glad2 / GLM / a full asset library if formats grow).
- **Modular, not monolithic** — `core` / `platform` / `rendering` / `assets` / `math` are separate systems behind a narrow public API; the gameplay-facing interface contract is logged in [`docs/CHANGELOG.md`](docs/CHANGELOG.md), and all future changes to it are documented there before landing.

---

## Roadmap

The track below separates *correctness* (establish the reference renderer)
from *capability* (scene abstraction) from *research* (approximation above
the BRDF). Nothing shading-related ships before its reference exists.

- ~~M3 — PBR lighting + HDR pipeline + OBJ model import.~~ ✅
- ~~M3.1 — Real-hardware hotfixes (NaN speckle, banding, asset discovery).~~ ✅
- ~~M3.2 — PBR correctness pass: exact height-correlated Smith, ambient energy
  split, material validation views, BRDF reference-value tests.~~ ✅
- ~~M3.3 — Cornell Box benchmark: frozen cornell-box/1.0 standard (3 variants),
  deterministic benchmark mode with telemetry, reference path tracer,
  RMSE/SSIM similarity metric, two-axis ledger.~~ ✅
- ~~M3.3.1 — Cornell hotfix: r1 geometry authored every room normal outward
  (96.5%-black first render); r2 corrects orientation, generator validates,
  bench_tests pins it, acceptance checklist is code
  (`tools/verify_cornell_shot.py`).~~ ✅
- **M4** — Scene/material abstraction: `MeshInstance` / `Material` / `Light` /
  `Camera` as renderer-known objects (today the sandbox knows them; the
  renderer only knows "draw this mesh"). Replaces the generated Cornell header
  with a real `scene.json` loader; adds area lights (retiring the point-grid
  emitter approximation). Texture loading + OBJ mtl support.
- **M5** — Image-based lighting: environment map, irradiance, prefiltered
  specular, BRDF LUT — replaces the ambient-gradient placeholder wholesale.
- **M6** — Shadows (directional sun depth pre-pass).
- **M7** — Light culling / clustered lighting (8 lights × every pixel → N
  relevant lights per pixel).
- **M8** — Indirect lighting experiments (probes / screen-space / voxel /
  hybrid).
- **M9+** — Research: the renderer chooses the cheapest acceptable solution,
  benchmarked against the M3.2 reference values and the M3.3 Cornell ledger.
  Multi-scattering energy compensation, temporal reuse, auto-exposure.
  Larger standard scenes (Sponza / San Miguel / Bistro class) enter the
  rotation only after the Cornell protocol has been exercised for several
  milestones.

Ground rule (learned the hard way in M3.2): fix known errors FIRST. When
something later "looks better", you need to know whether you invented an
improvement or just fixed a mistake.
