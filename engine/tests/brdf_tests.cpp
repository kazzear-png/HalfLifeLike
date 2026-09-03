//
// M3.2 BRDF verification: reference-value tests for the PBR shader math.
// Run: ctest --test-dir build --output-on-failure
//
// The CPU functions below are ports of the GLSL in sandbox/src/shaders.h.
// They pin the equations to INDEPENDENTLY derived reference values (a
// different algebraic derivation of the same published model, plus float64
// quadrature constants from tools/brdf_reference.py), so "does the renderer
// match the published math" is a test, not an opinion.
//
// Rule of the house: if you change a GLSL equation, mirror the change here
// and re-derive the anchors with tools/brdf_reference.py -- never tune a
// shader by eye alone.
//
// Reference sources for the model (no invented equations):
//   D -- GGX / Trowbridge-Reitz          (Walter et al. 2007)
//   V -- Smith height-correlated          (Heitz 2014; Karis 2013 / Filament form)
//   F -- Schlick                          (Schlick 1994)
// Ambient split: Karis 2013 / LearnOpenGL pre-IBL form.

#include <cmath>
#include <cstdio>

#ifdef _WIN32
#include <io.h>   // _isatty: detect a double-clicked console run
#endif

namespace {

int g_checks   = 0;
int g_failures = 0;

// Windows convenience: hold the console open when double-clicked so results
// are readable; ctest/CI runs (piped stdio) pass through with no pause.
void pauseIfInteractive() {
#ifdef _WIN32
    if (_isatty(_fileno(stdin))) {
        std::printf("\nPress Enter to close...");
        std::fflush(stdout);
        std::fgetc(stdin);
    }
#endif
}

void expectTrue(bool condition, const char* what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL: %s\n", what);
    }
}

void expectNear(float a, float b, float eps, const char* what) {
    expectTrue(std::fabs(a - b) <= eps, what);
}

constexpr float kPi = 3.14159265358979f;

// ---------------------------------------------------------------------------
// CPU ports of the GLSL (keep the structure equivalent to shaders.h)
// ---------------------------------------------------------------------------

// GGX / Trowbridge-Reitz normal distribution.
float dGgx(float NoH, float alpha) {
    const float a2 = alpha * alpha;
    const float d  = NoH * NoH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}

// Smith height-correlated visibility, Karis 2013 / Filament reference form
// (the M3.2 fixed shader form).
float vSmithGGXCorrelated(float NoV, float NoL, float alpha) {
    const float a2   = alpha * alpha;
    const float GGXV = NoL * std::sqrt(NoV * NoV * (1.0f - a2) + a2);
    const float GGXL = NoV * std::sqrt(NoL * NoL * (1.0f - a2) + a2);
    return 0.5f / std::fmax(GGXV + GGXL, 1e-5f);
}

// The M3.0/M3.1 shader form, kept ONLY as the documented regression: it
// squares (1 - a2) inside the sqrt, which matches no published Smith variant.
float vSmithOldSquared(float NoV, float NoL, float alpha) {
    const float a2 = alpha * alpha;
    const float lv = NoL * std::sqrt((NoV - a2 * NoV) * (NoV - a2 * NoV) + a2);
    const float ll = NoV * std::sqrt((NoL - a2 * NoL) * (NoL - a2 * NoL) + a2);
    return 0.5f / std::fmax(lv + ll, 1e-5f);
}

// Independent derivation of the exact height-correlated Smith visibility
// (Heitz 2014): V = G2 / (4 * NoV * NoL), G2 = 1 / (1 + LambdaV + LambdaL),
// Lambda(t) = (sqrt(1 + a^2 tan^2(theta_t)) - 1) / 2.
// Shares no code with the simplified form above -- agreement between the two
// is the test.
float vLambdaExact(float NoV, float NoL, float alpha) {
    const float tan2V = (1.0f - NoV * NoV) / (NoV * NoV);
    const float tan2L = (1.0f - NoL * NoL) / (NoL * NoL);
    const float lambdaV = 0.5f * (std::sqrt(1.0f + alpha * alpha * tan2V) - 1.0f);
    const float lambdaL = 0.5f * (std::sqrt(1.0f + alpha * alpha * tan2L) - 1.0f);
    return 1.0f / (4.0f * NoV * NoL * (1.0f + lambdaV + lambdaL));
}

