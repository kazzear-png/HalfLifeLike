#include "rendering/DiffuseGI.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace engine {
namespace {
const char* kGiCommon = R"GLSL(
uniform sampler2D uGiAtlas;
uniform int uGiResolution;
uniform int uGiSurface;
uniform vec4 uGiSurfaceLo;
uniform vec4 uGiSurfaceHi;

// Face order -X,+X,-Y,+Y,-Z,+Z. Sphere tiles use normalized cube directions.
vec3 giFaceNormal(int face) {
    vec3 n = vec3(0.0);
    n[face / 2] = (face % 2 == 0) ? -1.0 : 1.0;
    return n;
}
vec3 giFaceVector(int face, vec2 uv) {
    vec3 p = giFaceNormal(face);
    int axis = face / 2;
    p[(axis + 1) % 3] = uv.x * 2.0 - 1.0;
    p[(axis + 2) % 3] = uv.y * 2.0 - 1.0;
    return p;
}
vec3 giSurfacePoint(vec4 lo, vec4 hi, int face, vec2 uv, out vec3 n) {
    vec3 q = giFaceVector(face, uv);
    if (lo.w > 0.5) {
        n = normalize(q);
        return (lo.xyz + hi.xyz) * 0.5 + n * ((hi.x - lo.x) * 0.5);
    }
    n = giFaceNormal(face);
    return mix(lo.xyz, hi.xyz, q * 0.5 + 0.5);
}
vec3 giLookup(vec3 p, vec3 normal) {
    if (uGiSurface < 0) return vec3(0.0);
    vec3 n = normal;
    if (uGiSurfaceLo.w > 0.5)
        n = normalize(p - (uGiSurfaceLo.xyz + uGiSurfaceHi.xyz) * 0.5);
    vec3 an = abs(n);
    int axis = (an.x >= an.y && an.x >= an.z) ? 0 : ((an.y >= an.z) ? 1 : 2);
    int face = axis * 2 + ((n[axis] >= 0.0) ? 1 : 0);
    vec2 uv;
    int a = (axis + 1) % 3, b = (axis + 2) % 3;
    if (uGiSurfaceLo.w > 0.5) {
        vec3 q = n / max(abs(n[axis]), 1e-8);
        uv = vec2(q[a], q[b]) * 0.5 + 0.5;
    } else {
        vec3 q = (p - uGiSurfaceLo.xyz) / max(uGiSurfaceHi.xyz - uGiSurfaceLo.xyz, vec3(1e-8));
        uv = vec2(q[a], q[b]);
    }
    float r = float(uGiResolution);
    float tile = r + 2.0;
    vec2 pixel = vec2(float(face), float(uGiSurface)) * tile + 1.0 + clamp(uv, 0.0, 1.0) * r;
    return texture(uGiAtlas, pixel / vec2(textureSize(uGiAtlas, 0))).rgb;
}
)GLSL";

