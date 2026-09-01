# Thin client that drives the engine (stands in for gameplay/editor for now).
add_executable(sandbox
    src/main.cpp
)
target_link_libraries(sandbox PRIVATE engine)