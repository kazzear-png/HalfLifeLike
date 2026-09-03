#pragma once
//
// Sandbox shaders (M3).
//
// PBR = established real-time math, no invented equations:
//   diffuse  : Lambert (albedo / PI)
//   specular : Cook-Torrance
//     D -- GGX / Trowbridge-Reitz normal distribution
//     V -- Smith height-correlated visibility (Karis 2013 / Filament form;
//          algebraically exact -- cross-checked against the independent
//          Lambda-based derivation in engine/tests/brdf_tests.cpp)
//     F -- Schlick approximation
//   workflow : metallic-roughness
//   ambient  : hemispheric gradient, split with the SAME Fresnel/metalness
//          energy rules as the direct term (M3.2)
//
// All lighting runs in LINEAR space and outputs HDR (unclamped) radiance;
// the Renderer's tonemap pass handles exposure + ACES + sRGB encode.
// Light radiance terms use physical inverse-square falloff for point/spot.

namespace shaders {

// ---------------------------------------------------------------------------
// Lit PBR shader
// ---------------------------------------------------------------------------

inline const char* kPbrVertex = R"GLSL(
#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat4 uNormalMat;      // inverse-transpose of uModel (sliced to mat3 here)

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;

void main()
{
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal   = mat3(uNormalMat) * aNormal;
    vColor    = aColor;
    gl_Position = uViewProj * world;
}
)GLSL";

inline const char* kPbrFragment = R"GLSL(
#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;
out vec4 FragColor;

const float PI = 3.14159265359;

// --- material ---
uniform vec3  uAlbedo;
uniform float uRoughness;      // perceptual roughness (alpha = roughness^2)
uniform float uMetalness;
uniform float uUseVertexColor; // 1.0: albedo *= vertex color (M1/M2 meshes)
uniform int   uDebugMode;      // 0 shaded | 1 normals | 2 albedo | 3 metal-rough | 4 F0 | 5 validation

// --- camera ---
uniform vec3 uViewPos;

// --- directional "sun" ---
uniform vec3  uSunDirection;   // unit, points TOWARD the sun
uniform vec3  uSunColor;
uniform float uSunIntensity;

// --- hemispheric ambient (IBL stand-in; the research slot for probes) ---
uniform vec3 uAmbientSky;
uniform vec3 uAmbientGround;

// --- point lights (physical inverse-square falloff) ---
// Array size MUST match engine::kMaxPointLights (Lighting.h).
uniform int  uPointCount;
uniform vec3 uPointPos[16];
uniform vec3 uPointRadiance[16];  // color * intensity, precomputed on CPU

// --- camera flashlight ---
uniform int   uSpotOn;
uniform vec3  uSpotPos;
uniform vec3  uSpotDir;       // unit, pointing away from camera
uniform vec3  uSpotRadiance;
uniform float uSpotInnerCos;
uniform float uSpotOuterCos;

// --- Cook-Torrance terms ---------------------------------------------------

