#pragma once
//
// GLFW-backed window + OpenGL context. One window per process (GLFW is
// terminated in the destructor).
//
// M2: keyboard/mouse/cursor input moved to platform/Input (GLFW callbacks).
// Window owns: lifecycle, GL context, presentation, vsync, title, size.

#include <string>

struct GLFWwindow;

namespace engine {

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

    void pollEvents();     // process OS events (input callbacks fire here)
    void swapBuffers();    // present the frame

    // Size of the GL framebuffer in pixels (differs from window size on HiDPI).
    void getFramebufferSize(int& outWidth, int& outHeight) const;

    void setTitle(const std::string& title);
    bool vsync() const { return m_vsync; }
    void setVSync(bool enabled);

    // Engine-internal: raw GLFW handle for platform-layer code (Input) to
    // register callbacks. NOT part of the gameplay-facing API.
    GLFWwindow* nativeHandle() const { return m_window; }

private:
    GLFWwindow* m_window = nullptr;
    bool m_vsync = true;
};

} // namespace engine