const char* kGiVertex = R"GLSL(
#version 330 core
void main() {
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";

const char* kGiUpdateFragment = R"GLSL(
#version 330 core
out vec4 FragColor;
uniform int uGiCount;
uniform vec4 uGiLo[16];
uniform vec4 uGiHi[16];
uniform vec4 uGiMaterial[16];
uniform vec4 uGiLight; // center xyz, emitted radiance
uniform vec4 uGiPatch; // half size x,z
uniform int uGiRays;
uniform int uGiFrame;
uniform float uGiBlend;
const float giPi = 3.14159265358979323846;
const float giEpsilon = 0.001;

uint giHash(uint x) {
    x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u;
    x *= 0x846ca68bu; x ^= x >> 16u; return x;
}
float giRandom(inout uint state) {
    state = giHash(state + 0x9e3779b9u);
    return float(state >> 8u) * (1.0 / 16777216.0);
}
// One intersection implementation for bounce rays and light visibility rays.
// Handles parallel rays and zero-thickness room planes without inverse infinities.
bool giIntersect(int id, vec3 o, vec3 d, out float t, out vec3 n) {
    vec4 lo = uGiLo[id], hi = uGiHi[id];
    if (lo.w > 0.5) {
        vec3 c = (lo.xyz + hi.xyz) * 0.5;
        float radius = (hi.x - lo.x) * 0.5;
        vec3 oc = o - c;
        float b = dot(oc, d), v = dot(oc, oc) - radius * radius;
        float disc = b * b - v;
        if (disc < 0.0) return false;
        float root = sqrt(disc);
        t = -b - root;
        if (t <= giEpsilon) t = -b + root;
        if (t <= giEpsilon) return false;
        n = normalize(o + t * d - c);
    } else {
        float nearT = -1e30, farT = 1e30;
        vec3 nearN = vec3(0.0), farN = vec3(0.0);
        for (int a = 0; a < 3; ++a) {
            if (abs(d[a]) < 1e-8) {
                if (o[a] < lo[a] || o[a] > hi[a]) return false;
            } else {
                float ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
                vec3 entry = vec3(0.0); entry[a] = d[a] > 0.0 ? -1.0 : 1.0;
                float nt = min(ta, tb), ft = max(ta, tb);
                if (nt > nearT) { nearT = nt; nearN = entry; }
                if (ft < farT) { farT = ft; farN = -entry; }
                if (nearT > farT) return false;
            }
        }
        t = nearT; n = nearN;
        if (t <= giEpsilon) { t = farT; n = farN; }
        if (t <= giEpsilon || t >= 1e29) return false;
    }
    if (dot(n, d) > 0.0) n = -n;
    return true;
}
int giTrace(vec3 o, vec3 d, float limit, out vec3 point, out vec3 normal) {
    int found = -1;
    float best = limit;
    for (int i = 0; i < uGiCount; ++i) {
        float t; vec3 n;
        if (giIntersect(i, o, d, t, n) && t < best) {
            best = t; normal = n; found = i;
        }
    }
    point = o + best * d;
    return found;
}
bool giOccluded(vec3 o, vec3 d, float limit) {
    for (int i = 0; i < uGiCount; ++i) {
        float t; vec3 n;
        if (giIntersect(i, o, d, t, n) && t < limit) return true;
    }
    return false;
}
// Direct diffuse radiance leaving a secondary surface. One sample of the
// rectangular emitter using a complete mixture PDF and exact segment visibility.
vec3 giDirectDiffuse(int id, vec3 p, vec3 n, vec3 v, inout uint rng) {
    if (p.y >= uGiLight.y - giEpsilon) return vec3(0.0);
    float area = 4.0 * uGiPatch.x * uGiPatch.y;
    vec3 centerRay = uGiLight.xyz - p;
    float centerD2 = dot(centerRay, centerRay);
    // Mix area sampling with cosine-hemisphere sampling. Near a large emitter,
    // pure area sampling has a near-singular 1/r^2 weight and bright outliers.
    // This adaptive mixture stays unbiased and needs no radiance clamp/blur.
    float areaProbability = clamp(centerD2 / (centerD2 + area), 0.05, 0.95);
    float choose = giRandom(rng), u = giRandom(rng), vSample = giRandom(rng);
    vec3 q, l;
    float d2;
    if (choose < areaProbability) {
        q = uGiLight.xyz + vec3((2.0*u-1.0)*uGiPatch.x,0.0,(2.0*vSample-1.0)*uGiPatch.y);
        vec3 delta = q - p;
        d2 = max(dot(delta, delta), 1e-8);
        l = delta / sqrt(d2);
    } else {
        vec3 tangent = normalize(cross(abs(n.y)<0.9 ? vec3(0,1,0) : vec3(1,0,0), n));
        float phi = 2.0*giPi*vSample, radius = sqrt(u);
        l = tangent*(radius*cos(phi)) + cross(n,tangent)*(radius*sin(phi)) + n*sqrt(1.0-u);
        if (l.y <= 1e-8) return vec3(0.0);
        float t = (uGiLight.y-p.y)/l.y;
        q = p+t*l;
        if (abs(q.x-uGiLight.x)>uGiPatch.x || abs(q.z-uGiLight.z)>uGiPatch.y) return vec3(0.0);
        d2 = t*t;
    }
    float cosN = max(dot(n, l), 0.0), cosE = max(l.y, 0.0);
    if (cosN <= 0.0 || cosE <= 0.0) return vec3(0.0);
    float pdf = areaProbability * d2 / (area * cosE) + (1.0-areaProbability)*cosN/giPi;
    // Visibility uses the offset origin's actual segment to the same light point.
    vec3 origin = p + n * (2.0 * giEpsilon);
    vec3 shadow = q - origin;
    float shadowLength = length(shadow);
    if (giOccluded(origin, shadow / shadowLength, shadowLength - giEpsilon)) return vec3(0.0);
    vec4 mat = uGiMaterial[id];
    vec3 f0 = mix(vec3(0.04), mat.rgb, mat.a);
    vec3 h = l + v;
    h = dot(h, h) > 1e-8 ? normalize(h) : n;
    float f = pow(1.0 - clamp(dot(v, h), 0.0, 1.0), 5.0);
    vec3 kd = (1.0 - (f0 + (1.0 - f0) * f)) * (1.0 - mat.a);
    return kd * mat.rgb / giPi * (uGiLight.w * cosN / max(pdf, 1e-12));
}
void main() {
    int tile = uGiResolution + 2;
    ivec2 cell = ivec2(gl_FragCoord.xy);
    int face = cell.x / tile, id = cell.y / tile;
    if (id >= uGiCount || uGiLight.w <= 0.0 || (int(uGiHi[id].w) & (1 << face)) == 0) {
        FragColor = vec4(0.0); return;
    }
    vec2 uv = clamp((vec2(cell % tile) - 0.5) / float(uGiResolution), 0.001, 0.999);
    vec3 n;
    vec3 p = giSurfacePoint(uGiLo[id], uGiHi[id], face, uv, n);
    vec3 origin = p + n * (2.0 * giEpsilon);
    vec3 tangent = normalize(cross(abs(n.y) < 0.9 ? vec3(0,1,0) : vec3(1,0,0), n));
    vec3 bitangent = cross(n, tangent);
    uint rng = giHash(uint(cell.x) + uint(cell.y) * 4099u + uint(uGiFrame) * 104729u);
    vec3 sum = vec3(0.0);
    for (int s = 0; s < uGiRays; ++s) {
        float u = giRandom(rng), phi = 2.0 * giPi * giRandom(rng);
        float r = sqrt(u);
        vec3 d = tangent * (r * cos(phi)) + bitangent * (r * sin(phi)) + n * sqrt(1.0 - u);
        vec3 hitP, hitN;
        int hit = giTrace(origin, d, 100.0, hitP, hitN);
        // E/pi estimator for cosine-weighted hemisphere samples. The emitter
        // is excluded from the ray scene: its direct term is already rendered.
        if (hit >= 0) sum += giDirectDiffuse(hit, hitP, hitN, -d, rng);
    }
    vec3 estimate = sum / float(uGiRays);
    // First frame never reads uninitialized texture memory. Later frames use
    // a capped averaging time constant; scene/light edits explicitly reset it.
    if (uGiBlend < 1.0) estimate = mix(texelFetch(uGiAtlas, cell, 0).rgb, estimate, uGiBlend);
    FragColor = vec4(estimate, 1.0);
}
)GLSL";
}

