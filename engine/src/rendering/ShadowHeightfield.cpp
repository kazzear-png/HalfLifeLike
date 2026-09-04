#include "rendering/ShadowHeightfield.h"

#include <cstdio>
#include <cmath>
#include <utility>
#include <vector>

namespace engine {

namespace {

// Capture program: world-space height into the R16F color channel. Deliberately
// engine-side (next to the tonemap shaders): the height field is renderer
// infrastructure -- scene code only supplies occluder meshes.
const char* kHeightVS = R"GLSL(
#version 330 core

layout (location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uViewProj;

out float vWorldY;

void main()
{
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldY = world.y;
    gl_Position = uViewProj * world;
}
)GLSL";

const char* kHeightFS = R"GLSL(
#version 330 core

in float vWorldY;
out vec4 FragColor;

void main()
{
    // Only R is stored (R16F target). Linear filtering of these textures is
    // the softening of the shadow test: bilinearly interpolated heights turn
    // the hard footprint edge into a height ramp the march resolves smoothly.
    FragColor = vec4(vWorldY, 0.0, 0.0, 1.0);
}
)GLSL";

} // namespace

ShadowHeightfield::~ShadowHeightfield() {
    destroy();
}

bool ShadowHeightfield::create(unsigned resolution, float minY, float maxY) {
    destroy();

    if (resolution == 0 || minY >= maxY) {
        std::fprintf(stderr, "[ShadowHeightfield] invalid create parameters.\n");
        return false;
    }

    Shader program = Shader::fromSource(kHeightVS, kHeightFS);
    if (!program.valid()) {
        std::fprintf(stderr, "[ShadowHeightfield] capture program failed.\n");
        return false;
    }
    m_program = std::move(program);
    m_uViewProj = gl::GetUniformLocation(m_program.nativeHandle(), "uViewProj");
    m_uModel    = gl::GetUniformLocation(m_program.nativeHandle(), "uModel");

    // --- two R16F height textures + FBOs (filterable in core 3.3) ----------
    struct Target { gl::GLuint fbo; gl::GLuint tex; gl::GLuint depth; };
    Target targets[2] = { {0, 0, 0}, {0, 0, 0} };

    for (int i = 0; i < 2; ++i) {
        Target& t = targets[i];
        gl::GenTextures(1, &t.tex);
        gl::BindTexture(gl::Texture2D, t.tex);
        gl::TexImage2D(gl::Texture2D, 0, gl::R16F,
                       static_cast<gl::GLsizei>(resolution), static_cast<gl::GLsizei>(resolution),
                       0, gl::Red, gl::HalfFloat, nullptr);
        gl::TexParameteri(gl::Texture2D, gl::TextureMinFilter, gl::Linear);
        gl::TexParameteri(gl::Texture2D, gl::TextureMagFilter, gl::Linear);
        gl::TexParameteri(gl::Texture2D, gl::TextureWrapS, gl::ClampToEdge);
        gl::TexParameteri(gl::Texture2D, gl::TextureWrapT, gl::ClampToEdge);
        gl::BindTexture(gl::Texture2D, 0);

        gl::GenFramebuffers(1, &t.fbo);
        gl::BindFramebuffer(gl::Framebuffer, t.fbo);
        gl::FramebufferTexture2D(gl::Framebuffer, gl::ColorAttachment0, gl::Texture2D, t.tex, 0);

        gl::GenRenderbuffers(1, &t.depth);
        gl::BindRenderbuffer(gl::Renderbuffer, t.depth);
        gl::RenderbufferStorage(gl::Renderbuffer, gl::DepthComponent24,
                                static_cast<gl::GLsizei>(resolution), static_cast<gl::GLsizei>(resolution));
        gl::FramebufferRenderbuffer(gl::Framebuffer, gl::DepthAttachment, gl::Renderbuffer, t.depth);

        const gl::GLenum status = gl::CheckFramebufferStatus(gl::Framebuffer);
        gl::BindFramebuffer(gl::Framebuffer, 0);
        gl::BindRenderbuffer(gl::Renderbuffer, 0);
        if (status != gl::FramebufferComplete) {
            std::fprintf(stderr, "[ShadowHeightfield] target %d incomplete (0x%04x) at %ux%u\n",
                         i, status, resolution, resolution);
            destroy();
            return false;
        }
    }

    m_maxFbo = targets[0].fbo; m_maxTex = targets[0].tex; m_maxDepthRb = targets[0].depth;
    m_minFbo = targets[1].fbo; m_minTex = targets[1].tex; m_minDepthRb = targets[1].depth;
    m_resolution = resolution;
    m_minY = minY;
    m_maxY = maxY;
    std::printf("[ShadowHeightfield] capture targets ready: %ux%u R16F x2, slab y %.2f..%.2f\n",
                resolution, resolution, static_cast<double>(minY), static_cast<double>(maxY));
    return true;
}

