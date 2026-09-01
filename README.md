# Engine

A small, dependency-light C++17 3D engine core for Windows, macOS, and Linux.
Renders an animated quad through a real 3D pipeline (perspective camera, view
transform, depth test, indexed drawing) and ships with an event-driven input
system and an FPS-style fly camera.

**Current milestone:** M2 — *Event-driven input + fly camera*.

---

## Features

- Cross-platform (Windows / macOS / Linux) C++17 core.
- Real 3D pipeline: perspective projection, look-at view, depth test, indexed draws.
- Hand-rolled OpenGL 3.3 core loader — no glad/GLEW dependency.
- In-house `Vec3` / `Mat4` math (column-major, OpenGL conventions) — no GLM yet.
- Event-driven input with edge detection (`pressed` / `released`), mouse motion, scroll, and locked-cursor (raw-motion where supported) mode.
- FPS-style fly camera with clamped pitch, resize-safe aspect, frame-rate-independent movement.
- Per-frame telemetry in the title bar: resolution, FPS, triangle count.
- Variable-delta main loop with `dt` clamped to `0.1 s` so debugger pauses or window drags cannot explode simulation state.
- Minimal math test harness (`engine_math_tests`) wired into CTest.

---

## Project layout

```
.
├── CMakeLists.txt          # top-level: project, GLFW fetch, CTest
├── docs/                   # architecture, verification, changelog
│   ├── ARCHITECTURE.md
│   ├── VERIFICATION.md
│   └── CHANGELOG.md
├── engine/                 # the engine core (static lib)
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── core/           # Application — boot, main loop, telemetry
│   │   ├── platform/       # Window (GLFW), Input (callbacks/edge state), KeyCodes
│   │   ├── rendering/      # GL loader, Shader, Mesh (VAO/VBO/EBO), Renderer, Camera
│   │   └── math/           # Vec3, Mat4 (column-major)
│   └── tests/              # math_tests.cpp — ctest target
├── sandbox/                # thin client that drives the engine
│   ├── CMakeLists.txt
│   └── src/main.cpp
└── README.md
```

Dependency rule: `sandbox → engine → glfw`. GL types do not leak into the
`core` or `platform` public headers.

---

## Prerequisites

- VS Code + extensions (recommended): [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools), [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools).
- C++17 compiler:
  - **Windows**: Visual Studio 2022 Build Tools (MSVC) or MinGW-w64.
  - **macOS**: `xcode-select --install`.
  - **Linux**: `sudo apt install build-essential cmake libx11-dev libxkbcommon-dev libwayland-dev libgl1-mesa-dev`.
- CMake 3.16+.
- Internet access for the **first** configure — GLFW is fetched once via `FetchContent` into `build/_deps/`. Subsequent offline builds reuse the cached download.

---

## Build & run

### VS Code

1. `File → Open Folder…` (this folder).
2. `Ctrl+Shift+P → CMake: Select a Kit → choose your compiler`.
3. Build: `F7`.
4. Run: `Shift+F5` (target `sandbox`), or run the binary directly:
   - VS generator: `build\bin\Debug\sandbox.exe`
   - Ninja/Make:   `build/bin/sandbox`

### Terminal

```bash
cmake -B build
cmake --build build
./build/bin/sandbox            # Windows VS generator: build\bin\Debug\sandbox.exe
```

### Run the math tests

```bash
ctest --test-dir build --output-on-failure
```

---

## Controls

The sandbox exposes the M2 fly camera over a spinning quad and a reference floor.

| Input             | Action                                                       |
|-------------------|--------------------------------------------------------------|
| Left click        | Lock the cursor and look with the mouse.                     |
| ESC               | Unlock the cursor (when locked); quit (when already unlocked). |
| W / A / S / D     | Fly forward / left / back / right (`W` follows the view direction, including pitch). |
| E / Q             | Fly up / down (world-space).                                 |
| SPACE             | Pause / resume the quad's spin (toggles per press, not while held). |

The window title bar reports `width × height`, current FPS, and triangle count.

---

## Documentation

| File                       | Contents                                                                            |
|----------------------------|-------------------------------------------------------------------------------------|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Module map, key decisions with exit conditions, GL lifetime rules, frame contract, public API surface. |
| [`docs/VERIFICATION.md`](docs/VERIFICATION.md) | Build + runtime + input / frame-timing / diagnostics checklists; expected math test output. |
| [`docs/CHANGELOG.md`](docs/CHANGELOG.md)       | One entry per change to gameplay-facing interfaces. Written before the change lands. |

---

## Status against operating requirements

- **Compiles** — full CMake build config provided; single external dependency (GLFW) fetched automatically on first configure.
- **Test/verification procedure** — `ctest --test-dir build --output-on-failure` for math; full checklist in [`docs/VERIFICATION.md`](docs/VERIFICATION.md).
- **Documentation updated** — README, ARCHITECTURE, VERIFICATION, CHANGELOG maintained per milestone.
- **Minimal dependencies** — GLFW only; GL loader and math are in-house with documented upgrade paths (glad2 / GLM).
- **Modular, not monolithic** — `core` / `platform` / `rendering` / `math` are separate systems behind a narrow public API; the gameplay-facing interface contract is logged in [`docs/CHANGELOG.md`](docs/CHANGELOG.md), and all future changes to it are documented there before landing.

---

## Roadmap

Proposed next milestones (ordering is flexible):

- **M3** — Asset pipeline: shaders and meshes loaded from disk.
- **M4** — Scene graph + batched renderer stats (draw calls / triangles / materials per frame).
- **M5** — Fixed-step physics with interpolation.
