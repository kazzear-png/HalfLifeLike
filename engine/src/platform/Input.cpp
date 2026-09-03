#include "platform/Input.h"

#include "platform/Window.h"

#define GLFW_INCLUDE_NONE  // engine ships its own scoped GL loader (rendering/GL.h)
#include <GLFW/glfw3.h>

#include <cstdio>

namespace engine {

namespace {

int toGlfwKey(Key key) {
    switch (key) {
        case Key::Escape:      return GLFW_KEY_ESCAPE;
        case Key::Space:       return GLFW_KEY_SPACE;
        case Key::Enter:       return GLFW_KEY_ENTER;
        case Key::Tab:         return GLFW_KEY_TAB;
        case Key::LeftShift:   return GLFW_KEY_LEFT_SHIFT;
        case Key::LeftControl: return GLFW_KEY_LEFT_CONTROL;
        case Key::Q:           return GLFW_KEY_Q;
        case Key::W:           return GLFW_KEY_W;
        case Key::E:           return GLFW_KEY_E;
        case Key::A:           return GLFW_KEY_A;
        case Key::S:           return GLFW_KEY_S;
        case Key::D:           return GLFW_KEY_D;
        case Key::F:           return GLFW_KEY_F;
        case Key::V:           return GLFW_KEY_V;
        case Key::Digit1:      return GLFW_KEY_1;
        case Key::Digit2:      return GLFW_KEY_2;
        case Key::Digit3:      return GLFW_KEY_3;
        case Key::Digit4:      return GLFW_KEY_4;
        default:               return GLFW_KEY_UNKNOWN;
    }
}

int toGlfwButton(MouseButton button) {
    switch (button) {
        case MouseButton::Left:   return GLFW_MOUSE_BUTTON_LEFT;
        case MouseButton::Middle: return GLFW_MOUSE_BUTTON_MIDDLE;
        case MouseButton::Right:  return GLFW_MOUSE_BUTTON_RIGHT;
        default:                  return -1;
    }
}

Input* inputFor(GLFWwindow* window) {
    return static_cast<Input*>(glfwGetWindowUserPointer(window));
}

} // namespace

void Input::attach(Window& window) {
    m_window = window.nativeHandle();
    if (m_window == nullptr) {
        std::fprintf(stderr, "[Input] attach() called on an invalid window.\n");
        return;
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetKeyCallback(m_window, &Input::keyCallback);
    glfwSetCursorPosCallback(m_window, &Input::cursorPosCallback);
    glfwSetMouseButtonCallback(m_window, &Input::mouseButtonCallback);
    glfwSetScrollCallback(m_window, &Input::scrollCallback);
    glfwSetWindowFocusCallback(m_window, &Input::windowFocusCallback);

    // Establish the cursor-delta baseline so the first frame has no jump.
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(m_window, &x, &y);
    m_lastCursorX = x;
    m_lastCursorY = y;
    m_cursorBaselineValid = true;
}

void Input::newFrame() {
    m_previous = m_current;
    for (int i = 0; i < 8; ++i) {
        m_mousePrevious[i] = m_mouseCurrent[i];
    }
    m_mouseDX = 0.0f;
    m_mouseDY = 0.0f;
    m_scrollDelta = 0.0f;
}

bool Input::isKeyDown(Key key) const {
    const int k = toGlfwKey(key);
    if (k < 0 || k >= static_cast<int>(m_current.size())) return false;
    return m_current[k] != 0;
}

bool Input::wasKeyDown(Key key) const {
    const int k = toGlfwKey(key);
    if (k < 0 || k >= static_cast<int>(m_previous.size())) return false;
    return m_previous[k] != 0;
}

bool Input::pressed(Key key) const  { return isKeyDown(key) && !wasKeyDown(key); }
bool Input::released(Key key) const { return wasKeyDown(key) && !isKeyDown(key); }

bool Input::isMouseButtonDown(MouseButton button) const {
    const int i = toGlfwButton(button);
    return i >= 0 && m_mouseCurrent[i] != 0;
}

bool Input::wasMouseButtonDown(MouseButton button) const {
    const int i = toGlfwButton(button);
    return i >= 0 && m_mousePrevious[i] != 0;
}

bool Input::mousePressed(MouseButton button) const  { return isMouseButtonDown(button) && !wasMouseButtonDown(button); }
bool Input::mouseReleased(MouseButton button) const { return wasMouseButtonDown(button) && !isMouseButtonDown(button); }

void Input::setCursorMode(CursorMode mode) {
    if (m_window == nullptr || m_cursorMode == mode) return;
    m_cursorMode = mode;

    int glfwMode = GLFW_CURSOR_NORMAL;
    switch (mode) {
        case CursorMode::Normal: glfwMode = GLFW_CURSOR_NORMAL;  break;
        case CursorMode::Hidden: glfwMode = GLFW_CURSOR_HIDDEN;  break;
        case CursorMode::Locked: glfwMode = GLFW_CURSOR_DISABLED; break;
    }
    glfwSetInputMode(m_window, GLFW_CURSOR, glfwMode);

    if (mode == CursorMode::Locked && glfwRawMouseMotionSupported()) {
        glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    // Mode transitions teleport the cursor; re-baseline so the jump is not
    // reported as a mouse delta.
    m_cursorBaselineValid = false;
}

// ---- GLFW callbacks ----

void Input::keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (Input* self = inputFor(window)) self->onKey(key, action);
}

void Input::onKey(int glfwKey, int action) {
    if (glfwKey < 0 || glfwKey >= static_cast<int>(m_current.size())) return;
    if (action == GLFW_REPEAT) return;  // repeats do not change edge state
    m_current[glfwKey] = (action == GLFW_PRESS) ? 1 : 0;
}

void Input::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (Input* self = inputFor(window)) self->onCursorPos(xpos, ypos);
}

void Input::onCursorPos(double xpos, double ypos) {
    if (!m_cursorBaselineValid) {
        m_cursorBaselineValid = true;
        m_lastCursorX = xpos;
        m_lastCursorY = ypos;
        return;
    }
    m_mouseDX += static_cast<float>(xpos - m_lastCursorX);
    m_mouseDY += static_cast<float>(ypos - m_lastCursorY);
    m_lastCursorX = xpos;
    m_lastCursorY = ypos;
}

void Input::mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    if (Input* self = inputFor(window)) self->onMouseButton(button, action);
}

void Input::onMouseButton(int glfwButton, int action) {
    if (glfwButton < 0 || glfwButton >= 8) return;
    m_mouseCurrent[glfwButton] = (action == GLFW_PRESS) ? 1 : 0;
}

void Input::scrollCallback(GLFWwindow* window, double, double yoffset) {
    if (Input* self = inputFor(window)) self->m_scrollDelta += static_cast<float>(yoffset);
}

void Input::windowFocusCallback(GLFWwindow* window, int focused) {
    if (Input* self = inputFor(window)) {
        if (focused == GLFW_FALSE) self->onFocusLost();
    }
}

void Input::onFocusLost() {
    // Keys can get stuck 'down' if the window loses focus mid-press.
    m_current.fill(0);
    for (int i = 0; i < 8; ++i) m_mouseCurrent[i] = 0;
}

} // namespace engine