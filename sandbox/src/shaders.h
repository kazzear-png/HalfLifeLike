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

// --- heightfield shadows (M4) ----------------------------------------------
// Two static top/bottom captures store, per footprint texel, the vertical
// INTERVAL of occluder geometry at that (x, z): uShadowHeightsMax = highest
// surface Y, uShadowHeightsMin = lowest surface Y (R16F,
// engine::ShadowHeightfield). Per light, the receiver -> light segment is
// marched in XZ: the light is blocked where the ray height falls INSIDE the
// local column interval. EXACT for convex occluders -- the entire frozen
// Cornell set (blocks, baffle, spheres are all convex, so the column
// interval is their full vertical extent); conservative for non-convex
// columns (shadows slightly large, never small).
// Footprint uniforms pack XZ into vec3: the GL loader carries no Uniform2f
// entry point (surface-area discipline, see GL.h); the third component is
// padding. THE PACK IS (worldX, worldZ, pad): world Z lives in COMPONENT 1,
// so the (x, z) pair unpacks as .xy. Unpacking with .xz reads (worldX, pad)
// -- that exact one-token mistake was the M4.0.1..M4.0.4 inert-march root
// cause (see the BUGFIX note inside shadowVisibility).
uniform sampler2D uShadowHeightsMax;
uniform sampler2D uShadowHeightsMin;
uniform int   uShadowOn;            // 0 = demo scene / --no-shadows
uniform vec3  uShadowFootprintMin;  // world (x, z) of texel corner (0,0)
uniform vec3  uShadowFootprintMax;  // world (x, z) of texel corner (1,1)
uniform float uShadowMaxHeight;     // no occluder above this: early-out
uniform float uShadowStep;          // march step in world meters
uniform float uShadowBias;          // height bias in world meters
uniform float uShadowPenumbra;      // M4.0.7: soft-penumbra scale; 0 = exact
                                    // binary march (M4.0.6 regression mode).
                                    // Derived from the frozen rig, see
                                    // kShadowPenumbra in main.cpp.
uniform int   uShadowRefine;        // M4.0.8: bracket refinement of the soft
                                    // minimum (0 = exact M4.0.7 soft march).

// --- M4.0.5 shadow-march debug instrument (temporary) ----------------------
// --shadow-debug vis|field|uv, default OFF (0). All modes REPLACE the shaded
// image, so they are diagnostic-only: never feed one to the similarity
// ledger. They exist to split the surviving failure surface from inside the
// shader, where no CPU instrument can look:
//   vis   : the M4.0.5 request -- the march's verdict made blatant. Black
//           where ANY point light is blocked, bright where none is. Answers
//           "does the marcher ever detect the white/gray footprints".
//   field : R = hMax sampled at THIS fragment's own footprint uv (the
//           march's formula, one direct fetch); G = march calls / 16 (did
//           the march run at all?); B = uShadowMaxHeight / 6 (did that
//           uniform land?). Reads block FACES (their XZ is inside the
//           footprints), floor stays dark in R.
//   uv    : the march's uv formula at the receiver, as RG. Correct wiring =
//           smooth 0->1 red gradient along X and green along Z across the
//           room. A broken footprint unpack shows as a HARD BINARY SEAM in
//           green at z = 0 (uv.y = +/-inf -> clamped edge rows) -- the exact
//           M4.0.5 smoking gun, made visible.
// Debug values pass through the standard HDR -> ACES -> sRGB display
// transform: read them comparatively (0 stays black), not absolutely.
uniform int   uShadowDebug;         // 0 off | 1 vis | 2 field | 3 uv
uniform float uShadowDebugNorm;     // field-mode normalizer (frozen capture top)

float g_dbgMinVis = 1.0;            // min visibility across this fragment's lights
float g_dbgCalls  = 0.0;            // shadowVisibility() calls for this fragment

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

