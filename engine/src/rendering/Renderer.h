#pragma once

#include "rendering/GL.h"

#include <cstdint>

namespace engine {

class Mesh;

struct RenderStats {
    std::uint64_t drawCalls = 0;
    std::uint64_t triangles = 0;
    void reset() { drawCalls = 0; triangles = 0; }
};

// Owns global GL render state per frame. No GL types leak into this interface.
class Renderer {
public:
    // Call once, after the GL context is current and entry points are loaded.
    void init();

    void setViewport(int x, int y, int width, int height);
    void setClearColor(float r, float g, float b, float a);

    // Begins a frame (resets per-frame stats).
    void clear();

    // Submits an indexed draw; binds the mesh's vertex array.
    void drawIndexed(const Mesh& mesh);

    const RenderStats& stats() const { return m_stats; }

private:
    RenderStats m_stats;
};

} // namespace engine