void ShadowHeightfield::destroy() {
    if (m_maxDepthRb != 0) { gl::DeleteRenderbuffers(1, &m_maxDepthRb); m_maxDepthRb = 0; }
    if (m_maxTex != 0)     { gl::DeleteTextures(1, &m_maxTex);          m_maxTex = 0; }
    if (m_maxFbo != 0)     { gl::DeleteFramebuffers(1, &m_maxFbo);      m_maxFbo = 0; }
    if (m_minDepthRb != 0) { gl::DeleteRenderbuffers(1, &m_minDepthRb); m_minDepthRb = 0; }
    if (m_minTex != 0)     { gl::DeleteTextures(1, &m_minTex);          m_minTex = 0; }
    if (m_minFbo != 0)     { gl::DeleteFramebuffers(1, &m_minFbo);      m_minFbo = 0; }
    m_resolution = 0;
    // m_program releases its own GL program through the Shader destructor.
}

bool ShadowHeightfield::verifyField(float expectedTopM, float coverageLoPct,
                                    float coverageHiPct,
                                    FieldStats* outStats) const {
    if (!valid()) {
        return false;
    }

    // Read both R16F fields back as RGBA/FLOAT (the universally accepted
    // readback combination; GL converts). 256x256x4 floats = 1 MB per field.
    const std::size_t n = std::size_t(m_resolution) * std::size_t(m_resolution);
    std::vector<float> maxPix(n * 4u, 0.0f);
    std::vector<float> minPix(n * 4u, 0.0f);
    gl::BindFramebuffer(gl::Framebuffer, m_maxFbo);
    gl::ReadPixels(0, 0, static_cast<gl::GLsizei>(m_resolution),
                   static_cast<gl::GLsizei>(m_resolution),
                   gl::RGBA, gl::Float, maxPix.data());
    gl::BindFramebuffer(gl::Framebuffer, m_minFbo);
    gl::ReadPixels(0, 0, static_cast<gl::GLsizei>(m_resolution),
                   static_cast<gl::GLsizei>(m_resolution),
                   gl::RGBA, gl::Float, minPix.data());
    gl::BindFramebuffer(gl::Framebuffer, 0);

    FieldStats s;
    s.coveragePct = 0.0f;
    s.topM = 0.0f;
    s.minWhereOccludedM = 0.0f;
    s.intervalValid = true;
    std::size_t occupied = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const float hMax = maxPix[i * 4u + 0u];
        const float hMin = minPix[i * 4u + 0u];
        if (hMin > hMax + 1e-3f) {
            s.intervalValid = false;   // inverted interval: broken capture
        }
        if (hMax > 0.25f) {
            ++occupied;
            if (hMax > s.topM) s.topM = hMax;
            if (occupied == 1 || hMin < s.minWhereOccludedM) {
                s.minWhereOccludedM = hMin;
            }
        }
    }
    s.coveragePct = 100.0f * static_cast<float>(occupied) / static_cast<float>(n);

    const bool topOk = std::fabs(s.topM - expectedTopM) <= 0.15f;
    const bool coverageOk = (s.coveragePct >= coverageLoPct) &&
                            (s.coveragePct <= coverageHiPct);
    const bool ok = topOk && coverageOk && s.intervalValid;

    // Loud either way: this line is the hardware-side proof that the shadow
    // march has a real field to march (M4.0.1 lesson: an inert field is
    // indistinguishable from --no-shadows in the image).
    if (ok) {
        std::printf("[ShadowHeightfield] field verify OK: coverage %.2f%% "
                    "(expected %.2f..%.2f%%), top %.3f m (expected %.2f), "
                    "min-where-occluded %.3f m, intervals %s\n",
                    static_cast<double>(s.coveragePct),
                    static_cast<double>(coverageLoPct),
                    static_cast<double>(coverageHiPct),
                    static_cast<double>(s.topM),
                    static_cast<double>(expectedTopM),
                    static_cast<double>(s.minWhereOccludedM),
                    s.intervalValid ? "valid" : "INVALID");
    } else {
        std::fprintf(stderr,
                    "[ShadowHeightfield] FIELD VERIFY FAILED: coverage %.2f%% "
                    "(expected %.2f..%.2f%%), top %.3f m (expected %.2f), "
                    "intervals %s -- shadows disabled, telemetry will say so\n",
                    static_cast<double>(s.coveragePct),
                    static_cast<double>(coverageLoPct),
                    static_cast<double>(coverageHiPct),
                    static_cast<double>(s.topM),
                    static_cast<double>(expectedTopM),
                    s.intervalValid ? "valid" : "INVALID");
    }
    if (outStats != nullptr) {
        *outStats = s;
    }
    return ok;
}