float D_GGX(float NoH, float alpha)
{
    float a2 = alpha * alpha;
    float d  = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// NOTE: VoH arrives clamped to [0,1] from shadeLight. The explicit-multiply
// pow5 replaces pow(): pow(x, y) is undefined for x < 0 in GLSL, and a
// normalized V/H pair can land a few ulp outside that domain. With the
// flashlight sitting ON the camera, L ≈ V across the whole beam, so VoH
// hugs 1.0 to within rounding noise -- exactly where an unclamped pow(1-VoH,5)
// went negative and produced NaN speckle + low-precision-pow banding rings
// (seen on real hardware, M3.1). Multiplies are also faster on every driver.
vec3 F_Schlick(float VoH, vec3 f0)
{
    float f  = 1.0 - VoH;
    float f2 = f * f;
    float f5 = f2 * f2 * f;
    return f0 + (vec3(1.0) - f0) * f5;
}

// Ambient-light Fresnel: Schlick with the grazing ceiling capped by
// roughness -- a rough surface never reaches full mirror reflectance at
// grazing angles, while F0 itself stays the normal-incidence value.
// Established pre-IBL ambient split (Karis 2013 / LearnOpenGL IBL form).
// Explicit multiplies instead of pow(), same domain-safety reasoning as
// F_Schlick above (NoV arrives clamped to [0,1]).
vec3 F_SchlickRoughness(float NoV, vec3 f0, float roughness)
{
    vec3  fMax = max(vec3(1.0 - roughness), f0);
    float f    = 1.0 - NoV;
    float f5   = f * f * f * f * f;
    return f0 + (fMax - f0) * f5;
}

// Smith height-correlated GGX visibility: V = G2 / (4 * NoV * NoL), written
// in the Karis 2013 / Filament reference form. This is the ALGEBRAICALLY
// EXACT height-correlated Smith term (Heitz 2014):
//   G2 = 1 / (1 + LambdaV + LambdaL),
//   Lambda(t) = (sqrt(1 + a^2 tan^2(theta_t)) - 1) / 2,
// and the closed form below simplifies to it with zero approximation
// (verified to ~1e-16 in engine/tests/brdf_tests.cpp, which also pins
// white-furnace reference integrals).
//
// M3.2 fix: the previous form squared (1 - a2) inside the sqrt --
//   lambdaV = NoL * sqrt((NoV - a2 * NoV)^2 + a2)
// -- which is NOT any published Smith variant and bent the specular response
// by up to ~13% at medium/high roughness (measured against the exact form).
float V_SmithGGXCorrelated(float NoV, float NoL, float alpha)
{
    float a2   = alpha * alpha;
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(GGXV + GGXL, 1e-5);
}

// Single-light Cook-Torrance evaluation. radiance = color * intensity * falloff.
vec3 shadeLight(vec3 N, vec3 V, vec3 L, vec3 radiance,
                vec3 albedo, float alpha, vec3 f0, float metalness)
{
    float NoL = dot(N, L);
    if (NoL <= 0.0) {
        return vec3(0.0);
    }
    // Degenerate half-vector guard: when V + L nearly cancels, normalize()
    // amplifies rounding noise into a wild H (and dot(V,H) can exceed 1.0).
    // Falling back to N keeps every term finite; the case is a measure-zero
    // pixel set (light exactly opposite the view ray).
    vec3  H   = V + L;
    H         = (dot(H, H) > 1e-8) ? normalize(H) : N;
    float NoV = clamp(dot(N, V), 1e-4, 1.0);
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);

    float D   = D_GGX(NoH, alpha);
    vec3  F   = F_Schlick(VoH, f0);
    float Vis = V_SmithGGXCorrelated(NoV, NoL, alpha);

    vec3 specular = D * F * Vis;
    vec3 diffuse  = (vec3(1.0) - F) * (1.0 - metalness) * albedo / PI;

    return (diffuse + specular) * radiance * NoL;
}

