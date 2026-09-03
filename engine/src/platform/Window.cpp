#include "platform/Window.h"

#define GLFW_INCLUDE_NONE  // engine ships its own scoped GL loader (rendering/GL.h)
#include <GLFW/glfw3.h>
#include <cstdio>

namespace engine {

namespace {

bool s_glfwInitialized = false;

void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "[GLFW] error %d: %s\n", error, description);
}

} // namespace

Window::Window(const WindowDesc& desc) : m_vsync(desc.vsync) {
    glfwSetErrorCallback(glfwErrorCallback);

    if (!s_glfwInitialized) {
        if (glfwInit() != GLFW_TRUE) {
            std::fprintf(stderr, "[Window] glfwInit() failed\n");
            return;
        }
        s_glfwInitialized = true;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, desc.glVersionMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, desc.glVersionMinor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    // macOS only exposes forward-compatible core profiles.
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    m_window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
    if (m_window == nullptr) {
        std::fprintf(stderr,
                     "[Window] glfwCreateWindow failed. A driver supporting OpenGL %d.%d core is required.\n",
                     desc.glVersionMajor, desc.glVersionMinor);
        return;
    }

    glfwMakeContextCurrent(m_window);
    setVSync(desc.vsync);
}

Window::~Window() {
    if (m_window != nullptr) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    if (s_glfwInitialized) {
        glfwTerminate();
        s_glfwInitialized = false;
    }
}

bool Window::shouldClose() const {
    return m_window != nullptr && glfwWindowShouldClose(m_window) != 0;
}

void Window::requestClose() {
    if (m_window != nullptr) {
        glfwSetWindowShouldClose(m_window, 1);
    }
}

void Window::pollEvents() {
    glfwPollEvents();
}

void Window::swapBuffers() {
    if (m_window != nullptr) {
        glfwSwapBuffers(m_window);
    }
}

void Window::getFramebufferSize(int& outWidth, int& outHeight) const {
    outWidth = 0;
    outHeight = 0;
    if (m_window != nullptr) {
        glfwGetFramebufferSize(m_window, &outWidth, &outHeight);
    }
}

void Window::setTitle(const std::string& title) {
    if (m_window != nullptr) {
        glfwSetWindowTitle(m_window, title.c_str());
    }
}

void Window::setVSync(bool enabled) {
    m_vsync = enabled;
    if (m_window != nullptr) {
        glfwSwapInterval(enabled ? 1 : 0);
    }
}

} // namespace engine