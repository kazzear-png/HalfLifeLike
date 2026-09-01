# Engine core: platform, input, rendering, math, application framework.
add_library(engine STATIC
    src/core/Application.cpp
    src/math/Mat4.cpp
    src/platform/Input.cpp
    src/platform/Window.cpp
    src/rendering/Camera.cpp
    src/rendering/GL.cpp
    src/rendering/Mesh.cpp
    src/rendering/Renderer.cpp
    src/rendering/Shader.cpp
)

# Includes are namespaced from src/, e.g. #include "rendering/Shader.h"
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(engine PUBLIC glfw)

# M2: math verification (micro harness; see docs/ARCHITECTURE.md decisions).
if(BUILD_TESTING)
    add_executable(engine_math_tests tests/math_tests.cpp)
    target_link_libraries(engine_math_tests PRIVATE engine)
    add_test(NAME math_tests COMMAND engine_math_tests)
endif()