bool ShadowHeightfield::readbackHeights(std::vector<float>& outMax,
                                        std::vector<float>& outMin) const {
    outMax.clear();
    outMin.clear();
    if (!valid()) {
        return false;
    }
    const std::size_t n = std::size_t(m_resolution) * std::size_t(m_resolution);
    // Read as RGBA/FLOAT (the universally accepted readback combination for
    // an R16F attachment -- same trick verifyField uses), keep only R.
    std::vector<float> pix(n * 4u, 0.0f);
    outMax.resize(n);
    outMin.resize(n);
    gl::BindFramebuffer(gl::Framebuffer, m_maxFbo);
    gl::ReadPixels(0, 0, static_cast<gl::GLsizei>(m_resolution),
                   static_cast<gl::GLsizei>(m_resolution),
                   gl::RGBA, gl::Float, pix.data());
    for (std::size_t i = 0; i < n; ++i) outMax[i] = pix[i * 4u + 0u];
    gl::BindFramebuffer(gl::Framebuffer, m_minFbo);
    gl::ReadPixels(0, 0, static_cast<gl::GLsizei>(m_resolution),
                   static_cast<gl::GLsizei>(m_resolution),
                   gl::RGBA, gl::Float, pix.data());
    gl::BindFramebuffer(gl::Framebuffer, 0);
    for (std::size_t i = 0; i < n; ++i) outMin[i] = pix[i * 4u + 0u];
    return true;
}

bool ShadowHeightfield::worldToTexel(float x, float z,
                                     float minX, float maxX,
                                     float minZ, float maxZ,
                                     unsigned resolution,
                                     float* outCol, float* outRow) {
    if (resolution == 0 || maxX <= minX || maxZ <= minZ) {
        return false;
    }
    // Same formula as the GLSL march: uv = (world.xz - footprintMin) / span,
    // texture v = 0 at minZ. ReadPixels row 0 is the framebuffer bottom,
    // which the capture projection puts at world minZ (y_ndc = -1), so the
    // row index and the texture v agree -- no flip anywhere in the chain.
    const float u = (x - minX) / (maxX - minX);
    const float v = (z - minZ) / (maxZ - minZ);
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return false;
    }
    *outCol = u * static_cast<float>(resolution);
    *outRow = v * static_cast<float>(resolution);
    return true;
}

