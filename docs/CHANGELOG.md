# Changelog

One entry per change to **interfaces gameplay can see**. Written **before** the
change lands. Format: `MAJOR.MINOR.PATCH — <short title>` followed by the
sections Added / Changed / Removed / Deprecated / Fixed as needed.

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
