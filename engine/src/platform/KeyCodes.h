#pragma once
//
// Engine key codes. Single source of truth for the `Key` enum used by the
// input system and gameplay code.
//
// Include order: prefer `#include "platform/Input.h"` from gameplay code -- it
// pulls this in transitively. Include this header directly only when you need
// `Key` but NOT the rest of the Input class (rare).

namespace engine {

enum class Key {
    Escape, Space, Enter, Tab,
    LeftShift, LeftControl,
    Q, W, E, A, S, D, F,
    Digit1, Digit2, Digit3, Digit4,
};

} // namespace engine
