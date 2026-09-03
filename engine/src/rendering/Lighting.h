#pragma once
//
// M3 lighting + material model.
//
// Foundation = established real-time math only (no invented BRDF):
//   - diffuse:  Lambert (energy-conserving, albedo / PI)
//   - specular: Cook-Torrance -- GGX distribution (D), Smith height-correlated
//     visibility (V), Schlick Fresnel (F)
//   - metallic-roughness workflow
//
// Light units are intentionally simple for M3:
//   - directional light: radiance = color * intensity (no falloff)
//   - point lights:      physical inverse-square falloff, radiance = color *
//                        intensity / d^2
//   - spot light:        point-light falloff + smooth cone edge
//
// Ambient is a cheap hemispheric (sky/ground) gradient -- the deliberate
// stand-in for IBL/probe lighting. That slot is where the renderer's
// "approximation research" lands later; the interface below is kept so an
// ambient-irradiance upgrade does not change call sites.

#include "math/Vec3.h"

#include <cstdint>

namespace engine {

// Upper bound for point lights per frame. Uniform arrays are sized to this;
// uPointLightCount selects how many are evaluated.
inline constexpr int kMaxPointLights = 8;

struct DirectionalLight {
    Vec3 direction{0.0f, -1.0f, 0.0f};  // unit vector, points FROM surface TOWARD the light
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

struct PointLight {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

struct SpotLight {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 direction{0.0f, -1.0f, 0.0f};  // unit vector, from the light outward
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float innerCutoffDegrees = 6.0f;    // full brightness inside
    float outerCutoffDegrees = 12.0f;   // smooth falloff to zero outside
};

// PBR material (metallic-roughness). albedo tints the mesh's vertex color,
// so authored vertex colors keep working (M1/M2 scene) and imported OBJ
// geometry (white vertex color) takes the material albedo directly.
struct PbrMaterial {
    Vec3  albedo{0.8f, 0.8f, 0.8f};
    float roughness    = 0.5f;   // 0 = mirror, 1 = fully rough
    float metalness    = 0.0f;   // 0 = dielectric, 1 = metal
    bool  useVertexColor = true; // when true, albedo *= vertex color
};

// Ambient terms (linear, HDR): sky lights up-facing normals, ground bounces
// into down-facing ones. Scaled by albedo inside the shader.
struct AmbientTerms {
    Vec3 sky{0.04f, 0.045f, 0.055f};
    Vec3 ground{0.02f, 0.02f, 0.022f};
};

} // namespace engine
