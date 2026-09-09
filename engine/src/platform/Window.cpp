#include "platform/Window.h"

#define GLFW_INCLUDE_NONE  // engine ships its own scoped GL loader (rendering/GL.h)
#include <GLFW/glfw3.h>
#include <cstdio>

namespace engine {

namespace {

// M5.5: GLFW global lifetime is REFERENCE-COUNTED, not a single bool. The
// old single-shot "last window terminates GLFW" logic broke the moment two
// Window instances overlapped: A ok, B ok, A destroyed -> glfwTerminate()
// with B still alive (GLFW state freed under a live window); or a failed
// B construction left the flag set after its destructor terminated. Each
// successfully-created window owns exactly one reference; the count hitting
// zero (and only zero) terminates.
int s_glfwRefCount = 0;

void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "[GLFW] error %d: %s\n", error, description);
}

} // namespace

Window::Window(const WindowDesc& desc) : m_vsync(desc.vsync) {
    glfwSetErrorCallback(glfwErrorCallback);

    if (s_glfwRefCount == 0) {
        if (glfwInit() != GLFW_TRUE) {
            std::fprintf(stderr, "[Window] glfwInit() failed\n");
            return;
        }
    }

    // This Window instance now owns one GLFW lifetime reference (released
    // on destruction, or below if window creation fails).
    ++s_glfwRefCount;

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
        // Construction failed AFTER acquiring the reference: release it
        // here so the destructor (which sees m_window == nullptr and does
        // nothing) cannot decrement it twice.
        --s_glfwRefCount;
        if (s_glfwRefCount == 0) {
            glfwTerminate();
        }
        return;
    }

    glfwMakeContextCurrent(m_window);
    setVSync(desc.vsync);
}

Window::~Window() {
    if (m_window != nullptr) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;

        if (s_glfwRefCount > 0) {
            --s_glfwRefCount;
        }
        if (s_glfwRefCount == 0) {
            glfwTerminate();
        }
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

bool Window::isFocused() const {
    return m_window != nullptr && glfwGetWindowAttrib(m_window, GLFW_FOCUSED) != 0;
}

bool Window::isIconified() const {
    return m_window != nullptr && glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) != 0;
}

void Window::waitEvents(double timeoutSeconds) {
    if (m_window == nullptr) {
        return;
    }
    if (timeoutSeconds > 0.0) {
        glfwWaitEventsTimeout(timeoutSeconds);
    } else {
        glfwWaitEvents();
    }
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