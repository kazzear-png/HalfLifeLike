# Verification

Checklist for verifying that a given build behaves correctly. Run through it
after every milestone that touches build / window / input / rendering / math.

**Current milestone:** M2 — *Event-driven input + fly camera*.

---

## Build

- [ ] `cmake -B build` succeeds. First run downloads GLFW into `build/_deps/`;
      third-party CMake deprecation warnings are harmless.
- [ ] `cmake --build build` succeeds with zero errors on the configured compiler:
      - MSVC (Windows VS generator)
      - GCC / Clang (Ninja or Make)
- [ ] `ctest --test-dir build --output-on-failure` runs `math_tests` and reports `58 checks, 0 failure(s)`.

## Runtime

- [ ] Console prints `[Engine] Initialized. OpenGL: <version>` where `<version>`
      is `3.3` or newer.
- [ ] A `1280×720` window opens titled `Sandbox - M2 Fly Camera`.
- [ ] The reference floor (dark grey) and a spinning quad are visible.
- [ ] The quad's corners are red / green / blue / yellow, smoothly interpolated
      (proves vertex positions + color attributes + varyings).
- [ ] The quad spins about Y; it foreshortens, and its near edge renders larger
      than the far edge (proves perspective projection — real 3D, not a 2D blit).
- [ ] The title bar shows `width × height | FPS | tris`, with FPS near monitor
      refresh when vsync is on.

## Input / controls

- [ ] **Left click** locks the cursor; the cursor disappears and mouse motion
      rotates the camera (yaw + pitch, with pitch clamped at ±~89.95°).
- [ ] **ESC** when locked unlocks the cursor.
- [ ] **ESC** when already unlocked requests window close (exit code 0 on next loop).
- [ ] **W / A / S / D** move the camera; `W` follows the full view direction
      (including pitch).
- [ ] **E / Q** move the camera up / down in world space.
- [ ] **SPACE** toggles the quad's spin per press — no rapid toggling while held.
- [ ] Closing the window via the close button exits cleanly with code 0.

## Frame timing / robustness

- [ ] Pausing in a debugger for >0.1 s does not explode simulation state
      (proves `dt` is clamped to `Application::kMaxDeltaTime`).
- [ ] Resizing the window keeps the quad correctly proportioned (aspect is
      recomputed per frame from the framebuffer size).
- [ ] Losing window focus mid-press does not leave a key stuck down
      (`Input::onFocusLost` clears key + mouse state).

## Diagnostics

| Symptom                                          | Likely cause                                                                                       |
|--------------------------------------------------|----------------------------------------------------------------------------------------------------|
| `glfwCreateWindow failed`                        | No GL 3.3+ driver (common over Remote Desktop; run on the physical GPU).                           |
| `[GLLoader] Missing entry point: glXxx`          | Broken driver install. If persistent, switch the loader to glad2 (see `ARCHITECTURE.md` → Decisions). |
| `[Shader] ... compile error`                     | Should be unreachable with the bundled shaders; file a bug with the log.                          |
| `[Renderer] GL error 0x%04x raised during drawIndexed` | Unexpected GL state. Should not appear during a clean run.                                    |
| `Skipping engine_math_tests: ... not found`      | The CMakeLists expected `engine/tests/math_tests.cpp` (plural); the file was renamed or never renamed. |

## Math

Verified in two ways:

1. **Transitively at runtime** — a wrong `perspective` / `lookAt` / `multiply`
   produces a missing or badly distorted quad, not a clean render.
2. **Directly via `engine_math_tests`** — covers `Vec3` basics (`dot`,
   `cross`, `length`, `normalize`), identity laws, translation, rotation
   (incl. length preservation), perspective depth mapping (near → NDC -1,
   far → NDC +1), `lookAt` mapping, and camera yaw/pitch clamping.

Expected output:

```
[math] Vec3 basics
[math] identity laws
[math] translation
[math] rotation
[math] perspective
[math] lookAt
[math] camera
[math] 58 checks, 0 failure(s)
```