void main()
{
    vec3  N = normalize(vNormal);
    vec3  V = normalize(uViewPos - vWorldPos);
    // Alpha floor: alpha == 0 makes D_GGX divide by zero at NoH == 1 (0/0 NaN).
    float alpha = max(uRoughness * uRoughness, 2e-3);

    vec3 albedo = mix(uAlbedo, uAlbedo * vColor, uUseVertexColor);
    vec3 f0 = mix(vec3(0.04), albedo, uMetalness);   // 4% dielectric reflectance

    // --- material debug / validation views (Valve-style practice) ----------
    // Cheap modes bail BEFORE the light loop: never compute what is not
    // shown. Debug colors still pass through the standard HDR -> ACES ->
    // sRGB display transform, so read them comparatively, not absolutely.
    if (uDebugMode == 1) { FragColor = vec4(N * 0.5 + 0.5, 1.0); return; }
    if (uDebugMode == 2) { FragColor = vec4(albedo, 1.0); return; }
    if (uDebugMode == 3) { FragColor = vec4(uMetalness, uRoughness, 0.0, 1.0); return; }
    if (uDebugMode == 4) { FragColor = vec4(f0, 1.0); return; }
    if (uDebugMode == 5) {
        // Material validation, Dota 2 rules: metalness is binary; dielectric
        // albedo must stay below emitter-level brightness; metal F0 must sit
        // inside the plausible-metal band; no accidental perfect mirrors.
        // Valid materials render as a green-scaled luminance map; violations
        // render saturated red.
        float lum = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
        bool  bad = false;
        if (uMetalness > 0.04 && uMetalness < 0.96) bad = true;
        if (uMetalness <  0.5 && lum > 0.70)        bad = true;
        if (uMetalness >= 0.5 && lum < 0.45)        bad = true;
        if (uRoughness < 0.03 || uRoughness > 1.0)  bad = true;
        FragColor = bad ? vec4(1.0, 0.10, 0.05, 1.0)
                        : vec4(vec3(0.20, 0.85, 0.35) * (0.30 + 0.70 * lum), 1.0);
        return;
    }

    // --- ambient (hemispheric irradiance; the IBL research slot) -----------
    // M3.2 correctness fix: ambient now splits energy with the SAME rules as
    // the direct term. The old code multiplied the gradient by raw albedo
    // regardless of metalness -- direct diffuse correctly vanished as
    // metalness -> 1, ambient did not, so chrome was lit by fake diffuse
    // light (external review finding, M3.2). Now: Fresnel-weighted diffuse
    // via kD, and the energy diffuse gives up returns as environment
    // specular sampled off the same gradient.
    float up = N.y * 0.5 + 0.5;
    vec3 ambientIrradiance = mix(uAmbientGround, uAmbientSky, up);

    float NoV   = clamp(dot(N, V), 1e-4, 1.0);
    vec3  F_amb = F_SchlickRoughness(NoV, f0, uRoughness);
    vec3  kD    = (vec3(1.0) - F_amb) * (1.0 - uMetalness);
    vec3  ambientDiffuse = kD * albedo * ambientIrradiance;

    // Placeholder for the M5 prefiltered-specular IBL slot: sample the same
    // analytic gradient along the reflection vector and crudely "prefilter"
    // by lerping toward the hemispheric average as roughness grows. This
    // whole block is replaced by irradiance + prefiltered mips + BRDF LUT
    // once a real environment map exists; metals and grazing angles are the
    // visible beneficiaries already.
    vec3  R     = reflect(-V, N);
    float rup   = R.y * 0.5 + 0.5;
    vec3  envRefl = mix(uAmbientGround, uAmbientSky, rup);
    vec3  prefiltered = mix(envRefl, ambientIrradiance, uRoughness);
    vec3  ambientSpecular = prefiltered * F_amb;

    vec3 ambient = ambientDiffuse + ambientSpecular;

    // --- sun (directional, no falloff) ---
    vec3 color = ambient;
    color += shadeLight(N, V, uSunDirection, uSunColor * uSunIntensity,
                        albedo, alpha, f0, uMetalness);

    // --- point lights ---
    for (int i = 0; i < 16; ++i) {
        if (i >= uPointCount) break;
        vec3  toLight = uPointPos[i] - vWorldPos;
        float dist    = length(toLight);
        vec3  L       = toLight / max(dist, 1e-4);
        vec3  radiance = uPointRadiance[i] / max(dist * dist, 1e-4);
        color += shadeLight(N, V, L, radiance, albedo, alpha, f0, uMetalness);
    }

    // --- flashlight (smooth-edged cone on the camera) ---
    if (uSpotOn == 1) {
        vec3  toLight = uSpotPos - vWorldPos;
        float dist    = length(toLight);
        vec3  L       = toLight / max(dist, 1e-4);
        float theta   = clamp(dot(-L, uSpotDir), 0.0, 1.0);  // normalized-dot rounding can pass 1.0
        float spot    = smoothstep(uSpotOuterCos, uSpotInnerCos, theta);   // edge0 < edge1 by construction
        if (spot > 0.0) {
            vec3 radiance = uSpotRadiance / max(dist * dist, 1e-4);
            color += shadeLight(N, V, L, radiance * spot, albedo, alpha, f0, uMetalness);
        }
    }

    FragColor = vec4(color, 1.0);   // linear HDR; tonemap pass encodes sRGB
}
)GLSL";

// ---------------------------------------------------------------------------
// Unlit shader -- draws the visible light-bulb spheres (radiance made visible).
// ---------------------------------------------------------------------------

inline const char* kUnlitVertex = R"GLSL(
#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
}
)GLSL";

inline const char* kUnlitFragment = R"GLSL(
#version 330 core

in vec3 vColor;
out vec4 FragColor;

uniform vec3 uTint;   // color * intensity; HDR values > 1 bloom after tonemap

void main()
{
    FragColor = vec4(vColor * uTint, 1.0);
}
)GLSL";

} // namespace shaders
