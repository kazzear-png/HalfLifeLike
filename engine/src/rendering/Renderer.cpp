#include "rendering/Renderer.h"

#include "rendering/Mesh.h"
#include "rendering/Shader.h"

#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace engine {

namespace {

// Tonemap shaders live engine-side: the HDR -> exposure -> tone map -> sRGB
// chain is renderer infrastructure, not scene content. Gameplay shaders stay
// in the app (sandbox).
const char* kTonemapVS = R"GLSL(
#version 330 core

// Fullscreen triangle from gl_VertexID -- no vertex buffers, one empty VAO.
out vec2 vUV;

void main()
{
    vec2 pos = vec2(
        (gl_VertexID == 1) ? 3.0 : -1.0,
        (gl_VertexID == 2) ? 3.0 : -1.0);
    vUV = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)GLSL";

const char* kTonemapFS = R"GLSL(
#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uHDR;
uniform float uExposure;

// ACES filmic approximation (Narkowicz 2015) -- established tonemap curve.
vec3 acesFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Linear -> sRGB (exact IEC 61966-2-1 piecewise).
vec3 linearToSRGB(vec3 c)
{
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

// Stateless per-pixel hash (Dave Hoskins 2013). Feeds the dither below.
float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
    vec3 hdr = texture(uHDR, vUV).rgb * uExposure;
    vec3 srgb = linearToSRGB(acesFilm(hdr));

    // Triangular-PDF dither (3 LSB peak-to-peak, i.e. +-1.5 LSB): smooth HDR
    // gradients (flashlight pools, ambient falloffs) quantize into visible
    // concentric bands on an 8-bit backbuffer once sRGB stretches the darks
    // by 12.92x. Summing two uniform hashes centers the noise at zero with a
    // triangular distribution whose tails reach +-1.5 LSB -- slightly beyond
    // +-1 LSB on purpose, so even the heavier low-end quantization steps are
    // fully decorrelated. The classic perceptual shortcut: buy smoothness
    // with imperceptible noise, not bits.
    float n1 = hash12(gl_FragCoord.xy);
    float n2 = hash12(gl_FragCoord.xy + vec2(0.137, -7.913));
    srgb += (n1 + n2 - 1.0) * (1.5 / 255.0);

    FragColor = vec4(srgb, 1.0);
}
)GLSL";

} // namespace

void Renderer::init() {
    gl::Enable(gl::DepthTest);
    setClearColor(0.10f, 0.12f, 0.15f, 1.0f);
}

Renderer::~Renderer() {
    // Application member destruction order guarantees the GL context is still
    // alive here (m_renderer is destroyed before m_window).
    destroyHDRTargets();
    if (m_timerQueries[0] != 0) {
        gl::DeleteQueries(kTimerRingSize, m_timerQueries);
        for (int i = 0; i < kTimerRingSize; ++i) {
            m_timerQueries[i] = 0;
            m_timerSlotPending[i] = false;
        }
    }
    if (m_emptyVao != 0) {
        gl::DeleteVertexArrays(1, &m_emptyVao);
        m_emptyVao = 0;
    }
    // m_tonemapShader releases its program through its own destructor.
}