// Heightfield visibility for one point light (M4). Marches the XZ segment
// receiver -> light with a fixed world-space step. The dynamic loop bound is
// fine in GL 3.3; the body early-outs twice: once above every occluder (the
// ray can only rise from there), once on the first blocked sample.
// Numeric contract (pinned by bench_tests' C++ port of this march):
//   - sampling starts one step AWAY from the receiver: a surface can never
//     shadow itself with its own footprint texel;
//   - blocked = ray height inside [minH + bias, maxH - bias] at a sample:
//     the interval test is what keeps the hanging baffle (y 3.4..5.4) from
//     shadowing rays that pass beneath it;
//   - bias is a world-space epsilon (~half a texel height quantum), not a
//     slope-scaled fudge.
// M4.0.7 soft penumbra (uShadowPenumbra > 0): instead of a binary verdict,
// each sample reports its signed clearance d to the column interval and the
// march returns the MINIMUM of d against a penumbra window that grows with
// distance traveled (horizon-style accumulation). This extracts the
// penumbra already implicit in the march -- zero extra texture taps -- and
// converts the sampled staircase boundary into a graded edge. With
// uShadowPenumbra == 0 every decision below is IDENTICAL to the M4.0.6
// binary march (max(a,b) < 0 iff both old inequalities hold; the early-out
// only skips samples that could never block), so --shadow-penumbra 0 must
// reproduce that run's md5 byte-for-byte.
// M4.0.8 bracket refinement (uShadowRefine > 0, soft path only): the graded
// edge is computed from the SAMPLED minimum clearance, and near a silhouette
// the true minimum sits BETWEEN samples -- the graded value inherits the
// march cadence as staircase noise (the defect class GPU Gems 3 ch.18 names
// "the linear search ... is prone to aliasing"; see
// docs/SHADOW_EDGE_REFERENCES.md). The refinement phase is the
// reference-standard second half of every heightfield-ray solver (POM /
// relief mapping): two extra fetch pairs at t_best +/- half-step re-locate
// the dip at half-step resolution, engaged ONLY where the window actually
// fired (vis < 1) -- zero cost for fully lit or hard-blocked pixels. A
// refined fetch landing inside the interval returns the hard 0 (a crossing
// the cadence jumped over is a real crossing). Binary decisions (penumbra
// == 0) are untouched, so --shadow-penumbra 0 replay pins hold
// byte-for-byte, and --shadow-refine 0 reproduces the M4.0.7 soft march
// exactly.
float shadowVisibility(vec3 receiverPos, vec3 lightPos)
{
    if (uShadowDebug != 0) {
        g_dbgCalls += 1.0;   // instrument only: tells field mode the march ran
    }
    vec2  p0    = receiverPos.xz;
    vec2  p1    = lightPos.xz;
    vec2  delta = p1 - p0;
    float horiz = length(delta);
    if (horiz < 1e-4) {
        return 1.0;   // ray straight up: nothing can stand between
    }
    float y0 = receiverPos.y;
    float y1 = lightPos.y;
    float segLen = distance(receiverPos, lightPos);   // true path length: the
                                                      // penumbra window grows with this
    int steps = int(horiz / uShadowStep) + 1;
    // M4.0.5 BUGFIX: unpack the packed footprint with .xy, NOT .xz. The
    // uniforms carry (worldX, worldZ, pad), so (x, z) = components (0, 1) =
    // .xy. With .xz the span was (maxX - minX, pad - pad) = (5.5, 0.0):
    // invSpan.z = 1/0 = +inf, uv.y = worldZ * inf clamped to the field's
    // EDGE ROWS (floor texels, hMax = 0), and the interval test could never
    // fire. Every instrument passed -- capture, verifyField, registration
    // probes 7/7, --dump-heightfield (which computes uv from gl_FragCoord
    // and never touches these uniforms), binding, telemetry -- the march
    // executed every fetch (GPU +0.025 ms) and returned 1.0 forever, while
    // the image stayed byte-identical to --no-shadows. The bench_tests C++
    // port could not catch this class: it implements the INTENDED mapping,
    // not the shader's swizzle. GLSL-only divergences need GPU-side
    // instruments; uShadowDebug below is that instrument.
    vec2 invSpan = 1.0 / (uShadowFootprintMax.xy - uShadowFootprintMin.xy);
    float vis = 1.0;   // M4.0.7 soft accumulator (1 = fully lit)
    float tBest = -1.0;   // M4.0.8: t of the sample that last lowered vis
    for (int s = 1; s <= steps; ++s) {
        float t = float(s) / float(steps);
        float rayY = mix(y0, y1, t);
        // M4.0.7: penumbra-aware early-out. No column's top exceeds
        // uShadowMaxHeight, so a sample's GLOBAL clearance lower bound is
        // rayY - (uShadowMaxHeight - uShadowBias); every future sample's
        // clearance is at least that (the ray rises), and every future
        // penumbra window is at most uShadowPenumbra * segLen (t <= 1).
        // Once the bound exceeds the largest window, no future sample can
        // lower vis. With uShadowPenumbra == 0 this breaks one bias EARLIER
        // than the M4.0.6 early-out -- strictly on samples whose interval
        // test could never fire (blocking needs rayY < hMax - bias <=
        // maxHeight - bias) -- so the binary image is unchanged.
        if (rayY - (uShadowMaxHeight - uShadowBias) > uShadowPenumbra * segLen) {
            break;    // above every occluder's reach for the rest of the ray
        }
        // .xy, not .xz: the pack is (worldX, worldZ, pad) -- M4.0.5 BUGFIX.
        vec2 uv = (mix(p0, p1, t) - uShadowFootprintMin.xy) * invSpan;
        float hMax = texture(uShadowHeightsMax, uv).r;
        float hMin = texture(uShadowHeightsMin, uv).r;
        // Signed clearance to the column interval [hMin + bias, hMax - bias]:
        // negative = inside (hard block), positive = clear by that many
        // meters. max(a, b) < 0 iff BOTH old inequalities hold, so with
        // uShadowPenumbra == 0 this is the exact M4.0.6 binary decision.
        float d = max(hMin + uShadowBias - rayY, rayY - (hMax - uShadowBias));
        if (d < 0.0) {
            return 0.0;
        }
        if (uShadowPenumbra > 0.0) {
            float traveled = t * segLen;
            if (traveled > 2.0 * uShadowStep) {
                // Horizon-style soft accumulation: a sample that merely
                // GRAZES the interval (clearance small against a penumbra
                // window growing with distance traveled) shades partially
                // instead of flipping hard. The 2*step start zone keeps the
                // receiver's own neighborhood binary: contact shadows stay
                // crisp (physically correct) and a receiver standing on an
                // occluder top cannot self-shade (the acne the bias also
                // guards). Scale derivation: kShadowPenumbra in main.cpp.
                float graded = clamp(d / max(uShadowPenumbra * traveled, 1e-5),
                                     0.0, 1.0);
                if (graded < vis) {
                    vis = graded;   // min(), plus the argmin t for M4.0.8
                    tBest = t;
                }
            }
        }
    }
    // M4.0.8 bracket refinement (see the contract note above): two fetch
    // pairs at half-step offsets around the worst sample. Same interval
    // clearance, same window, same start zone as the linear search -- the
    // refinement can only lower vis toward the true minimum or confirm a
    // hard block; it can never invent occlusion for a clear ray.
    if (uShadowPenumbra > 0.0 && uShadowRefine > 0.5 && vis < 1.0 &&
        tBest >= 0.0) {
        float halfT = 0.5 / float(steps);
        for (int r = 0; r < 2; ++r) {
            float m = tBest + ((r == 0) ? -halfT : halfT);
            if (m <= 0.0 || m >= 1.0) {
                continue;
            }
            float rayYr = mix(y0, y1, m);
            if (rayYr - (uShadowMaxHeight - uShadowBias) >
                uShadowPenumbra * segLen) {
                continue;
            }
            // .xy, not .xz: the pack is (worldX, worldZ, pad) -- M4.0.5.
            vec2 uvR = (mix(p0, p1, m) - uShadowFootprintMin.xy) * invSpan;
            float hMaxR = texture(uShadowHeightsMax, uvR).r;
            float hMinR = texture(uShadowHeightsMin, uvR).r;
            float dR = max(hMinR + uShadowBias - rayYr,
                           rayYr - (hMaxR - uShadowBias));
            if (dR < 0.0) {
                return 0.0;   // a crossing the cadence jumped over: hard 0
            }
            float traveledR = m * segLen;
            if (traveledR > 2.0 * uShadowStep) {
                vis = min(vis,
                          clamp(dR / max(uShadowPenumbra * traveledR, 1e-5),
                                0.0, 1.0));
            }
        }
    }
    return vis;
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
    if (uShadowDebug == 3) {
        // --shadow-debug uv: the march's uv formula at this fragment's world
        // position, shown directly. Cheap on purpose: it needs no march, so
        // it bails BEFORE the light loop (never compute what is not shown).
        vec2 invSpanDbg = 1.0 / (uShadowFootprintMax.xy - uShadowFootprintMin.xy);
        vec2 uvDbg      = (vWorldPos.xz - uShadowFootprintMin.xy) * invSpanDbg;
        FragColor = vec4(uvDbg, 0.0, 1.0);
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
    // M4: the shadow test runs only after the NoL > 0 guard -- backfacing
    // lights must not pay for texture fetches (half the grid on average).
    for (int i = 0; i < 16; ++i) {
        if (i >= uPointCount) break;
        vec3  toLight = uPointPos[i] - vWorldPos;
        float dist    = length(toLight);
        vec3  L       = toLight / max(dist, 1e-4);
        if (dot(N, L) <= 0.0) {
            continue;
        }
        float vis = (uShadowOn == 1)
            ? shadowVisibility(vWorldPos, uPointPos[i]) : 1.0;
        if (uShadowDebug != 0) {
            g_dbgMinVis = min(g_dbgMinVis, vis);   // instrument only
        }
        if (vis <= 0.0) {
            continue;
        }
        vec3  radiance = uPointRadiance[i] / max(dist * dist, 1e-4);
        color += shadeLight(N, V, L, radiance, albedo, alpha, f0, uMetalness) * vis;
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

    if (uShadowDebug == 1) {
        // --shadow-debug vis: black = at least one point light blocked by the
        // march; bright = none blocked. THE blatant verdict the M4.0.5 run
        // asked for: if the marcher ever detects the footprints, they show
        // as black here regardless of what the BRDF does with visibility.
        FragColor = vec4(vec3(g_dbgMinVis), 1.0);
        return;
    }
    if (uShadowDebug == 2) {
        // --shadow-debug field: what the march's inputs look like from inside
        // the shader. R: one direct hMax fetch through the march's own uv
        // formula at the receiver (block faces light up; floor is dark --
        // floor texels are not inside any footprint). G: march calls / 16
        // (0 = shadowVisibility never ran -> uShadowOn not delivered).
        // B: uShadowMaxHeight / 6 (0 = that uniform never landed -> the
        // early-out breaks before the first fetch).
        vec2 invSpanDbg = 1.0 / (uShadowFootprintMax.xy - uShadowFootprintMin.xy);
        vec2 uvDbg      = (vWorldPos.xz - uShadowFootprintMin.xy) * invSpanDbg;
        float hDbg      = texture(uShadowHeightsMax, uvDbg).r;
        FragColor = vec4(clamp(hDbg / max(uShadowDebugNorm, 1e-4), 0.0, 1.0),
                         min(g_dbgCalls / 16.0, 1.0),
                         clamp(uShadowMaxHeight / 6.0, 0.0, 1.0), 1.0);
        return;
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

// ---------------------------------------------------------------------------
// Heightfield dump shader (M4.0.4 registration instrument, temporary).
// Renders a captured shadow-height field DIRECTLY TO THE SCREEN through the
// same wiring the shadow march uses: bindTextures(maxUnit, minUnit) followed
// by setInt("uShadowHeightsMax"/"...Min", unit) and the SAME footprint
// uniforms. If the march is inert but this dump shows the occluder
// footprints in the right places, the field and sampler path are fine and
// the bug lives in the march's uniforms/logic; if the dump is black or
// displaced, the field or its binding is the bug.
// ---------------------------------------------------------------------------

inline const char* kHeightfieldDumpVertex = R"GLSL(
#version 330 core

layout (location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uViewProj;

void main()
{
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
}
)GLSL";

inline const char* kHeightfieldDumpFragment = R"GLSL(
#version 330 core

out vec4 FragColor;

// SAME NAMES as the PBR march uniforms: the dump exercises the identical
// bind + setInt sequence (a miswired sampler shows up here too).
uniform sampler2D uShadowHeightsMax;
uniform sampler2D uShadowHeightsMin;
uniform vec3  uShadowFootprintMin;   // world (x, z) of texel corner (0,0)
uniform vec3  uShadowFootprintMax;   // world (x, z) of texel corner (1,1)
uniform int   uDumpField;            // 0 = max field, 1 = min field
uniform float uDumpNorm;             // gray 1.0 at this height (frozen top)
uniform float uDumpRes;              // dump viewport = capture resolution

void main()
{
    // One dump pixel = one field texel: uv = (texel center) / resolution.
    // This is the march's uv formula read backwards: world = min + uv*span.
    vec2 uv    = gl_FragCoord.xy / vec2(uDumpRes);
    float h    = (uDumpField == 0) ? texture(uShadowHeightsMax, uv).r
                                   : texture(uShadowHeightsMin, uv).r;
    float g    = clamp(h / uDumpNorm, 0.0, 1.0);
    FragColor  = vec4(g, g, g, 1.0);   // raw linear gray; NO tonemap
}
)GLSL";

} // namespace shaders