// Schlick Fresnel (explicit-multiplies form, as in the shader).
float fSchlick(float VoH, float f0) {
    const float f  = 1.0f - VoH;
    const float f2 = f * f;
    const float f5 = f2 * f2 * f;
    return f0 + (1.0f - f0) * f5;
}

// Ambient Fresnel: grazing ceiling capped by roughness (Karis 2013 /
// LearnOpenGL pre-IBL form). F0 stays the normal-incidence value.
float fSchlickRoughness(float NoV, float f0, float roughness) {
    const float fMax = std::fmax(1.0f - roughness, f0);
    const float f  = 1.0f - NoV;
    const float f5 = f * f * f * f * f;
    return f0 + (fMax - f0) * f5;
}

// Deterministic equal-solid-angle hemisphere quadrature of the white-furnace
// integral (F = 1): integral of D * V * NoL dOmega over the upper hemisphere.
// The grid is uniform in (phi, cos-theta), so every cell carries the same
// solid angle and the estimator is a plain average -- no RNG, CI-stable.
// N = +Y, view in the XY plane; mirrors the float64 reference in
// tools/brdf_reference.py.
float whiteFurnace(float NoV, float alpha, int nCos, int nPhi) {
    const float tv = std::acos(NoV);
    const float vx = std::sin(tv), vy = NoV, vz = 0.0f;

    auto brdfTimesNoL = [&](float cosTheta, float phi) -> float {
        const float st = std::sqrt(std::fmax(0.0f, 1.0f - cosTheta * cosTheta));
        const float lx = st * std::cos(phi);
        const float ly = cosTheta;
        const float lz = st * std::sin(phi);
        const float hx = vx + lx, hy = vy + ly, hz = vz + lz;
        const float hl = std::sqrt(hx * hx + hy * hy + hz * hz);
        if (hl < 1e-12f) return 0.0f;
        const float NoH = hy / hl;
        if (NoH <= 0.0f) return 0.0f;
        if (ly <= 0.0f) return 0.0f;
        return dGgx(NoH, alpha) * vSmithGGXCorrelated(NoV, ly, alpha) * ly;
    };

    double total = 0.0;
    for (int j = 0; j < nCos; ++j) {
        const float ct = static_cast<float>(j + 0.5) / static_cast<float>(nCos);
        for (int i = 0; i < nPhi; ++i) {
            const float phi = 2.0f * kPi * static_cast<float>(i + 0.5) / static_cast<float>(nPhi);
            total += static_cast<double>(brdfTimesNoL(ct, phi));
        }
    }
    // Each cell spans (2*pi / nPhi) in phi and (1 / nCos) in cos-theta.
    return static_cast<float>(total * (2.0 * kPi) / (static_cast<double>(nPhi) * nCos));
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// The M3.2 core fix: the simplified Karis/Filament form must equal the exact
// height-correlated Smith visibility everywhere on the domain.
void testVisibilityMatchesIndependentDerivation() {
    std::printf("[brdf] V: simplified form vs independent Lambda derivation\n");
    const float alphas[] = { 0.05f, 0.1f, 0.2f, 0.35f, 0.5f, 0.7f, 0.9f, 1.0f };
    for (const float alpha : alphas) {
        float worst = 0.0f;
        for (int i = 1; i <= 20; ++i) {
            const float NoV = 0.05f * static_cast<float>(i);
            for (int j = 1; j <= 20; ++j) {
                const float NoL   = 0.05f * static_cast<float>(j);
                const float ref   = vLambdaExact(NoV, NoL, alpha);
                const float got   = vSmithGGXCorrelated(NoV, NoL, alpha);
                const float rel   = std::fabs(got - ref) / (std::fabs(ref) + 1e-12f);
                if (rel > worst) worst = rel;
            }
        }
        expectTrue(worst < 2e-5f, "V form equals exact height-correlated Smith");
        std::printf("  alpha %.2f: max relative deviation %.2e\n", static_cast<double>(alpha), static_cast<double>(worst));
    }
}

// Closed-form anchors -- values derivable by hand from the equations.
void testClosedFormAnchors() {
    std::printf("[brdf] closed-form anchors\n");

    // At normal incidence for BOTH view and light, every Smith variant gives
    // V = 0.25 (GGXV = GGXL = 1 for any alpha).
    const float alphas[] = { 0.05f, 0.35f, 1.0f };
    for (const float alpha : alphas) {
        expectNear(vSmithGGXCorrelated(1.0f, 1.0f, alpha), 0.25f, 1e-6f,
                   "V(NoV=1, NoL=1) == 0.25 for any alpha");
    }

    // Fully rough limit (a2 = 1): V collapses to 0.5 / (NoV + NoL).
    expectNear(vSmithGGXCorrelated(0.5f, 0.25f, 1.0f), 2.0f / 3.0f, 1e-6f,
               "V(alpha=1) collapses to 0.5/(NoV+NoL)");

    // float64 point anchors from tools/brdf_reference.py (Lambda derivation).
    expectNear(vSmithGGXCorrelated(0.70f, 0.60f, 0.50f), 0.5121564492f, 1e-5f,
               "V point anchor A (float64 Lambda reference)");
    expectNear(vSmithGGXCorrelated(0.25f, 0.90f, 0.20f), 0.9791287342f, 1e-5f,
               "V point anchor B (float64 Lambda reference)");

    // GGX normalization anchor: D(NoH=1) = 1 / (pi * alpha^2).
    expectNear(dGgx(1.0f, 0.5f), 4.0f / kPi, 1e-5f, "D(NoH=1, alpha=0.5) == 4/pi");
    expectNear(dGgx(0.0f, 0.5f), 0.25f / kPi, 1e-6f, "D(NoH=0, alpha=0.5) == a2/pi");

    // float64 point anchors for D.
    expectNear(dGgx(0.90f, 0.25f), 0.3435964364f, 1e-5f, "D point anchor A");
    expectNear(dGgx(0.35f, 0.70f), 0.1774518341f, 1e-5f, "D point anchor B");
}

void testFresnelAnchors() {
    std::printf("[brdf] Fresnel anchors\n");

    expectNear(fSchlick(1.0f, 0.04f), 0.04f, 1e-7f, "F_Schlick(1) == F0");
    expectNear(fSchlick(0.0f, 0.04f), 1.0f, 1e-6f, "F_Schlick(0) == 1 (grazing)");
    expectNear(fSchlick(0.30f, 0.04f), 0.2013472000f, 1e-5f, "F_Schlick point anchor");

    // Ambient Fresnel keeps F0 at normal incidence for any roughness.
    expectNear(fSchlickRoughness(1.0f, 0.04f, 0.8f), 0.04f, 1e-7f,
               "F_SchlickRoughness(NoV=1) == F0");
    // Roughness 0 must reduce to classic Schlick.
    expectNear(fSchlickRoughness(0.40f, 0.04f, 0.0f), fSchlick(0.40f, 0.04f), 1e-6f,
               "F_SchlickRoughness(roughness=0) == F_Schlick");
    // float64 point anchor.
    expectNear(fSchlickRoughness(0.40f, 0.04f, 0.8f), 0.0524416000f, 1e-5f,
               "F_SchlickRoughness point anchor");
}

// The ambient energy rules the external review found missing in M3.1.
void testAmbientSplitRules() {
    std::printf("[brdf] ambient split rules\n");

    // Metals must receive ZERO fake diffuse ambient: kD = (1-F)(1-metalness).
    // This is exactly the inconsistency the M3.2 fix removed (old code
    // multiplied raw albedo by the ambient gradient regardless of metalness).
    const float metalness = 1.0f;
    const float F = fSchlickRoughness(0.5f, 0.04f, 0.5f);
    const float kD = (1.0f - F) * (1.0f - metalness);
    expectNear(kD, 0.0f, 1e-7f, "metalness=1 kills ambient diffuse (kD == 0)");

    // Roughness caps only the grazing ceiling; F0 anchor stays put.
    expectTrue(fSchlickRoughness(0.0f, 0.04f, 0.9f) < 0.2f,
               "rough surface does not reach full mirror Fresnel at grazing");
    expectTrue(fSchlickRoughness(0.0f, 0.04f, 0.9f) < fSchlickRoughness(0.0f, 0.04f, 0.5f),
               "grazing ceiling falls as roughness rises");
}

// White-furnace reference integrals: integral of D * V * NoL dOmega (F = 1)
// over the upper hemisphere, float64 quadrature anchors (tools/brdf_reference.py).
// NOTE: a single-scattering microfacet model does NOT integrate to 1.0 -- the
// missing energy is light that bounces between microfacets (multi-scattering).
// That deficit is a documented M9 research slot (Heitz-style energy
// compensation), not a bug. What this test pins is the exact D*V PAIRING:
// any future edit to either equation shifts these numbers immediately.
void testWhiteFurnaceReferenceIntegrals() {
    std::printf("[brdf] white-furnace reference integrals (F = 1)\n");
    struct Anchor { float NoV; float alpha; float value; };
    const Anchor anchors[] = {
        { 0.3f, 0.15f, 0.8932922f },
        { 0.3f, 0.35f, 0.7998605f },
        { 0.3f, 0.7f,  0.6692388f },
        { 0.7f, 0.15f, 0.9566636f },
        { 0.7f, 0.35f, 0.8030286f },
        { 0.7f, 0.7f,  0.5426401f },
        { 1.0f, 0.15f, 0.9717655f },
        { 1.0f, 0.35f, 0.8337258f },
        { 1.0f, 0.7f,  0.5037174f },
    };
    for (const Anchor& a : anchors) {
        const float got = whiteFurnace(a.NoV, a.alpha, 128, 128);
        expectNear(got, a.value, 0.005f, "white-furnace integral matches float64 reference");
        std::printf("  NoV %.2f alpha %.2f: %.6f (ref %.6f)\n",
                    static_cast<double>(a.NoV), static_cast<double>(a.alpha),
                    static_cast<double>(got), static_cast<double>(a.value));
    }
    expectTrue(whiteFurnace(0.7f, 0.7f, 128, 128) < 0.99f,
               "single-scattering furnace shows the documented multi-scatter deficit");
}

// Documents the bug the M3.2 fix removed: the old squared-term visibility
// deviates from the exact Smith term by >10% at high roughness. If this
// check ever fails, the "old" formula has drifted toward the exact one --
// meaning somebody edited the wrong equation.
void testOldSquaredTermRegression() {
    std::printf("[brdf] M3.1 regression reference (old squared-term form)\n");
    const float oldV  = vSmithOldSquared(0.975f, 0.975f, 0.7f);
    const float exact = vLambdaExact(0.975f, 0.975f, 0.7f);
    const float rel   = std::fabs(oldV - exact) / exact;
    expectTrue(rel > 0.05f, "old form is measurably wrong (>13% here) -- fix was real");
    std::printf("  old form deviates %.1f%% from exact Smith at NoV=NoL=0.975, alpha=0.7\n",
                static_cast<double>(rel) * 100.0);
}

} // namespace

int main() {
    std::printf("=== engine BRDF reference tests (M3.2) ===\n");
    testVisibilityMatchesIndependentDerivation();
    testClosedFormAnchors();
    testFresnelAnchors();
    testAmbientSplitRules();
    testWhiteFurnaceReferenceIntegrals();
    testOldSquaredTermRegression();

    std::printf("[brdf] %d checks, %d failure(s)\n", g_checks, g_failures);
    pauseIfInteractive();
    return g_failures == 0 ? 0 : 1;
}
