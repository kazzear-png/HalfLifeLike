# Engine core: platform, rendering, math, application framework.
add_library(engine STATIC
    src/core/Application.cpp
    src/math/Mat4.cpp
    src/platform/Window.cpp
    src/rendering/GL.cpp
    src/rendering/Mesh.cpp
    src/rendering/Renderer.cpp
    src/rendering/Shader.cpp
)

# Includes are namespaced from src/, e.g. #include "rendering/Shader.h"
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(engine PUBLIC glfw)