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
- Hand-rolled OpenGL 3.3 core loader (`rendering/GL.h`) — no glad/GLEW dependency.
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

## Architecture

### Module responsibilities

| Module                  | Responsibility                                                            |
|-------------------------|---------------------------------------------------------------------------|
| `core::Application`     | Boot order, main loop, frame timing, telemetry (title-bar FPS / tris).    |
| `platform::Window`      | GLFW wrapper: GL context, swap, vsync, title, framebuffer size, lifecycle.|
| `platform::Input`       | Keyboard / mouse / cursor state with edge detection; GLFW callbacks.      |
| `rendering::GL`          | Scoped GL 3.3 core loader (entry points loaded explicitly on demand).     |
| `rendering::Shader`     | GLSL program compile/link, uniform setters. Move-only.                     |
| `rendering::Mesh`       | VAO + VBO + EBO for a fixed `{position, color}` vertex layout. Move-only.  |
| `rendering::Renderer`   | Global GL state, per-frame clear, indexed draw submission, stats.          |
| `rendering::Camera`     | FPS-style yaw/pitch fly camera with perspective projection.                |
| `math::Vec3` / `Mat4`   | In-house math, column-major, OpenGL uniform layout.                       |

### Key decisions (M2)

| Decision                                | Rationale                                                        | Exit condition                                       |
|-----------------------------------------|------------------------------------------------------------------|------------------------------------------------------|
| GLFW as the only third-party dependency | Mature, tiny, permissive; window + GL context + input in one.   | Keep.                                                |
| Hand-scoped GL loader (`rendering/GL.h`) | No glad/GLEW/generator dependency at M1/M2; GL surface stays explicit and reviewable. | Replace with glad2 when >~60 entry points or extensions are needed. |
| Own `Vec3` / `Mat4`                     | M1/M2 need ~10 ops; no GLM dependency yet.                       | Adopt GLM (or extend) when math demands grow.        |
| Immediate bind-and-draw submission       | One object on screen.                                            | Batched submission when object counts grow.          |
| Variable-delta loop, `dt` clamped to 0.1 s | No fixed-step simulation yet.                                  | Fixed timestep with the physics milestone.           |
| State-snapshot input (not an event queue) | Sufficient for camera controls; press/release edges captured per frame. | Event queue when text input / rebinding / fast-tap fidelity is needed. |
| Minimal math test harness                | Project rule: minimize dependencies.                              | Switch to doctest/Catch2 when test surface grows.    |

### Lifetime rules

- `Application` owns the GL context. Create `Shader` / `Mesh` **after** the `Application` and destroy them **before** it (normal C++ scoping handles this — see `sandbox/src/main.cpp`).
- One `Window` per process: GLFW is terminated in `Window`'s destructor.
- `Shader` and `Mesh` are move-only; their GL handles are released on destruction.

### Frame contract

Driven by `Application::run`:

1. `input.newFrame()` — previous := current; reset per-frame mouse / scroll deltas.
2. `window.pollEvents()` — GLFW callbacks update current state.
3. Gameplay queries `isKeyDown` / `pressed` / `released` / `mouseDX` / `mouseDY` / `scrollDelta`.
4. `dt` passed to the frame callback is clamped to `Application::kMaxDeltaTime` (`0.1 s`). Telemetry uses unclamped time.

`pressed()`  = down this frame AND up last frame (rising edge).
`released()` = up this frame AND down last frame (falling edge).

---

## Public API

The contract gameplay codes against:

