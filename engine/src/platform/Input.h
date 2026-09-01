#pragma once
//
// Event-driven input system (M2). Owns keyboard/mouse state and cursor modes.
//
// Frame contract (driven by Application::run):
//   1. input.newFrame()    -- previous := current, resets per-frame deltas
//   2. window.pollEvents() -- GLFW callbacks update current state
//   3. gameplay queries    -- isKeyDown / pressed / released / mouse deltas
//
// pressed()  = down this frame AND up last frame (edge)
// released() = up this frame AND down last frame (edge)
//
// CursorMode::Locked disables the cursor and (where supported) enables raw
// mouse motion; mouseDX/mouseDY report accumulated motion since newFrame().
// Raw motion is unavailable on some platforms (e.g. macOS); locked-cursor
// deltas still work there, they just don't bypass OS acceleration.
//
// Known limitation: a press+release fully contained inside ONE frame is not
// reported (state snapshot, not an event queue). Fine for camera controls;
// revisit when text input / rebinding / fast-tap fidelity is needed.

#include <array>
#include <cstdint>

struct GLFWwindow;

namespace engine {

class Window;

enum class Key {
    Escape, Space, Enter, Tab,
    LeftShift, LeftControl,
    Q, W, E, A, S, D,
    Digit1, Digit2, Digit3,
};

enum class MouseButton { Left = 0, Middle = 1, Right = 2 };

enum class CursorMode { Normal, Hidden, Locked };

class Input {
public:
    // Registers GLFW callbacks on the window. Called by Application after the
    // window exists. Input is invalidated if the Window is destroyed.
    // NOTE: Input exclusively owns the window's GLFW user pointer; if Window
    // ever needs its own callbacks, introduce a shared per-window context.
    void attach(Window& window);

    // Advances frame-edge state. Called by Application::run BEFORE pollEvents.
    void newFrame();

    // Keyboard
    bool isKeyDown(Key key) const;
    bool wasKeyDown(Key key) const;   // state at the previous frame
    bool pressed(Key key) const;      // went down this frame
    bool released(Key key) const;     // went up this frame

    // Mouse buttons (same edge semantics)
    bool isMouseButtonDown(MouseButton button) const;
    bool wasMouseButtonDown(MouseButton button) const;
    bool mousePressed(MouseButton button) const;
    bool mouseReleased(MouseButton button) const;

    // Accumulated since the last newFrame():
    float mouseDX() const { return m_mouseDX; }
    float mouseDY() const { return m_mouseDY; }
    float scrollDelta() const { return m_scrollDelta; }

    // Cursor
    void setCursorMode(CursorMode mode);
    CursorMode cursorMode() const { return m_cursorMode; }
    bool cursorLocked() const { return m_cursorMode == CursorMode::Locked; }

private:
    // GLFW callbacks (registered in attach)
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void windowFocusCallback(GLFWwindow* window, int focused);

    void onKey(int glfwKey, int action);
    void onMouseButton(int glfwButton, int action);
    void onCursorPos(double xpos, double ypos);
    void onFocusLost();

    GLFWwindow* m_window = nullptr;

    std::array<std::uint8_t, 512> m_current{};   // GLFW key code -> down
    std::array<std::uint8_t, 512> m_previous{};
    std::uint8_t m_mouseCurrent[8]   = {};
    std::uint8_t m_mousePrevious[8]  = {};

    float m_mouseDX      = 0.0f;
    float m_mouseDY      = 0.0f;
    float m_scrollDelta  = 0.0f;

    double m_lastCursorX       = 0.0;
    double m_lastCursorY       = 0.0;
    bool   m_cursorBaselineValid = false;

    CursorMode m_cursorMode = CursorMode::Normal;
};

} // namespace engine