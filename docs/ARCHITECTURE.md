# Architecture

Design overview of the engine core. Covers module boundaries, key decisions,
GL lifetime rules, the main-loop frame contract, and the public API surface
that gameplay codes against.

**Current milestone:** M2 — *Event-driven input + fly camera*.

---

## Module map

```
sandbox/                    thin client (will become gameplay / editor shell)
engine/
├── src/
│   ├── core/               Application — boot order, main loop, frame timing, telemetry
│   ├── platform/           Window (GLFW), Input (callbacks/edge state), KeyCodes (enum Key)
│   ├── rendering/          GL (scoped loader), Shader, Mesh (VAO/VBO/EBO), Renderer, Camera
│   └── math/               Vec3, Mat4 (column-major, OpenGL conventions)
└── tests/                  math_tests.cpp — ctest target
```

Dependency rule: **`sandbox → engine → glfw`**. GL types do not leak into
`core` or `platform` public headers.

---

## Decisions

| Decision                                | Rationale                                                        | Exit condition                                          |
|-----------------------------------------|------------------------------------------------------------------|---------------------------------------------------------|
| GLFW as the only third-party dependency | Mature, tiny, permissive; window + GL context + input in one.    | Keep.                                                   |
| Hand-scoped GL loader (`rendering/GL.h`) | No glad/GLEW/generator dependency at M1/M2; GL surface stays explicit and reviewable. | Replace with glad2 when >~60 entry points or extensions are needed. |
| Own `Vec3` / `Mat4`                     | M1/M2 need ~10 ops; no GLM dependency yet.                       | Adopt GLM (or extend) when math demands grow.           |
| Immediate bind-and-draw submission      | One object on screen.                                            | Batched submission when object counts grow.             |
| Variable-delta loop, `dt` clamped to 0.1 s | No fixed-step simulation yet.                                  | Fixed timestep with the physics milestone.              |
| State-snapshot input (not an event queue) | Sufficient for camera controls; press/release edges captured per frame. | Event queue when text input / rebinding / fast-tap fidelity is needed. |
| Minimal math test harness               | Project rule: minimize dependencies.                             | Switch to doctest/Catch2 when test surface grows.       |
| `Key` enum lives in `platform/KeyCodes.h` (single source of truth) | Avoids MSVC C2011 redefinition when both `Input.h` and `KeyCodes.h` are included transitively; one place to edit when adding keys. | Keep.                                                   |

---

## Lifetime rules

- `Application` owns the GL context. Create `Shader` / `Mesh` **after** the
  `Application` and destroy them **before** it (normal C++ scoping handles
  this — see `sandbox/src/main.cpp`).
- One `Window` per process: GLFW is terminated in `Window`'s destructor.
- `Shader` and `Mesh` are move-only; their GL handles are released on
  destruction. Copy is deleted to prevent double-frees.

---

## Frame contract

Driven by `Application::run`:

1. `input.newFrame()` — `previous := current`; reset per-frame mouse / scroll deltas.
2. `window.pollEvents()` — GLFW callbacks update current state.
3. Gameplay queries `isKeyDown` / `pressed` / `released` / `mouseDX` / `mouseDY` / `scrollDelta`.
4. Frame callback receives `dt`, clamped to `Application::kMaxDeltaTime` (`0.1 s`).
   Telemetry (title-bar FPS) uses **unclamped** time so visible FPS isn't artificially capped by the clamp.

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
    void setViewport(int x, int y, int width, int height);
    void setClearColor(float r, float g, float b, float a);
    void clear();                        // begins a frame; resets per-frame stats
    void drawIndexed(const Mesh& mesh);
    const RenderStats& stats() const;
};

// rendering/Shader.h — move-only
class Shader {
public:
    static Shader fromSource(const char* vertexSource, const char* fragmentSource);
    void bind() const;
    void setMat4(const char* name, const Mat4& value);
    void setFloat4(const char* name, float x, float y, float z, float w);
    bool valid() const;
};

// rendering/Mesh.h — move-only
struct Vertex { float x, y, z; float r, g, b; };   // fixed layout for M1/M2
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
```

---

## GL surface (currently loaded entry points)

The scoped loader in `rendering/GL.h` resolves only what the engine uses.
Upgrade path: replace with glad2 once coverage grows beyond ~60 entry points.

Currently loaded groups:

- **Queries:** `GetString`, `GetError`
- **State:** `Enable`, `Viewport`, `ClearColor`, `Clear`
- **Drawing:** `DrawElements`
- **Shader / program:** `CreateShader`, `ShaderSource`, `CompileShader`,
  `GetShaderiv`, `GetShaderInfoLog`, `DeleteShader`, `CreateProgram`,
  `AttachShader`, `LinkProgram`, `GetProgramiv`, `GetProgramInfoLog`,
  `DeleteProgram`, `UseProgram`
- **Uniforms:** `GetUniformLocation`, `UniformMatrix4fv`, `Uniform4f`
- **Buffers / vertex arrays:** `GenVertexArrays`, `BindVertexArray`,
  `DeleteVertexArrays`, `GenBuffers`, `BindBuffer`, `BufferData`,
  `DeleteBuffers`, `EnableVertexAttribArray`, `VertexAttribPointer`

On Windows, the loader falls back to `opengl32.dll` (via `GetProcAddress`) for
any entry point `glfwGetProcAddress` does not return — some Windows drivers
do not expose GL 1.1 entry points through `wglGetProcAddress`.