Mat4 ShadowHeightfield::captureViewProjection(bool maxPass) const {
    // Shared mapping (both passes): x_view = x_world, y_view = z_world.
    // maxPass: z_view = +y_world  (camera looks DOWN:  forward -z_v = -y_w)
    // minPass: z_view = -y_world  (camera looks UP:    forward -z_v = +y_w)
    //
    // Projection: standard GL ortho over
    //   x_view in [minX, maxX], y_view in [minZ, maxZ]
    // with the z slab chosen so exactly the captured height range is visible:
    //   maxPass: z_view = y_w in [minY, maxY]  ->  near = -maxY, far = -minY
    //   minPass: z_view = -y_w in [-maxY, -minY] -> near = minY, far = maxY
    // (GL sees z_view in [-far, -near].)
    //
    // Depth ordering (default LESS keeps the smaller depth):
    //   maxPass: z_ndc decreasing in y_w  -> higher surface wins  (max field)
    //   minPass: z_ndc decreasing in -y_w -> lower surface wins   (min field)
    const float L = m_minX, R = m_maxX;
    const float B = m_minZ, T = m_maxZ;
    const float N = maxPass ? -m_maxY : m_minY;    // near distance along -z_view
    const float F = maxPass ? -m_minY : m_maxY;    // far distance along -z_view

    Mat4 v;
    // Column-major: m[col*4 + row]. Columns are the world basis vectors
    // expressed in view space (the transpose of the row-form derivation).
    v.m[0]  = 1.0f;  v.m[1]  = 0.0f;  v.m[2]  = 0.0f;  v.m[3]  = 0.0f;  // col 0: x_w axis
    v.m[4]  = 0.0f;  v.m[5]  = 0.0f;  v.m[6]  = maxPass ? 1.0f : -1.0f; // col 1: y_w -> z_v
    v.m[8]  = 0.0f;  v.m[9]  = 1.0f;  v.m[10] = 0.0f;  v.m[11] = 0.0f;  // col 2: z_w -> y_v
    v.m[12] = 0.0f;  v.m[13] = 0.0f;  v.m[14] = 0.0f;  v.m[15] = 1.0f;

    Mat4 p;
    p.m[0]  = 2.0f / (R - L);  p.m[12] = -(R + L) / (R - L);
    p.m[5]  = 2.0f / (T - B);  p.m[13] = -(T + B) / (T - B);
    p.m[10] = -2.0f / (F - N); p.m[14] = -(F + N) / (F - N);
    p.m[15] = 1.0f;

    return p * v;
}

void ShadowHeightfield::beginCapture(bool maxPass, float minX, float maxX,
                                     float minZ, float maxZ) {
    m_minX = minX; m_maxX = maxX;
    m_minZ = minZ; m_maxZ = maxZ;

    gl::BindFramebuffer(gl::Framebuffer, maxPass ? m_maxFbo : m_minFbo);
    gl::Viewport(0, 0, static_cast<gl::GLsizei>(m_resolution), static_cast<gl::GLsizei>(m_resolution));
    // Heights default to 0 ("no occluder") -- exactly what the shading-side
    // march must see outside every solid (max 0 = nothing to hit from above,
    // min 0 = nothing to pass under).
    gl::ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::Clear(gl::ColorBufferBit | gl::DepthBufferBit);

    m_program.bind();
    const Mat4 vp = captureViewProjection(maxPass);
    if (m_uViewProj >= 0) gl::UniformMatrix4fv(m_uViewProj, 1, 0, vp.data());
}

void ShadowHeightfield::endCapture() {
    gl::BindFramebuffer(gl::Framebuffer, 0);
    gl::BindTexture(gl::Texture2D, 0);
    // Program/viewport/clear-color intentionally left; see header note.
}

void ShadowHeightfield::setModel(const Mat4& model) {
    if (m_uModel >= 0) gl::UniformMatrix4fv(m_uModel, 1, 0, model.data());
}

void ShadowHeightfield::bindTextures(unsigned maxUnit, unsigned minUnit) const {
    gl::ActiveTexture(gl::Texture0 + maxUnit);
    gl::BindTexture(gl::Texture2D, m_maxTex);
    gl::ActiveTexture(gl::Texture0 + minUnit);
    gl::BindTexture(gl::Texture2D, m_minTex);
}

float ShadowHeightfield::texelWorldSize() const {
    if (m_resolution == 0) return 0.0f;
    const float extentX = m_maxX - m_minX;
    const float extentZ = m_maxZ - m_minZ;
    // Square target, square footprint assumed; report the larger for safety.
    const float sx = extentX / static_cast<float>(m_resolution);
    const float sz = extentZ / static_cast<float>(m_resolution);
    return (sx > sz) ? sx : sz;
}

} // namespace engine