void Renderer::enableGpuTiming(bool enable) {
    if (enable == m_gpuTimingWanted) {
        return;
    }
    m_gpuTimingWanted = enable;
    m_gpuTimingOn = enable;
    m_gpuFrameMs = -1.0f;
    for (int i = 0; i < kTimerRingSize; ++i) {
        m_timerSlotPending[i] = false;
    }
    m_timerRingIndex = 0;
    if (enable && m_timerQueries[0] == 0) {
        gl::GenQueries(kTimerRingSize, m_timerQueries);
    }
    if (!enable && m_timerQueries[0] != 0) {
        gl::DeleteQueries(kTimerRingSize, m_timerQueries);
        for (int i = 0; i < kTimerRingSize; ++i) {
            m_timerQueries[i] = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// HDR target management
// ---------------------------------------------------------------------------

void Renderer::destroyHDRTargets() {
    if (m_msaaDepthRb != 0)     { gl::DeleteRenderbuffers(1, &m_msaaDepthRb);  m_msaaDepthRb = 0; }
    if (m_msaaColorRb != 0)     { gl::DeleteRenderbuffers(1, &m_msaaColorRb);  m_msaaColorRb = 0; }
    if (m_msaaFbo != 0)         { gl::DeleteFramebuffers(1, &m_msaaFbo);       m_msaaFbo = 0; }
    if (m_resolveColorTex != 0) { gl::DeleteTextures(1, &m_resolveColorTex);   m_resolveColorTex = 0; }
    if (m_resolveFbo != 0)      { gl::DeleteFramebuffers(1, &m_resolveFbo);    m_resolveFbo = 0; }
    m_samples = 0;
}

bool Renderer::createHDRTargets(int width, int height, int samples) {
    // --- multisample scene target (or plain FBO when samples == 1) ---
    gl::GenFramebuffers(1, &m_msaaFbo);
    gl::BindFramebuffer(gl::Framebuffer, m_msaaFbo);

    gl::GenRenderbuffers(1, &m_msaaColorRb);
    gl::BindRenderbuffer(gl::Renderbuffer, m_msaaColorRb);
    if (samples > 1) {
        gl::RenderbufferStorageMultisample(gl::Renderbuffer, samples, gl::RGBA16F, width, height);
    } else {
        gl::RenderbufferStorage(gl::Renderbuffer, gl::RGBA16F, width, height);
    }
    gl::FramebufferRenderbuffer(gl::Framebuffer, gl::ColorAttachment0, gl::Renderbuffer, m_msaaColorRb);

    gl::GenRenderbuffers(1, &m_msaaDepthRb);
    gl::BindRenderbuffer(gl::Renderbuffer, m_msaaDepthRb);
    if (samples > 1) {
        gl::RenderbufferStorageMultisample(gl::Renderbuffer, samples, gl::DepthComponent24, width, height);
    } else {
        gl::RenderbufferStorage(gl::Renderbuffer, gl::DepthComponent24, width, height);
    }
    gl::FramebufferRenderbuffer(gl::Framebuffer, gl::DepthAttachment, gl::Renderbuffer, m_msaaDepthRb);

    const gl::GLenum status = gl::CheckFramebufferStatus(gl::Framebuffer);
    gl::BindFramebuffer(gl::Framebuffer, 0);
    gl::BindRenderbuffer(gl::Renderbuffer, 0);
    if (status != gl::FramebufferComplete) {
        std::fprintf(stderr, "[Renderer] HDR target incomplete (0x%04x) at %dx%d, %dx MSAA\n",
                     status, width, height, samples);
        destroyHDRTargets();
        return false;
    }

    // --- resolve texture + FBO (sampled by the tonemap pass) ---
    gl::GenTextures(1, &m_resolveColorTex);
    gl::BindTexture(gl::Texture2D, m_resolveColorTex);
    gl::TexImage2D(gl::Texture2D, 0, gl::RGBA16F, width, height, 0, gl::RGBA, gl::HalfFloat, nullptr);
    gl::TexParameteri(gl::Texture2D, gl::TextureMinFilter, gl::Linear);
    gl::TexParameteri(gl::Texture2D, gl::TextureMagFilter, gl::Linear);
    gl::TexParameteri(gl::Texture2D, gl::TextureWrapS, gl::ClampToEdge);
    gl::TexParameteri(gl::Texture2D, gl::TextureWrapT, gl::ClampToEdge);
    gl::BindTexture(gl::Texture2D, 0);

    gl::GenFramebuffers(1, &m_resolveFbo);
    gl::BindFramebuffer(gl::Framebuffer, m_resolveFbo);
    gl::FramebufferTexture2D(gl::Framebuffer, gl::ColorAttachment0, gl::Texture2D, m_resolveColorTex, 0);

    const gl::GLenum resolveStatus = gl::CheckFramebufferStatus(gl::Framebuffer);
    gl::BindFramebuffer(gl::Framebuffer, 0);
    if (resolveStatus != gl::FramebufferComplete) {
        std::fprintf(stderr, "[Renderer] HDR resolve target incomplete (0x%04x)\n", resolveStatus);
        destroyHDRTargets();
        return false;
    }

    m_samples = samples;
    m_width = width;
    m_height = height;
    return true;
}

bool Renderer::createTonemapProgram() {
    Shader shader = Shader::fromSource(kTonemapVS, kTonemapFS);
    if (!shader.valid()) {
        return false;
    }
    // Move the compiled program into the Renderer-owned shader wrapper.
    m_tonemapShader = std::move(shader);
    const gl::GLuint program = m_tonemapShader.nativeHandle();

    m_uHDRLocation      = gl::GetUniformLocation(program, "uHDR");
    m_uExposureLocation = gl::GetUniformLocation(program, "uExposure");
    return true;
}

bool Renderer::initHDR(int width, int height, int msaaSamples) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    destroyHDRTargets();

    // Try MSAA first; on unsupported/incomplete configs fall back to 1x.
    if (msaaSamples > 1) {
        if (createHDRTargets(width, height, msaaSamples)) {
            std::printf("[Renderer] HDR active: RGBA16F, %dx MSAA, %dx%d\n",
                        msaaSamples, width, height);
        } else {
            std::printf("[Renderer] MSAA unavailable; retrying without it.\n");
        }
    }

    if (m_samples == 0 && !createHDRTargets(width, height, 1)) {
        std::fprintf(stderr, "[Renderer] HDR targets unavailable; continuing in direct mode.\n");
        m_hdrActive = false;
        return false;
    }

    // Empty VAO for the attribute-less tonemap triangle (core profile
    // requires *some* VAO to be bound).
    if (m_emptyVao == 0) {
        gl::GenVertexArrays(1, &m_emptyVao);
    }
    if (m_tonemapShader.nativeHandle() == 0 && !createTonemapProgram()) {
        std::fprintf(stderr, "[Renderer] Tonemap program failed; continuing in direct mode.\n");
        destroyHDRTargets();
        m_hdrActive = false;
        return false;
    }

    m_hdrActive = true;
    return true;
}

void Renderer::resizeHDR(int width, int height) {
    if (!m_hdrActive || (width == m_width && height == m_height) || width <= 0 || height <= 0) {
        return;
    }
    const int samples = (m_samples > 1) ? m_samples : 1;
    destroyHDRTargets();
    if (!createHDRTargets(width, height, samples)) {
        // Driver refused recreation; retry without MSAA before giving up.
        destroyHDRTargets();
        if (!createHDRTargets(width, height, 1)) {
            std::fprintf(stderr, "[Renderer] HDR resize failed; continuing in direct mode.\n");
            m_hdrActive = false;
        }
    }
}

void Renderer::setExposure(float linearExposure) {
    m_exposure = (linearExposure > 0.0f) ? linearExposure : 0.0001f;
}

// ---------------------------------------------------------------------------
// Frame flow
// ---------------------------------------------------------------------------

void Renderer::setViewport(int x, int y, int width, int height) {
    gl::Viewport(x, y, width, height);
}

void Renderer::setClearColor(float r, float g, float b, float a) {
    m_clearColor[0] = r; m_clearColor[1] = g; m_clearColor[2] = b; m_clearColor[3] = a;
    gl::ClearColor(r, g, b, a);
}

void Renderer::beginFrame() {
    m_stats.reset();  // frame starts here

    if (m_gpuTimingOn) {
        // M5.2: drain completed query results NON-BLOCKINGLY before re-arming.
        // Slots are visited oldest -> newest (queries on one target retire in
        // issue order, so the first not-ready slot means nothing newer is done
        // either). Every drained value refreshes m_gpuFrameMs; a frame with no
        // completed sample reports n/a (-1) and the benchmark skips it.
        m_gpuFrameMs = -1.0f;
        for (int i = 0; i < kTimerRingSize; ++i) {
            const int slot = (m_timerRingIndex + i) % kTimerRingSize;
            if (!m_timerSlotPending[slot]) {
                continue;
            }
            gl::GLint available = 0;
            gl::GetQueryObjectiv(m_timerQueries[slot], gl::QueryResultAvailable, &available);
            if (available == 0) {
                if (i != 0) {
                    break;  // in-order retirement: stop at the first gap
                }
                // The slot we must re-arm THIS frame is a full ring old and
                // still not retired (GPU 4+ frames behind -- pathological).
                // One blocking read so its sample is not lost; this is the
                // rare fallback, not the steady-state path.
            }
            gl::GLuint64 ns = 0;
            gl::GetQueryObjectui64v(m_timerQueries[slot], gl::QueryResult, &ns);
            m_timerSlotPending[slot] = false;
            if (gl::GetError() != gl::NoError || ns == 0) {
                m_gpuTimingOn = false;   // driver misbehaved; report n/a
                std::fprintf(stderr, "[Renderer] GPU timer query failed; timing disabled.\n");
                break;
            }
            m_gpuFrameMs = static_cast<float>(static_cast<double>(ns) / 1e6);
        }
        // (a driver failure above clears m_gpuTimingOn; do not re-arm then)
        if (m_gpuTimingOn) {
            gl::BeginQuery(gl::TimeElapsed, m_timerQueries[m_timerRingIndex]);
        }
    }

    if (m_hdrActive) {
        gl::BindFramebuffer(gl::Framebuffer, m_msaaFbo);
    }
    gl::Clear(gl::ColorBufferBit | gl::DepthBufferBit);
}

void Renderer::drawTonemapPass() {
    // 1) Resolve multisample scene into the resolve texture.
    gl::BindFramebuffer(gl::ReadFramebuffer, m_msaaFbo);
    gl::BindFramebuffer(gl::DrawFramebuffer, m_resolveFbo);
    gl::BlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height,
                        gl::ColorBufferBit, gl::Nearest);

    // 2) Tonemap resolve texture to the default (backbuffer) framebuffer.
    gl::BindFramebuffer(gl::Framebuffer, 0);
    gl::Disable(gl::DepthTest);
    gl::BindVertexArray(m_emptyVao);
    gl::ActiveTexture(gl::Texture0);
    gl::BindTexture(gl::Texture2D, m_resolveColorTex);
    m_tonemapShader.bind();
    if (m_uHDRLocation >= 0)      gl::Uniform1i(m_uHDRLocation, 0);
    if (m_uExposureLocation >= 0) gl::Uniform1f(m_uExposureLocation, m_exposure);
    gl::DrawArrays(gl::Triangles, 0, 3);

    // 3) Restore state for the next frame's scene pass.
    gl::Enable(gl::DepthTest);
    gl::BindTexture(gl::Texture2D, 0);
    gl::BindVertexArray(0);
    gl::BindFramebuffer(gl::ReadFramebuffer, 0);
    gl::BindFramebuffer(gl::DrawFramebuffer, 0);
}

void Renderer::drawIndexed(const Mesh& mesh) {
    if (!mesh.valid()) {
        return;
    }

    mesh.bind();
    gl::DrawElements(gl::Triangles, gl::GLsizei(mesh.indexCount()), gl::UnsignedInt, nullptr);
    m_stats.drawCalls += 1;
    m_stats.triangles += mesh.indexCount() / 3;

    // M5.1 PERF: the per-draw glGetError() tripwire that used to live here
    // forced a driver-level command-buffer flush after EVERY draw call,
    // fully serializing CPU and GPU (measured ~6.4 ms/frame on a 36-triangle
    // scene: CPU 6.2 ms == GPU 6.1 ms -- both sides idle-waiting on the
    // other). Error state is now sampled once per frame in endFrame(),
    // after the timer query is closed, so the pipeline stays asynchronous.
    // For call-site attribution use GL_ARB_debug_output / GL_KHR_debug
    // instead of polling.
}

void Renderer::endFrame() {
    if (m_hdrActive) {
        drawTonemapPass();
    }
    if (m_gpuTimingOn) {
        gl::EndQuery(gl::TimeElapsed);
        m_timerSlotPending[m_timerRingIndex] = true;
        m_timerRingIndex = (m_timerRingIndex + 1) % kTimerRingSize;
    }

    // M5.1 PERF: once-per-frame error tripwire (replaces the per-draw check
    // removed from drawIndexed). glGetError() clears the pending error flag,
    // so this still surfaces any GL error raised anywhere during the frame;
    // only the call-site attribution is lost. Placed after EndQuery so the
    // GPU timer region is unaffected by the (rare) error path.
    const gl::GLenum error = gl::GetError();
    if (error != gl::NoError) {
        std::fprintf(stderr, "[Renderer] GL error 0x%04x raised during frame\n", error);
    }
}

bool Renderer::readBackbufferPixels(int width, int height, unsigned char* outRgba) {
    if (outRgba == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    gl::BindFramebuffer(gl::ReadFramebuffer, 0);
    gl::ReadPixels(0, 0, width, height, gl::RGBA, gl::UnsignedByte, outRgba);
    return gl::GetError() == gl::NoError;
}

} // namespace engine
