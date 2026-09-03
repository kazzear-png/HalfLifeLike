#pragma once

#include "rendering/GL.h"
#include "rendering/Shader.h"

#include <cstdint>

namespace engine {

class Mesh;

struct RenderStats {
    std::uint64_t drawCalls = 0;
    std::uint64_t triangles = 0;
    void reset() { drawCalls = 0; triangles = 0; }
};

// Owns global GL render state per frame. No GL types leak into this interface.
//
// M3 frame flow (HDR pipeline -- the first slice of the
// scene -> HDR -> exposure -> tone map -> present chain):
//
//   renderer.initHDR(w, h);          // once after init(); allocates targets
//   ...
//   renderer.beginFrame();           // bind HDR target + clear
//   ... submit lit draws ...
//   renderer.endFrame();             // MSAA resolve + exposure + ACES tonemap
//
// If initHDR() was never called (or failed), begin/end degrade gracefully to
// direct backbuffer rendering (legacy M2 behavior).
class Renderer {
public:
    // Call once, after the GL context is current and entry points are loaded.
    void init();

    // Releases owned GL objects. Called by the destructor (which runs while
    // the context is alive, per Application's member destruction order).
    ~Renderer();

    // --- HDR pipeline (M3) ---
    // Allocates the offscreen HDR target (RGBA16F) + depth, with 4x MSAA when
    // supported, and the tonemap program. Safe to call again to recreate.
    // Returns false (and continues in direct mode) if HDR targets are
    // unsupported on this driver.
    bool initHDR(int width, int height, int msaaSamples = 4);

    // Recreates targets when the framebuffer size changes.
    void resizeHDR(int width, int height);

    bool hdrActive() const { return m_hdrActive; }

    // Linear exposure multiplier applied by the tonemap pass. Scroll-driven
    // in the sandbox; the perceptual tuning knob of the pipeline.
    void setExposure(float linearExposure);
    float exposure() const { return m_exposure; }

    // --- frame ---
    void setViewport(int x, int y, int width, int height);
    void setClearColor(float r, float g, float b, float a);

    // Begins a frame (binds + clears the render target, resets per-frame stats).
    void beginFrame();

    // Ends a frame: resolve (MSAA -> texture) + tonemap to the backbuffer.
    void endFrame();

    // Submits an indexed draw; binds the mesh's vertex array.
    void drawIndexed(const Mesh& mesh);

    const RenderStats& stats() const { return m_stats; }

    // Verification helper (headless screenshot mode): reads the CURRENT
    // backbuffer (post-tonemap) as RGBA8, bottom-up rows.
    bool readBackbufferPixels(int width, int height, unsigned char* outRgba);

private:
    // --- GL object handles (kept internal; never exposed through the API) ---
    gl::GLuint m_msaaFbo        = 0;  // multisample scene target
    gl::GLuint m_msaaColorRb    = 0;  // RGBA16F multisample color
    gl::GLuint m_msaaDepthRb    = 0;  // depth multisample
    gl::GLuint m_resolveFbo     = 0;  // resolve target
    gl::GLuint m_resolveColorTex= 0;  // RGBA16F texture sampled by the tonemap
    gl::GLuint m_emptyVao       = 0;  // for the gl_VertexID fullscreen triangle
    Shader m_tonemapShader;           // engine-owned tonemap program (move-only)
    gl::GLint  m_uHDRLocation      = -1;
    gl::GLint  m_uExposureLocation = -1;

    bool m_hdrActive = false;
    int  m_width  = 0;
    int  m_height = 0;
    int  m_samples = 0;   // 1 = plain FBO, 4 = MSAA (0 = HDR not initialized)

    float m_exposure = 1.0f;
    float m_clearColor[4] = { 0.10f, 0.12f, 0.15f, 1.0f };

    RenderStats m_stats;

    // Internal helpers (require a current GL context).
    void destroyHDRTargets();
    bool createHDRTargets(int width, int height, int samples);
    bool createTonemapProgram();
    void drawTonemapPass();
};

} // namespace engine
