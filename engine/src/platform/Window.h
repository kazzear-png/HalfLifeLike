#pragma once
//
// GLFW-backed window + OpenGL context. One window per process (GLFW is
// terminated in the destructor).
//
// NOTE: key queries are a stopgap. A proper input system (event queue, edge
// detection, mouse, bindings) is the next engine milestone.

#include <string>

struct GLFWwindow;

namespace engine {

// Minimal key set -- grows with the input system.
enum class Key {
    Escape, Space,
    W, A, S, D,
};

struct WindowDesc {
    std::string title  = "Engine";
    int width          = 1280;
    int height         = 720;
    bool vsync         = true;
    int glVersionMajor = 3;   // minimum required GL version (core profile)
    int glVersionMinor = 3;
};

class Window {
public:
    explicit Window(const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool valid() const { return m_window != nullptr; }

    bool shouldClose() const;
    void requestClose();

    void pollEvents();     // process OS events / input
    void swapBuffers();    // present the frame

    bool isKeyDown(Key key) const;

    // Size of the GL framebuffer in pixels (differs from window size on HiDPI).
    void getFramebufferSize(int& outWidth, int& outHeight) const;

    void setTitle(const std::string& title);
    bool vsync() const { return m_vsync; }
    void setVSync(bool enabled);

private:
    GLFWwindow* m_window = nullptr;
    bool m_vsync = true;
};

} // namespace engine