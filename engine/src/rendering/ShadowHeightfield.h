#pragma once
//
// M4: heightfield shadow capture for overhead light rigs.
//
// TWO static ortho captures store, per footprint texel, the vertical INTERVAL
// of occluder geometry at that (x, z):
//
//   max capture (looking DOWN): highest surface Y  -> R channel source
//   min capture (looking UP):   lowest surface Y   -> the under-slab
//
// The PBR shader marches the receiver -> light segment in XZ and blocks the
// light where the ray height falls INSIDE [minY, maxY] of the local column.
//
// Exactness: the column interval [minY, maxY] IS the full vertical extent of
// any convex solid at that (x, z), so the march is EXACT for convex
// occluders -- the entire frozen Cornell set (blocks, baffle, spheres). For
// non-convex columns (e.g. a torus overhanging a gap) the interval is a
// conservative bound: shadows may be slightly too large, never too small.
//
// Why not classic shadow maps at M4: the Cornell emitter is a 1.3 x 1.05 m
// patch approximated by 16 point lights. One shadow map from the grid
// centroid misplaces each light's shadow by up to ~0.4 m, and 16 cube maps
// is not an incremental milestone. The heightfield is ONE capture pair,
// exact for every light in the rig, and the 16 superposed hard shadows form
// a natural 16-level penumbra. General omnidirectional shadow mapping
// (cube/point) lands with the scene-abstraction milestone when
// non-heightfield scenes need shadows.
//
// GL surface-area discipline: this component adds ZERO new loader entry
// points (TexImage2D covers R16F; the default depth func LESS is exactly the
// right keep rule for both cameras: nearest-from-above keeps the highest
// surface, nearest-from-below keeps the lowest).

#include "math/Mat4.h"
#include "rendering/GL.h"
#include "rendering/Shader.h"

#include <vector>

namespace engine {

class ShadowHeightfield {
public:
    // Runtime verification of the captured fields (readback + statistics).
    // Exists because the capture path is hardware GL: a silently empty field
    // (driver, state, draw-order issue) turns every shadow into light leak
    // while the telemetry still says "shadows: on". M4.0.1 on-hardware
    // evidence: the acceptance render's shadow probes read byte-identical to
    // the no-shadow model, so the field must prove itself from now on.
    struct FieldStats {
        float coveragePct;        // % of texels with occluder (max field > 0.25 m)
        float topM;               // highest captured surface (max field maximum)
        float minWhereOccludedM;  // lowest captured underside where occluded
        bool  intervalValid;      // min field <= max field + eps everywhere
    };
    // Reads back both R16F targets (GL_RED content read as RGBA/FLOAT -- no
    // new loader entry points; ReadPixels is already engine surface) and
    // checks against the FROZEN scene's expectations:
    //   expectedTopM            : tallest occluder (3.30 boxes / 5.40 baffle)
    //   coverageLoPct, ...HiPct : expected occluder-footprint band
    // Prints one loud line either way. Returns true iff top and coverage are
    // inside their bands and every texel's interval is valid (min <= max).
    // An invalid (un-created) instance returns false without printing.
    bool verifyField(float expectedTopM, float coverageLoPct, float coverageHiPct,
                     FieldStats* outStats = nullptr) const;

    // --- M4.0.4 registration instruments -----------------------------------
    // verifyField() proves the field's AGGREGATE content (coverage %, top,
    // interval validity) but not WHERE the content sits. A spatially
    // displaced, mirrored, or transposed field passes every one of those
    // statistics while making every shadow ray miss (M4.0.3 hardware
    // evidence: verify OK + march enabled + image byte-identical to
    // --no-shadows). These two surfaces let scene-side code check position,
    // not just quantity.

    // Raw readback of both captured fields, R channel as float, one value
    // per texel, ROW-MAJOR with row 0 at the footprint's minZ edge and
    // column 0 at minX -- the same layout the sampler sees (v = 0 at minZ).
    // Size res*res per field. One-time startup cost (~1 MB total at 256).
    // Returns false on an invalid (un-created) instance.
    bool readbackHeights(std::vector<float>& outMax,
                         std::vector<float>& outMin) const;

