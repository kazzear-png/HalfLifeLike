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
    // Restore scene FBO/viewport after an auxiliary pass, without clearing.
    void bindSceneTarget(int width, int height);

    // Ends a frame: resolve (MSAA -> texture) + tonemap to the backbuffer.
    void endFrame();

    // Submits an indexed draw; binds the mesh's vertex array.
    void drawIndexed(const Mesh& mesh);

    const RenderStats& stats() const { return m_stats; }

    // Verification helper (headless screenshot mode): reads the CURRENT
    // backbuffer (post-tonemap) as RGBA8, bottom-up rows.
    bool readBackbufferPixels(int width, int height, unsigned char* outRgba);

    // --- benchmark GPU timing (M3.3) ---
    // Wraps each frame in a GL_TIME_ELAPSED timer query. M5.2: results are
    // read through a small RING of query objects with a NON-BLOCKING
    // availability poll (GL_QUERY_RESULT_AVAILABLE), so the CPU never stalls
    // waiting for the GPU inside the frame loop -- the old one-frame-lag
    // blocking read (GL_QUERY_RESULT) sat at the top of beginFrame() INSIDE
    // the benchmark's measured cpu-ms window, which is why cpu ms mirrored
    // gpu ms on GPU-bound frames (the CPU finished submitting, then slept in
    // that read until the GPU retired the previous frame).
    //
    // M5.6: the ring is 64 deep and the overflow path no longer blocks. On
    // the hardware ledger a 4-deep ring overflowed EVERY frame (real app CPU
    // ~1.1 ms vs GPU ~6.2 ms: the GPU falls behind ~5 ms per frame, so the
    // slot up for re-arm was never retired) -- the "rare" blocking fallback
    // was the steady-state path and cpu ms STILL mirrored gpu ms. Now an
    // unretired slot means this frame simply does not arm a query (the slot
    // drains later; that frame reports n/a and the benchmark skips it): a
    // benchmark must never wait on the thing it measures. A blocked CPU now
    // can only be (a) real app/GL-driver submit work or (b) the present
    // queue at SwapBuffers -- both measured and reported separately by the
    // sandbox (submit ms / gpu-wait ms / present ms).
    void enableGpuTiming(bool enable);
    float lastGpuFrameMs() const { return m_gpuFrameMs; }
    bool gpuTimingActive() const { return m_gpuTimingOn; }

    // M5.4: wall time beginFrame() spent in the timer-drain section this
    // frame (query availability polls + result reads). With the skip-not-
    // block overflow policy this should sit in the microseconds; a large
    // value means the DRIVER blocks inside query reads and the report will
    // show it as its own line.
    float lastDrainWaitMs() const { return m_drainWaitMs; }

    // M5.4 telemetry: total query results drained / frames where arming was
    // skipped because the oldest slot was still in flight.
    std::uint64_t timerSamplesCollected() const { return m_timerSamples; }
    std::uint64_t timerOverflowSkips() const { return m_timerOverflowSkips; }

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

    // M3.3 / M5.2 / M5.4: GPU frame timing (timer-query ring, non-blocking
    // readback, skip-not-block overflow).
    static constexpr int kTimerRingSize = 64;
    bool m_gpuTimingWanted = false;   // caller asked for timing
    bool m_gpuTimingOn    = false;    // driver accepted it
    bool m_timerSlotPending[kTimerRingSize] = {};
    gl::GLuint m_timerQueries[kTimerRingSize] = {};
    int  m_timerRingIndex = 0;         // next slot to arm (oldest pending first)
    bool m_timerArmedThisFrame = false; // M5.4: BeginQuery issued (endFrame pairs it)
    float m_gpuFrameMs = -1.0f;       // < 0 == "n/a" (no completed sample this frame)
    float m_drainWaitMs = 0.0f;       // M5.4: drain-section wall time this frame
    std::uint64_t m_timerSamples = 0;       // M5.4: results drained (lifetime)
    std::uint64_t m_timerOverflowSkips = 0; // M5.4: frames that armed no query

    // Internal helpers (require a current GL context).
    void destroyHDRTargets();
    bool createHDRTargets(int width, int height, int samples);
    bool createTonemapProgram();
    void drawTonemapPass();
};

} // namespace engine
