# Engine

A small, dependency-light C++17 3D engine core for Windows, macOS, and Linux.
Renders a physically based, HDR-lit scene through a real 3D pipeline
(perspective camera, view transform, depth test, indexed drawing), imports
Wavefront OBJ models at runtime, and ships with an event-driven input system
and an FPS-style fly camera.

**Current milestone:** M3.1 — *PBR lighting + HDR pipeline + model import (real-hardware hotfixes)*.

**Research direction:** *"How far can a small renderer push perceptual realism
through intelligent approximation rather than brute-force computation?"*
The foundation below is deliberately conventional, established math; the
innovation slot sits above it — in how the renderer chooses to solve lighting
(baked / probe / screen-space / hybrid) rather than in new shading equations.

---

## Features

- Cross-platform (Windows / macOS / Linux) C++17 core.
- **Physically based shading** — Cook-Torrance specular (GGX distribution,
  Smith height-correlated visibility, Schlick Fresnel), Lambert diffuse,
  metallic-roughness workflow. Established math only; no invented BRDF.
- **HDR pipeline** — scene rendered linear into an RGBA16F target, 4x MSAA
  with graceful fallback, then exposure → ACES tone mapping → sRGB encode.
  The scroll wheel drives exposure live.
- **Lighting rig** — directional sun, up to 8 point lights with physical
  inverse-square falloff, smooth-edged camera spotlight, hemispheric ambient
  (the deliberate stand-in for future IBL/probe research).
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
- Test harnesses (`engine_math_tests`, `engine_obj_tests`) wired into CTest.

---

## Project layout

```
.
├── CMakeLists.txt          # top-level: project, GLFW fetch, CTest
├── docs/                   # architecture, verification, changelog
│   ├── ARCHITECTURE.md
│   ├── VERIFICATION.md
│   └── CHANGELOG.md
├── tools/
│   └── generate_models.py  # regenerates the bundled OBJ assets (deterministic)
├── engine/                 # the engine core (static lib)
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── assets/         # OBJ import (loadOBJ)
│   │   ├── core/           # Application — boot, main loop, telemetry
│   │   ├── platform/       # Window (GLFW), Input (callbacks/edge state), KeyCodes
│   │   ├── rendering/      # GL loader, Shader, Mesh (VAO/VBO/EBO), Renderer
│   │   │                   #   (+ HDR pipeline), Camera, Lighting model structs
│   │   └── math/           # Vec3, Mat4 (column-major, inverse/transpose)
│   └── tests/              # math_tests.cpp, obj_tests.cpp — ctest targets
├── sandbox/                # thin client that drives the engine
│   ├── CMakeLists.txt
│   ├── assets/models/      # bundled OBJs: torus, sphere, rock, ship
│   └── src/
│       ├── main.cpp        # M3 scene: PBR demo + import showcase
│       └── shaders.h       # PBR + unlit GLSL (scene shaders live app-side)
└── README.md
```

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
# obj_tests:  23 checks, 0 failure(s)
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
| Mouse wheel       | Exposure (linear multiplier into the ACES tonemap). Printed to stdout when changed. |
| SPACE             | Pause / resume the quad's spin (freezes in place).           |

The window title bar reports `width × height`, current FPS, and triangle count.

### What to look for

- The **gold torus** (key 1): metallic — no diffuse, pure shaped reflection.
- The **chrome sphere** (key 2): the sun and both orbiting lights read as
  distinct highlights on one smooth surface.
- The **asteroid** (key 3): rough dielectric — flat-shaded facets catch light.
- The **ship** (key 4): painted hard-surface material with a metal hint.
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

Milestones land in order; ordering below is flexible:

- ~~**M3** — PBR lighting + HDR pipeline + OBJ model import.~~ ✅
- **M4** — Scene graph + batched renderer stats (draw calls / triangles / materials per frame); texture loading + OBJ material (mtl) support.
- **M5** — Shadow mapping (directional sun depth pre-pass) — the first "does the player notice?" approximation test bed.
- **M6** — Fixed-step physics with interpolation.
- **Research track** — lighting strategy selection (baked / probe / screen-space / hybrid), clustered light culling, auto-exposure. Benchmark everything; replace brute force with precomputation wherever the player cannot tell.