    // World (x, z) -> continuous texel coordinates (col, row), row 0 at
    // minZ: col = (x - minX) / spanX * res, row = (z - minZ) / spanZ * res.
    // THE shared mapping for every diagnostic that must agree with the
    // shadow march (the GLSL computes uv = (world.xz - footprintMin) / span
    // and samples with v = 0 at minZ -- this is that formula in CPU form).
    // Returns false outside the closed footprint. Pinned headlessly by
    // bench_tests (pure math, no GL context needed).
    static bool worldToTexel(float x, float z,
                             float minX, float maxX, float minZ, float maxZ,
                             unsigned resolution,
                             float* outCol, float* outRow);

public:
    // Allocates both capture targets (R16F) + FBOs + the capture program.
    //   resolution : texels per side (square targets)
    //   minY, maxY : captured height slab; also both cameras' near/far slab
    bool create(unsigned resolution, float minY, float maxY);
    void destroy();
    ~ShadowHeightfield();   // destroy() while the context is alive

    bool valid() const { return m_maxFbo != 0; }
    unsigned resolution() const { return m_resolution; }
    float minY() const { return m_minY; }
    float maxY() const { return m_maxY; }

    // World XZ rectangle mapped onto the textures. Everything the shading
    // side needs to reconstruct a texel's world position is derived from
    // these values -- pass the SAME numbers to the PBR shader uniforms.
    // maxPass: true = highest surface (camera above), false = lowest surface
    // (camera below). beginCapture may be called twice (once per pass) with
    // the same footprint; endCapture between passes.
    void beginCapture(bool maxPass, float minX, float maxX, float minZ, float maxZ);
    // Does NOT restore clear color / viewport: the capture is a one-time init
    // step; callers re-issue renderer.setClearColor() afterwards and the
    // frame loop sets the viewport every frame anyway.
    void endCapture();

    // Per-draw model transform, before renderer.drawIndexed(mesh). The
    // capture program stays bound between beginCapture/endCapture.
    void setModel(const Mat4& model);

    // Shading side: bind the two height textures (max, min) to sampler units.
    void bindTextures(unsigned maxUnit, unsigned minUnit) const;

    // World-space size of one texel (square footprint assumed). The shader's
    // march step is chosen relative to occluder thickness so a step can never
    // skip a thin occluder; bench_tests pins that invariant.
    float texelWorldSize() const;

    // Down- or up-looking ortho view-projection for the capture. World
    // (x, y, z) -> clip with x_ndc = -1 at minX, +1 at maxX; y_ndc = -1 at
    // minZ, +1 at maxZ. maxPass: depth INCREASES as world Y DECREASES (LESS
    // keeps the highest surface). minPass: depth INCREASES as world Y
    // INCREASES (LESS keeps the lowest surface). Exposed for verification
    // (bench_tests pins the corner mappings and the orderings).
    Mat4 captureViewProjection(bool maxPass) const;

private:
    gl::GLuint m_maxFbo     = 0;
    gl::GLuint m_maxTex     = 0;   // R16F: highest surface Y per texel
    gl::GLuint m_maxDepthRb = 0;
    gl::GLuint m_minFbo     = 0;
    gl::GLuint m_minTex     = 0;   // R16F: lowest surface Y per texel
    gl::GLuint m_minDepthRb = 0;
    Shader m_program;              // shared capture program (move-only)
    gl::GLint m_uViewProj = -1;
    gl::GLint m_uModel    = -1;

    unsigned m_resolution = 0;
    float m_minY = 0.0f;
    float m_maxY = 1.0f;
    float m_minX = 0.0f;
    float m_maxX = 1.0f;
    float m_minZ = 0.0f;
    float m_maxZ = 1.0f;
};

} // namespace engine
