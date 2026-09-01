#pragma once
//
// Owns the engine boot sequence and the main loop.
//
// Lifecycle:
//   1. Creates the platform window (+ GL context)
//   2. Attaches input, loads GL entry points, initializes the renderer
//   3. run(): input.newFrame() -> pollEvents() -> onFrame(dt) -> swap, until close
//
// Frame contract (M2):
//   - newFrame() runs BEFORE pollEvents(), so pressed()/released() reflect
//     the events arriving during this frame's poll.
//   - dt handed to onFrame is CLAMPED to kMaxDeltaTime (0.1 s): debugger
//     pauses, window drags and stalls must not explode simulation state.
//     Telemetry (title-bar FPS) uses unclamped time.
//
// GL resources (Shader, Mesh, ...) must be created AFTER the Application and
// destroyed BEFORE it (plain C++ scoping handles this -- see sandbox/main.cpp).

#include "platform/Input.h"
#include "platform/Window.h"
#include "rendering/Renderer.h"

#include <cstdint>
#include <functional>
#include <string>

namespace engine {

class Application {
public:
    // Maximum dt passed to onFrame. Public so gameplay knows the contract.
    static constexpr float kMaxDeltaTime = 0.1f;  // seconds

    explicit Application(const WindowDesc& windowDesc = WindowDesc{});
    ~Application() = default;

    bool valid() const;

    Window& window() { return m_window; }
    const Window& window() const { return m_window; }
    Renderer& renderer() { return m_renderer; }
    const Renderer& renderer() const { return m_renderer; }
    Input& input() { return m_input; }
    const Input& input() const { return m_input; }

    // Runs until the window is asked to close. onFrame is responsible for
    // clearing + submitting geometry. dt is clamped to kMaxDeltaTime.
    void run(const std::function<void(float dt)>& onFrame);

    double fps() const { return m_fps; }

private:
    Window m_window;
    Input m_input;        // attached to m_window in the constructor
    Renderer m_renderer;
    bool m_glLoaded = false;
    double m_fps = 0.0;
    std::string m_baseTitle;
    std::uint64_t m_lastDrawCalls = 0;
    std::uint64_t m_lastTriangles = 0;
};

} // namespace engine