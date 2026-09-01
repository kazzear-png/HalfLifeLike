# Engine — Milestone 1: Hello Quad

A small **C++17 game-engine core** built from scratch for **Windows, macOS, and Linux**.

Milestone 1 establishes the engine's first real rendering pipeline:

* Perspective camera
* View and projection transforms
* Depth testing
* Indexed rendering
* GPU shaders
* Vertex/index buffers
* Frame timing and FPS telemetry
* Basic keyboard input
* Cross-platform windowing through GLFW

The result is a simple **animated 3D quad** rendered through OpenGL rather than a 2D blit.

---

## Milestone 1 — Hello Quad

When running the sandbox, you should see a four-colored quad rotating in 3D with perspective foreshortening and depth testing.

### Controls

| Key     | Action                  |
| ------- | ----------------------- |
| `SPACE` | Pause / resume rotation |
| `ESC`   | Exit                    |

The window title displays the current **FPS** and **triangle count**.

---

## Architecture

The engine is intentionally split into small modules with narrow interfaces.

```text
.
├── sandbox/
│   └── Thin client / gameplay / editor shell
│
├── engine/
│   ├── core/
│   │   └── Application
│   │       ├── Boot order
│   │       ├── Main loop
│   │       ├── Frame timing
│   │       └── Telemetry
│   │
│   ├── platform/
│   │   └── Window
│   │       ├── GLFW wrapper
│   │       ├── Context creation
│   │       ├── Input polling
│   │       └── Event handling
│   │
│   ├── rendering/
│   │   ├── GL
│   │   ├── Shader
│   │   ├── Mesh
│   │   └── Renderer
│   │
│   └── math/
│       ├── Vec3
│       └── Mat4
│
├── docs/
│   ├── ARCHITECTURE.md
│   ├── VERIFICATION.md
│   └── CHANGELOG.md
│
└── CMakeLists.txt
```

### Dependency Direction

```text
sandbox
   |
   v
engine
   |
   v
 GLFW
```

OpenGL types are kept out of the `core` and `platform` public interfaces.

---

## Public Engine API

Gameplay code is built against a small, explicit interface.

### Application

```cpp
Application::valid()
Application::run(onFrame(dt))
Application::window()
Application::renderer()
```

### Window

```cpp
Window::isKeyDown(Key)
Window::requestClose()
Window::getFramebufferSize()
Window::setTitle()
Window::setVSync()
```

### Renderer

```cpp
Renderer::setViewport()
Renderer::setClearColor()
Renderer::clear()
Renderer::drawIndexed(Mesh)
Renderer::stats()
```

### Shader

```cpp
Shader::fromSource(vs, fs)
Shader::bind()
Shader::setMat4()
Shader::setFloat4()
```

GPU resources are move-only.

### Mesh

```cpp
Mesh::create(vertices, indices)
Mesh::bind()
Mesh::indexCount()
```

### Vertex

Milestone 1 uses a fixed vertex layout:

```cpp
struct Vertex {
    float x, y, z;
    float r, g, b;
};
```

### Input

The initial input layer intentionally uses a small key enum and polling API.

A full event/input system will be introduced in a later milestone.

---

## Design Decisions

Milestone 1 deliberately keeps the engine small.

| Decision                      | Reason                                                                                  |
| ----------------------------- | --------------------------------------------------------------------------------------- |
| **GLFW only**                 | Provides windowing, OpenGL context creation, and input with minimal dependency overhead |
| **Hand-scoped OpenGL loader** | Keeps the OpenGL surface explicit without introducing glad/GLEW at M1                   |
| **Custom `Vec3` / `Mat4`**    | M1 only needs a small amount of math and avoids an unnecessary dependency               |
| **Immediate bind-and-draw**   | Only one object exists at this stage                                                    |
| **Variable timestep**         | A fixed timestep is unnecessary until simulation / physics                              |

### Planned Upgrade Paths

**OpenGL loader**

Replace the hand-scoped loader with `glad2` once the engine requires significantly more OpenGL entry points or extensions.

**Math**

Adopt GLM or expand the existing math library once the engine's math requirements grow.

**Rendering**

Move toward batched submission as object counts increase.

**Simulation**

Introduce a fixed timestep once physics and deterministic simulation are added.

---

## Lifetime Rules

The engine follows strict OpenGL resource lifetime ordering.

```text
Application
    |
    +-- creates GL context
    |
    +-- Shader / Mesh created
    |
    +-- Shader / Mesh destroyed
        |
        +-- Application destroyed
```

Normal C++ scope ordering enforces this.

`Application` owns the OpenGL context, so GPU resources must be created **after** the application and destroyed **before** it.

There is intentionally **one `Window` per process**.

GLFW is terminated when the window system shuts down.

---

## Dependencies

### Required

| Dependency         | Purpose                          |
| ------------------ | -------------------------------- |
| **CMake 3.16+**    | Build system                     |
| **C++17 compiler** | Engine compilation               |
| **GLFW**           | Windowing, OpenGL context, input |
| **OpenGL 3.3+**    | Rendering                        |

GLFW is fetched automatically during the first CMake configuration.

> An internet connection is required for the first configure only.

---

## Platform Setup

### Windows

Install one of:

* Visual Studio 2022 Build Tools / MSVC
* MinGW-w64

