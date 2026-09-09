#pragma once
#include "math/Frustum.h"
#include "rendering/Shader.h"
#include <array>
#include <vector>

namespace engine {
// Exact AABBs/planes and analytic spheres for the Cornell laboratory. Not a
// replacement for triangle/BVH traversal in arbitrary imported scenes.
struct GiSurface {
    std::array<float, 4> lo{};       // xyz bounds, w: 0 box/plane, 1 sphere
    std::array<float, 4> hi{};       // xyz bounds, w: six-bit active-face mask
    std::array<float, 4> material{}; // linear albedo rgb, metalness
};

// GPU-updated, world-surface diffuse transport cache. Stores incoming
// irradiance / pi from ONE indirect diffuse bounce; no direct emitter hits,
// no baked data, no screen-space history, no recursive cache feedback.
class DiffuseGI {
public:
    static constexpr int kMaxSurfaces = 16;
    DiffuseGI() = default;
    ~DiffuseGI();
    DiffuseGI(const DiffuseGI&) = delete;
    DiffuseGI& operator=(const DiffuseGI&) = delete;
    bool init(int resolution = 16, int rays = 8, int history = 32);
    // Scene/light changes invalidate history. Call these when editing either.
    bool setScene(const std::vector<GiSurface>& surfaces);
    void setLight(const Vec3& center, float halfX, float halfZ, float radiance);
    void resetHistory();
    void update(); // changes FBO/viewport; caller restores its scene target
    void bind(Shader& target, int surfaceIndex, unsigned textureUnit = 2) const;
    bool valid() const { return m_fbo[0] != 0 && m_shader.valid(); }
    int resolution() const { return m_resolution; }
    int rays() const { return m_rays; }
    int history() const { return m_history; }
    int surfaceCount() const { return static_cast<int>(m_surfaces.size()); }
    // Inject after #version in the scene shader. Also shared by update shader.
    static const char* samplingSource();
private:
    void release();
    Shader m_shader;
    gl::GLuint m_fbo[2]{}, m_texture[2]{}, m_vao = 0;
    int m_resolution = 16, m_rays = 8, m_history = 32, m_front = 0;
    unsigned m_frame = 0, m_historyFrames = 0;
    bool m_uniformsDirty = true;
    std::vector<GiSurface> m_surfaces;
    std::array<float, 4> m_light{0, 5.49f, 0, 12};
    std::array<float, 4> m_patch{0.65f, 0.525f, 0, 0};
};
}