```cpp
// core/Application.h
class Application {
public:
    static constexpr float kMaxDeltaTime = 0.1f;
    explicit Application(const WindowDesc& desc = {});
    bool valid() const;
    Window&   window();   Renderer& renderer();   Input& input();
    void run(const std::function<void(float dt)>& onFrame);
    double fps() const;
};

// platform/Window.h
struct WindowDesc { std::string title; int width, height; bool vsync;
                    int glVersionMajor, glVersionMinor; };
class Window {
public:
    bool shouldClose() const;            void requestClose();
    void pollEvents();                   void swapBuffers();
    void getFramebufferSize(int& w, int& h) const;
    void setTitle(const std::string&);   bool vsync() const;   void setVSync(bool);
};

// platform/Input.h
class Input {
public:
    void attach(Window& window);         void newFrame();
    bool isKeyDown(Key) const;           bool wasKeyDown(Key) const;
    bool pressed(Key) const;            bool released(Key) const;
    bool isMouseButtonDown(MouseButton) const;   bool mousePressed(MouseButton) const;
    bool mouseReleased(MouseButton) const;
    float mouseDX() const;              float mouseDY() const;   float scrollDelta() const;
    void setCursorMode(CursorMode);      CursorMode cursorMode() const;   bool cursorLocked() const;
};

// platform/KeyCodes.h — single source of truth for `enum class Key`.

// rendering/Renderer.h
struct RenderStats { std::uint64_t drawCalls, triangles; };
class Renderer {
public:
    void init();
    void setViewport(int x, int y, int w, int h);
    void setClearColor(float r, float g, float b, float a);
    void clear();                       // begins a frame; resets stats
    void drawIndexed(const Mesh&);       const RenderStats& stats() const;
};

// rendering/Shader.h
class Shader {  // move-only
public:
    static Shader fromSource(const char* vs, const char* fs);
    void bind() const;
    void setMat4(const char* name, const Mat4&);
    void setFloat4(const char* name, float x, float y, float z, float w);
};

// rendering/Mesh.h
struct Vertex { float x, y, z, r, g, b; };  // fixed layout for M1/M2
class Mesh {  // move-only
public:
    bool create(const Vertex* verts, uint32_t vertCount,
                const uint32_t* indices, uint32_t indexCount);
    void bind() const;    void release();
    bool valid() const;   uint32_t indexCount() const;
};

// rendering/Camera.h
class Camera {
public:
    void setPerspective(float fovYRad, float aspect, float nearZ, float farZ);
    const Vec3& position() const;        void setPosition(const Vec3&);
    float yaw() const;                   float pitch() const;
    void setYaw(float);                  void setPitch(float);   // clamped
    void addYaw(float);                  void addPitch(float);
    Vec3 forward() const;                Vec3 right() const;     Vec3 up() const;
    Mat4 view() const;                   Mat4 viewProjection() const;
};
```

Any change to the above is written up in `docs/CHANGELOG.md` **before** it lands.

---

## Known stopgaps (deliberate)

- **Input is state-snapshot, not an event queue.** A press+release fully contained inside one frame is not reported. Fine for camera controls; revisit for text input / rebinding / fast-tap fidelity.
- **No asset pipeline yet.** Shaders are inline strings in the sandbox for M1/M2.
- **Raw mouse motion is platform-dependent.** Locked-cursor deltas work everywhere; bypassing OS acceleration (raw motion) is unavailable on some platforms (e.g. macOS).

---

## Diagnostics

| Symptom                                          | Likely cause                                                                                       |
|--------------------------------------------------|----------------------------------------------------------------------------------------------------|
| `glfwCreateWindow failed`                        | No GL 3.3+ driver (common over Remote Desktop; run on the physical GPU).                           |
| `[GLLoader] Missing entry point: glXxx`          | Broken driver install. If persistent, switch the loader to glad2 (see Architecture → Decisions).   |
| `[Shader] ... compile error`                     | Should be unreachable with the bundled shaders; file a bug with the log.                          |
| `[Renderer] GL error 0x%04x raised during drawIndexed` | Unexpected GL state. Should not appear during a clean run.                                    |

Math (`Vec3` / `Mat4`) is verified transitively in M1/M2 (a wrong perspective/`lookAt`/multiply produces a missing or badly distorted quad, not a clean render) and is also covered by `engine_math_tests` (Vec3 basics, identity laws, translation, rotation, perspective depth mapping, lookAt, camera yaw/pitch).

---

## Status against operating requirements

- **Compiles** — full CMake build config provided; single external dependency (GLFW) fetched automatically on first configure.
- **Test/verification procedure** — `ctest --test-dir build --output-on-failure` for math; runtime checklist in `docs/VERIFICATION.md` for the sandbox.
- **Documentation updated** — README, ARCHITECTURE, VERIFICATION, CHANGELOG maintained per milestone.
- **Minimal dependencies** — GLFW only; GL loader and math are in-house with documented upgrade paths (glad2 / GLM).
- **Modular, not monolithic** — `core` / `platform` / `rendering` / `math` are separate systems behind a narrow public API; the gameplay-facing interface contract is logged in `docs/CHANGELOG.md`, and all future changes to it are documented there before landing.

---

## Roadmap

Proposed next milestones (ordering is flexible):

- **M3** — Asset pipeline: shaders and meshes loaded from disk.
- **M4** — Scene graph + batched renderer stats (draw calls / triangles / materials per frame).
- **M5** — Fixed-step physics with interpolation.
