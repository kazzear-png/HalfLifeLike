#pragma once
//
// Owns the engine boot sequence and the main loop.
//
// Lifecycle:
//   1. Creates the platform window (+ GL context)
//   2. Loads GL entry points, initializes the renderer
//   3. run(): poll input -> onFrame(dt) -> swap buffers, until close
//
// GL resources (Shader, Mesh, ...) must be created AFTER the Application and
// destroyed BEFORE it (plain C++ scoping handles this -- see sandbox/main.cpp).

#include "platform/Window.h"
#include "rendering/Renderer.h"

#include <cstdint>
#include <functional>
#include <string>

namespace engine {

class Application {
public:
    explicit Application(const WindowDesc& windowDesc = WindowDesc{});
    ~Application() = default;

    // True when window + GL context + renderer are ready for use.
    bool valid() const;

    Window& window() { return m_window; }
    const Window& window() const { return m_window; }
    Renderer& renderer() { return m_renderer; }
    const Renderer& renderer() const { return m_renderer; }

    // Runs until the window is asked to close. onFrame receives the frame
    // delta in seconds and is responsible for clearing + submitting geometry.
    void run(const std::function<void(float dt)>& onFrame);

    double fps() const { return m_fps; }

private:
    Window m_window;
    Renderer m_renderer;
    bool m_glLoaded = false;
    double m_fps = 0.0;
    std::string m_baseTitle;
    std::uint64_t m_lastDrawCalls = 0;
    std::uint64_t m_lastTriangles = 0;
};

} // namespace engine