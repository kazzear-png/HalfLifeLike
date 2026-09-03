#include "core/Application.h"

#include "rendering/GL.h"

#define GLFW_INCLUDE_NONE  // engine ships its own scoped GL loader (rendering/GL.h)
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>

namespace engine {

namespace {

void* getGlProcAddress(const char* name) {
    // GLFW returns a function pointer; GL loaders work with void*.
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

} // namespace

Application::Application(const WindowDesc& windowDesc)
    : m_window(windowDesc), m_baseTitle(windowDesc.title) {
    if (!m_window.valid()) {
        return;
    }

    m_glLoaded = gl::load(&getGlProcAddress);
    if (!m_glLoaded) {
        std::fprintf(stderr, "[Engine] Failed to load GL entry points.\n");
        return;
    }

    m_renderer.init();
    m_input.attach(m_window);

    const char* version = reinterpret_cast<const char*>(gl::GetString(gl::Version));
    std::printf("[Engine] Initialized. OpenGL: %s\n", version ? version : "unknown");
}

bool Application::valid() const {
    return m_window.valid() && m_glLoaded;
}

void Application::run(const std::function<void(float dt)>& onFrame) {
    if (!valid()) {
        std::fprintf(stderr, "[Engine] run() called on an invalid application.\n");
        return;
    }

    using Clock = std::chrono::steady_clock;
    Clock::time_point last = Clock::now();

    double statTimer = 0.0;
    std::uint64_t statFrames = 0;

    while (!m_window.shouldClose()) {
        m_input.newFrame();      // advance edge state BEFORE events arrive
        m_window.pollEvents();   // input callbacks fire during this call

        const Clock::time_point now = Clock::now();
        const float rawDt = std::chrono::duration<float>(now - last).count();
        last = now;

        // Clamp: debugger breaks / stalls must not explode simulation state.
        const float dt = (rawDt > kMaxDeltaTime) ? kMaxDeltaTime : rawDt;

        if (onFrame) {
            onFrame(dt);
        }

        m_lastDrawCalls = m_renderer.stats().drawCalls;
        m_lastTriangles = m_renderer.stats().triangles;

        m_window.swapBuffers();

        // Title-bar telemetry (verification signal). Uses UNCLAMPED time.
        statTimer += rawDt;
        statFrames += 1;
        if (statTimer >= 0.5) {
            m_fps = static_cast<double>(statFrames) / statTimer;

            int fbw = 0, fbh = 0;
            m_window.getFramebufferSize(fbw, fbh);

            char title[160];
            std::snprintf(title, sizeof(title), "%s | %dx%d | %.0f FPS | %llu tris",
                          m_baseTitle.c_str(), fbw, fbh, m_fps,
                          static_cast<unsigned long long>(m_lastTriangles));
            m_window.setTitle(title);

            statTimer = 0.0;
            statFrames = 0;
        }
    }
}

} // namespace engine