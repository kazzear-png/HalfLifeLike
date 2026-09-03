# Engine core: platform, input, rendering, assets, math, application framework.
add_library(engine STATIC
    src/assets/OBJ.cpp
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

# M2/M3: verification targets (micro harness; see docs/ARCHITECTURE.md decisions).
if(BUILD_TESTING)
    set(engine_math_tests_src "${CMAKE_CURRENT_SOURCE_DIR}/tests/math_tests.cpp")
    if(EXISTS "${engine_math_tests_src}")
        add_executable(engine_math_tests "${engine_math_tests_src}")
        target_link_libraries(engine_math_tests PRIVATE engine)
        add_test(NAME math_tests COMMAND engine_math_tests)
    else()
        message(STATUS "Skipping engine_math_tests: ${engine_math_tests_src} not found")
    endif()

    set(engine_obj_tests_src "${CMAKE_CURRENT_SOURCE_DIR}/tests/obj_tests.cpp")
    if(EXISTS "${engine_obj_tests_src}")
        add_executable(engine_obj_tests "${engine_obj_tests_src}")
        target_link_libraries(engine_obj_tests PRIVATE engine)
        add_test(NAME obj_tests COMMAND engine_obj_tests
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    else()
        message(STATUS "Skipping engine_obj_tests: ${engine_obj_tests_src} not found")
    endif()
endif()