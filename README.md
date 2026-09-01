Engine - Milestone 1: Hello Quad

C++17 engine core (Windows / macOS / Linux) rendering an animated quad through a real 3D pipeline: perspective camera, view transform, depth test, indexed drawing.
Prerequisites

    VS Code + extensions: CMake Tools (ms-vscode.cmake-tools), C/C++ (ms-vscode.cpptools)
    C++17 compiler:
        Windows: Visual Studio 2022 Build Tools (MSVC) or MinGW-w64
        macOS: xcode-select --install
        Linux: sudo apt install build-essential cmake libx11-dev libxkbcommon-dev libwayland-dev libgl1-mesa-dev
    CMake 3.16+
    Internet access for the FIRST configure (GLFW is fetched once into build/)

Build & run (VS Code)

    File -> Open Folder... (this folder)
    Ctrl+Shift+P -> CMake: Select a Kit -> choose your compiler
    Build: F7 (CMake: Build)
    Run: Shift+F5 (CMake: Run Without Debugging, target sandbox)
        or run the binary directly:
            VS generator: build\bin\Debug\sandbox.exe
            Ninja/Make:   build/bin/sandbox

Build & run (terminal)

cmake -B build
cmake --build build
./build/bin/sandbox        # Windows VS generator: build\bin\Debug\sandbox.exe

What you should see

A colored quad spinning in 3D under a perspective camera. SPACE pauses the spin,
ESC quits. The title bar shows FPS and triangle count.

Docs:
docs/ARCHITECTURE.md (design)
docs/VERIFICATION.md (milestone checklist)
docs/CHANGELOG.md (interface changes).

docs/ARCHITECTURE.md
Architecture - Milestone 1
Module map

sandbox/        thin client (will become gameplay / editor shell)
engine/
  core/        Application -- boot order, main loop, frame timing, telemetry
  platform/    Window -- GLFW wrapper: context, swap, events, key polling
  rendering/   GL (scoped loader), Shader, Mesh (VAO/VBO/EBO), Renderer (state, clear, draw, stats)
  math/        Vec3, Mat4 (column-major, OpenGL conventions)

Dependency rule: sandbox -> engine -> glfw. GL types do not leak into core or
platform headers.
Decisions (M1)
Decision	Rationale	Exit condition
GLFW as the only third-party dependency	Mature, tiny, permissive. Window + GL context + input in one.	Keep.
Hand-scoped GL loader (rendering/GL.h)	No glad/GLEW/generator dependency at M1; GL surface stays explicit and reviewable.	Replace with glad2 once >~60 entry points or extensions are needed.
Own Vec3/Mat4	M1 needs ~10 ops; no GLM dependency yet.	Adopt GLM (or extend) when math demands grow.
Immediate bind-and-draw submission	One object on screen.	Batched submission when object counts grow.
Variable-delta loop	No fixed-step simulation yet.	Fixed timestep with the physics milestone.
Lifetime rules

    Application owns the GL context. Create Shader/Mesh AFTER the Application
    and destroy them BEFORE it (normal C++ scoping does this - see sandbox/src/main.cpp).
    One Window per process: GLFW is terminated in Window's destructor.

Public API (the contract gameplay codes against)

    Application: valid(), run(onFrame(dt)), window(), renderer()
    Window: isKeyDown(Key), requestClose(), getFramebufferSize(), setTitle(), setVSync()
    Renderer: setViewport(), setClearColor(), clear(), drawIndexed(Mesh), stats()
    Shader: fromSource(vs, fs), bind(), setMat4(), setFloat4() - move-only
    Mesh: create(vertices, indices), bind(), indexCount() - move-only
    Vertex: {x, y, z, r, g, b} - fixed layout for M1

Any change to the above is written up in docs/CHANGELOG.md BEFORE it lands.
Known stopgaps (deliberate)

    Input: Window::isKeyDown polling + a 6-key enum. A real input system (event
    queue, edge-detection API, mouse, bindings) is the next engine milestone.
    No asset pipeline yet: shaders are inline strings in the sandbox for M1.

docs/VERIFICATION.md
Verification - Milestone 1: Hello Quad
Build

     cmake -B build succeeds (first run downloads GLFW; third-party CMake
deprecation warnings are harmless)
     cmake --build build succeeds with zero errors

Runtime

     Console prints [Engine] Initialized. OpenGL: <version> (3.3 or newer)
     1280x720 window opens: "Sandbox - M1 Hello Quad"
     Quad visible; corners red / green / blue / yellow, smoothly interpolated
(proves vertex positions + color attributes + varyings)
     Quad spins about Y; it foreshortens and its near edge renders larger than
the far edge (proves perspective projection - real 3D, not a 2D blit)
     Title bar shows FPS near monitor refresh (vsync) and "2 tris"
     SPACE toggles spin per press (no rapid toggle while held)
     ESC / close button exits with exit code 0

Diagnostics
Symptom	Likely cause
glfwCreateWindow failed	No GL 3.3+ driver (common over Remote Desktop; run on the physical GPU)
[GLLoader] Missing entry point: glXxx	Broken driver install; if persistent, switch loader to glad2 (ARCHITECTURE.md -> Decisions)
[Shader] ... compile error	Should be unreachable; file a bug with the log
No [Renderer] GL error lines during a run	Expected clean state

Math (Vec3/Mat4) is verified transitively in M1: a wrong perspective/lookAt/multiply produces a missing or badly distorted quad, not a clean render.
Unit tests for math land with the test harness in M2.

docs/CHANGELOG.md
Changelog - engine interfaces

One entry per change to interfaces Gameplay can see. Written BEFORE the change lands.
0.1.0 - M1 foundation (initial definition; no prior interfaces changed)

    Defined: engine::Application (run/onFrame contract, GL lifetime rules)
    Defined: engine::Window (isKeyDown, requestClose, getFramebufferSize, setTitle, setVSync)
    Defined: engine::Renderer (setViewport, setClearColor, clear, drawIndexed, stats)
    Defined: engine::Shader, engine::Mesh (move-only GPU resources)
    Defined: engine::Vertex layout {position, color}; engine::Key stopgap enum

7. Build & run

VS Code: open the folder → select a kit (CMake: Select a Kit) → F7 to build → Shift+F5 to run target sandbox. First configure downloads GLFW; that's expected.

Terminal:
bash
 
  
 
 
cmake -B build
cmake --build build
./build/bin/sandbox          # Windows VS generator: build\bin\Debug\sandbox.exe
 
 
8. Status against my operating requirements

    Compiles — full build config provided; single external dependency fetched automatically.
    Test/verification procedure — docs/VERIFICATION.md checklist (build, startup, render, interaction, diagnostics).
    Documentation updated — README, ARCHITECTURE, CHANGELOG created with this milestone.
    Minimal dependencies — GLFW only; loader and math are in-house with documented upgrade paths (glad2 / GLM).
    Modular, not monolithic — core / platform / rendering / math are separate systems behind a narrow public API; the initial Gameplay-facing interface contract is logged in CHANGELOG.md, and all future changes to it get documented there before landing.

Proposed next milestones (your call on ordering): M2 = input system (event queue, edge detection, mouse) + math unit tests + camera controls; M3 = asset pipeline (shaders/meshes from disk); M4 = scene graph + batched renderer stats.