Recommended VS Code extensions:

```text
ms-vscode.cmake-tools
ms-vscode.cpptools
```

### macOS

Install the Xcode command-line tools:

```bash
xcode-select --install
```

### Linux

Ubuntu / Debian:

```bash
sudo apt install build-essential cmake \
    libx11-dev \
    libxkbcommon-dev \
    libwayland-dev \
    libgl1-mesa-dev
```

---

# Build and Run

## VS Code

1. Open the project folder in VS Code.
2. Run:

```text
CMake: Select a Kit
```

3. Select your compiler.
4. Build with:

```text
F7
```

5. Run the `sandbox` target with:

```text
Shift + F5
```

The first configuration downloads GLFW automatically.

---

## Terminal

Configure:

```bash
cmake -B build
```

Build:

```bash
cmake --build build
```

### Linux / macOS

```bash
./build/bin/sandbox
```

### Windows — Visual Studio generator

```powershell
build\bin\Debug\sandbox.exe
```

### Ninja / Make

```bash
build/bin/sandbox
```

---

## Verification

Milestone 1 has an explicit verification checklist.

See [`docs/VERIFICATION.md`](docs/VERIFICATION.md).

### Build

```text
cmake -B build
cmake --build build
```

Expected:

```text
Build succeeds with zero errors.
```

The first configure may display third-party CMake deprecation warnings from GLFW. These are expected and do not indicate an engine build failure.

### Runtime

Expected initialization:

```text
[Engine] Initialized. OpenGL: <version>
```

OpenGL must be **3.3 or newer**.

The sandbox should open at:

```text
1280 x 720
```

with the title:

```text
Sandbox - M1 Hello Quad
```

The quad should:

* Render with four vertex colors
* Interpolate the colors smoothly
* Rotate around the Y axis
* Show perspective foreshortening
* Demonstrate depth testing
* Display approximately `2 tris`
* Run near monitor refresh rate with VSync
* Pause/resume with `SPACE`
* Exit cleanly with `ESC` or the window close button

---

## Diagnostics

| Symptom                                 | Likely Cause                                              |
| --------------------------------------- | --------------------------------------------------------- |
| `glfwCreateWindow failed`               | No OpenGL 3.3+ driver or unsupported graphics environment |
| `[GLLoader] Missing entry point: glXxx` | Broken or incomplete graphics driver                      |
| `[Shader] ... compile error`            | Shader compilation failure; investigate shader log        |
| No renderer GL error lines              | Expected clean state                                      |

### Remote Desktop

OpenGL context creation can fail under some Remote Desktop configurations.

When possible, run the engine directly against the machine's physical GPU.

---

## Math Verification

`Vec3` and `Mat4` are currently verified transitively through rendering.

A broken:

```text
perspective()
lookAt()
matrix multiplication
```

should result in a visibly incorrect or missing quad.

Dedicated math unit tests will be introduced with the test harness in **Milestone 2**.

---

## Intentional Stopgaps

Milestone 1 is deliberately incomplete.

### Input

Current implementation:

```text
Window::isKeyDown(...)
```

with a small key enum.

Future input system:

```text
Event queue
Edge detection
Mouse input
Bindings
Input actions
```

### Assets

There is no asset pipeline yet.

Shaders currently live as inline strings inside the sandbox.

External shader and mesh loading will arrive in a later milestone.

---

## Documentation

Detailed project documentation lives in `docs/`.

| Document                                  | Purpose                               |
| ----------------------------------------- | ------------------------------------- |
| [`ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Engine structure and design decisions |
| [`VERIFICATION.md`](docs/VERIFICATION.md) | Milestone verification checklist      |
| [`CHANGELOG.md`](docs/CHANGELOG.md)       | Gameplay-facing interface changes     |

---

## Interface Contract

The following APIs form the current gameplay-facing contract:

```text
Application
Window
Renderer
Shader
Mesh
Vertex
Key
```

Any change to these interfaces must be documented in:

```text
docs/CHANGELOG.md
```

**before the change lands.**

This keeps the engine's public surface explicit as development continues.

---

## Milestones

### M1 — Hello Quad

* OpenGL context
* Window system
* Shader pipeline
* Indexed rendering
* Perspective projection
* Depth testing
* Basic input
* Frame timing
* Renderer statistics

### M2 — Input and Camera

* Event queue
* Key edge detection
* Mouse input
* Camera controls
* Math unit tests

### M3 — Asset Pipeline

* External shaders
* Mesh loading
* Asset management
* Runtime resource loading

### M4 — Scene and Batching

* Scene graph
* Multiple objects
* Batched rendering
* Renderer statistics at scale

---

## Current Status

**Milestone 1: Complete**

The current implementation satisfies the initial engine requirements:

```text
✓ Cross-platform C++17 foundation
✓ CMake build
✓ GLFW dependency fetched automatically
✓ Modular engine architecture
✓ Real OpenGL 3.3+ rendering pipeline
✓ Perspective camera
✓ Indexed geometry
✓ Depth testing
✓ Shader abstraction
✓ Mesh abstraction
✓ Frame timing / FPS telemetry
✓ Verification procedure
✓ Architecture documentation
✓ Public interface changelog
```

The engine is intentionally starting with a small surface area so later systems can be added without turning the codebase into a monolithic renderer.