const char* DiffuseGI::samplingSource() { return kGiCommon; }
DiffuseGI::~DiffuseGI() { release(); }
void DiffuseGI::release() {
    for (int i = 0; i < 2; ++i) {
        if (m_fbo[i]) gl::DeleteFramebuffers(1, &m_fbo[i]);
        if (m_texture[i]) gl::DeleteTextures(1, &m_texture[i]);
        m_fbo[i] = m_texture[i] = 0;
    }
    if (m_vao) gl::DeleteVertexArrays(1, &m_vao);
    m_vao = 0;
    m_shader = Shader{};
}
bool DiffuseGI::init(int resolution, int rays, int history) {
    if (resolution < 4 || resolution > 64 || rays < 1 || rays > 64 || history < 1 || history > 128) return false;
    release();
    m_resolution = resolution; m_rays = rays; m_history = history;
    std::string source(kGiUpdateFragment);
    const std::string version = "#version 330 core\n";
    source.insert(source.find(version) + version.size(), kGiCommon);
    m_shader = Shader::fromSource(kGiVertex, source.c_str());
    if (!m_shader.valid()) return false;
    gl::GenVertexArrays(1, &m_vao);
    const int tile = resolution + 2;
    for (int i = 0; i < 2; ++i) {
        gl::GenTextures(1, &m_texture[i]);
        gl::BindTexture(gl::Texture2D, m_texture[i]);
        gl::TexImage2D(gl::Texture2D, 0, gl::RGBA16F, 6 * tile, kMaxSurfaces * tile, 0, gl::RGBA, gl::HalfFloat, nullptr);
        gl::TexParameteri(gl::Texture2D, gl::TextureMinFilter, gl::Linear);
        gl::TexParameteri(gl::Texture2D, gl::TextureMagFilter, gl::Linear);
        gl::TexParameteri(gl::Texture2D, gl::TextureWrapS, gl::ClampToEdge);
        gl::TexParameteri(gl::Texture2D, gl::TextureWrapT, gl::ClampToEdge);
        gl::GenFramebuffers(1, &m_fbo[i]);
        gl::BindFramebuffer(gl::Framebuffer, m_fbo[i]);
        gl::FramebufferTexture2D(gl::Framebuffer, gl::ColorAttachment0, gl::Texture2D, m_texture[i], 0);
        if (gl::CheckFramebufferStatus(gl::Framebuffer) != gl::FramebufferComplete) {
            std::fprintf(stderr, "[GI] Incomplete surface atlas.\n");
            gl::BindFramebuffer(gl::Framebuffer, 0); release(); return false;
        }
    }
    gl::BindFramebuffer(gl::Framebuffer, 0);
    gl::BindTexture(gl::Texture2D, 0);
    resetHistory();
    return true;
}
bool DiffuseGI::setScene(const std::vector<GiSurface>& surfaces) {
    if (surfaces.size() > kMaxSurfaces) return false;
    for (const auto& s : surfaces) {
        for (int a = 0; a < 4; ++a)
            if (!std::isfinite(s.lo[a]) || !std::isfinite(s.hi[a]) || !std::isfinite(s.material[a])) return false;
        for (int a = 0; a < 3; ++a)
            if (s.lo[a] > s.hi[a] || s.material[a] < 0 || s.material[a] > 1) return false;
        if ((s.lo[3] != 0 && s.lo[3] != 1) || s.hi[3] < 1 || s.hi[3] > 63 ||
            s.hi[3] != std::floor(s.hi[3]) || s.material[3] < 0 || s.material[3] > 1) return false;
        if (s.lo[3] == 1 && s.hi[0] <= s.lo[0]) return false;
    }
    bool same = surfaces.size() == m_surfaces.size();
    if (same) for (std::size_t i = 0; i < surfaces.size(); ++i)
        same = same && surfaces[i].lo == m_surfaces[i].lo && surfaces[i].hi == m_surfaces[i].hi && surfaces[i].material == m_surfaces[i].material;
    if (!same) { m_surfaces = surfaces; resetHistory(); }
    return true;
}
void DiffuseGI::setLight(const Vec3& center, float hx, float hz, float radiance) {
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
        !std::isfinite(hx) || !std::isfinite(hz) || !std::isfinite(radiance) || hx <= 0 || hz <= 0 || radiance < 0) return;
    std::array<float,4> light{center.x, center.y, center.z, radiance}, patch{hx,hz,0,0};
    if (light != m_light || patch != m_patch) { m_light = light; m_patch = patch; resetHistory(); }
}
void DiffuseGI::resetHistory() { m_frame = m_historyFrames = 0; m_uniformsDirty = true; }
void DiffuseGI::update() {
    if (!valid() || m_surfaces.empty()) return;
    int back = 1 - m_front;
    gl::BindFramebuffer(gl::Framebuffer, m_fbo[back]);
    gl::Viewport(0, 0, 6 * (m_resolution + 2), kMaxSurfaces * (m_resolution + 2));
    gl::Disable(gl::DepthTest);
    m_shader.bind();
    if (m_uniformsDirty) {
        float lo[kMaxSurfaces * 4]{}, hi[kMaxSurfaces * 4]{}, mat[kMaxSurfaces * 4]{};
        for (std::size_t i = 0; i < m_surfaces.size(); ++i) {
            std::copy(m_surfaces[i].lo.begin(), m_surfaces[i].lo.end(), lo + i * 4);
            std::copy(m_surfaces[i].hi.begin(), m_surfaces[i].hi.end(), hi + i * 4);
            std::copy(m_surfaces[i].material.begin(), m_surfaces[i].material.end(), mat + i * 4);
        }
        m_shader.setInt("uGiCount", surfaceCount());
        m_shader.setInt("uGiResolution", m_resolution);
        m_shader.setInt("uGiRays", m_rays);
        m_shader.setInt("uGiAtlas", 2);
        m_shader.setFloat4Array("uGiLo[0]", lo, surfaceCount());
        m_shader.setFloat4Array("uGiHi[0]", hi, surfaceCount());
        m_shader.setFloat4Array("uGiMaterial[0]", mat, surfaceCount());
        m_shader.setFloat4("uGiLight", m_light[0],m_light[1],m_light[2],m_light[3]);
        m_shader.setFloat4("uGiPatch", m_patch[0],m_patch[1],0,0);
        m_uniformsDirty = false;
    }
    m_shader.setInt("uGiFrame", static_cast<int>(m_frame));
    m_shader.setFloat("uGiBlend", 1.0f / static_cast<float>(std::min(m_historyFrames + 1u, static_cast<unsigned>(m_history))));
    gl::ActiveTexture(gl::Texture0 + 2);
    gl::BindTexture(gl::Texture2D, m_texture[m_front]);
    gl::BindVertexArray(m_vao);
    gl::DrawArrays(gl::Triangles, 0, 3);
    gl::BindVertexArray(0);
    gl::Enable(gl::DepthTest);
    m_front = back;
    m_frame = (m_frame + 1) % 1048576u;
    m_historyFrames = std::min(m_historyFrames + 1u, static_cast<unsigned>(m_history));
}
void DiffuseGI::bind(Shader& shader, int index, unsigned unit) const {
    if (!valid() || index < 0 || index >= surfaceCount()) { shader.setInt("uGiSurface", -1); return; }
    gl::ActiveTexture(gl::Texture0 + unit);
    gl::BindTexture(gl::Texture2D, m_texture[m_front]);
    shader.setInt("uGiAtlas", static_cast<int>(unit));
    shader.setInt("uGiResolution", m_resolution);
    shader.setInt("uGiSurface", index);
    const auto& s = m_surfaces[static_cast<std::size_t>(index)];
    shader.setFloat4("uGiSurfaceLo", s.lo[0],s.lo[1],s.lo[2],s.lo[3]);
    shader.setFloat4("uGiSurfaceHi", s.hi[0],s.hi[1],s.hi[2],s.hi[3]);
}
}
