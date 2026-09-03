#!/usr/bin/env python3
"""
Independent float64 reference computations for the M3.2 BRDF tests.

Produces the hardcoded anchor values that engine/tests/brdf_tests.cpp asserts
against, plus evidence numbers for the docs:

  1. V_SmithGGXCorrelated (Filament form) vs the independent Lambda-based
     exact height-correlated Smith visibility  -> must agree to ~1e-7.
  2. The OLD (M3.0/M3.1) shader form vs the exact one -> quantifies the bug.
  3. White-furnace reference integrals: integral of D * V * NoL domega
     (F = 1) over the upper hemisphere. NOTE: a single-scattering
     microfacet model does NOT integrate to 1.0 -- the deficit is
     multi-scattering energy (a documented M9 research slot). The anchors
     pin the exact D*V pairing so any edit to either equation shifts them.
"""

import math

PI = math.pi


def d_ggx(noh: float, alpha: float) -> float:
    a2 = alpha * alpha
    d = noh * noh * (a2 - 1.0) + 1.0
    return a2 / (PI * d * d)


def v_filament(nov: float, nol: float, alpha: float) -> float:
    """Fixed shader form (should equal the exact height-correlated Smith)."""
    a2 = alpha * alpha
    ggxv = nol * math.sqrt(nov * nov * (1.0 - a2) + a2)
    ggxl = nov * math.sqrt(nol * nol * (1.0 - a2) + a2)
    return 0.5 / max(ggxv + ggxl, 1e-5)


def v_old_buggy(nov: float, nol: float, alpha: float) -> float:
    """M3.0/M3.1 shader form: (1 - a2) accidentally squared inside the sqrt."""
    a2 = alpha * alpha
    lv = nol * math.sqrt((nov - a2 * nov) * (nov - a2 * nov) + a2)
    ll = nov * math.sqrt((nol - a2 * nol) * (nol - a2 * nol) + a2)
    return 0.5 / max(lv + ll, 1e-5)


def v_lambda_exact(nov: float, nol: float, alpha: float) -> float:
    """Independent derivation: V = G2 / (4 NoV NoL), G2 = 1/(1+Lv+Ll),
    Lambda(t) = (sqrt(1 + a^2 tan^2 theta) - 1)/2  (Heitz 2014)."""
    a = alpha
    tan2_v = (1.0 - nov * nov) / (nov * nov)
    tan2_l = (1.0 - nol * nol) / (nol * nol)
    lv = 0.5 * (math.sqrt(1.0 + a * a * tan2_v) - 1.0)
    ll = 0.5 * (math.sqrt(1.0 + a * a * tan2_l) - 1.0)
    return 1.0 / (4.0 * nov * nol * (1.0 + lv + ll))


def max_rel_dev(f, g, alphas) -> tuple[float, tuple]:
    worst, where = 0.0, None
    i = 0.025
    while i <= 1.0:
        j = 0.025
        while j <= 1.0:
            for a in alphas:
                x, y = f(i, j, a), g(i, j, a)
                rel = abs(x - y) / max(abs(x), 1e-12)
                if rel > worst:
                    worst, where = rel, (i, j, a)
            j += 0.025
        i += 0.025
    return worst, where


def white_furnace(v_func, nov: float, alpha: float, n_cos: int = 512,
                  n_phi: int = 512) -> float:
    """Deterministic quadrature of integral D*V*NoL dOmega (F=1) over the
    upper hemisphere. Grid uniform in (phi, cos-theta) => equal solid angle
    cells => plain averaging with the 2*pi*2 normalization."""
    # view direction in the xz->xy plane: V = (sin t, cos t, 0)
    tv = math.acos(nov)
    vx, vz = math.sin(tv), 0.0
    vy = nov

    def brdf_times_nol(ct: float, phi: float) -> float:
        sx, sy = math.sin(math.acos(ct)), ct
        lx = sx * math.cos(phi)
        ly = sy
        lz = sx * math.sin(phi)
        # half vector
        hx, hy, hz = vx + lx, vy + ly, vz + lz
        hl = math.sqrt(hx * hx + hy * hy + hz * hz)
        if hl < 1e-12:
            return 0.0
        noh = hy / hl
        if noh <= 0.0:
            return 0.0
        nol = ly
        if nol <= 0.0:
            return 0.0
        return d_ggx(noh, alpha) * v_func(nov, nol, alpha) * nol

    total = 0.0
    for j in range(n_cos):
        ct = (j + 0.5) / n_cos            # uniform in cos-theta
        for i in range(n_phi):
            phi = 2.0 * PI * (i + 0.5) / n_phi
            total += brdf_times_nol(ct, phi)
    # each cell has solid angle (2*pi/n_phi) * (1/n_cos); sum f * dOmega
    return total * (2.0 * PI) / (n_phi * n_cos)


def main() -> None:
    alphas = [0.05, 0.1, 0.2, 0.35, 0.5, 0.7, 0.9, 1.0]

    # 1) Filament form == Lambda-exact height-correlated Smith
    dev, where = max_rel_dev(v_filament, v_lambda_exact, alphas)
    print(f"[filament vs lambda-exact] max rel dev = {dev:.3e} at NoV/NoL/alpha={where}")

    # 2) old buggy form deviation (the reviewer's ~10-12% claim)
    dev_old, where_old = max_rel_dev(v_old_buggy, v_lambda_exact, alphas)
    print(f"[old-buggy vs lambda-exact] max rel dev = {dev_old:.3%} at NoV/NoL/alpha={where_old}")

    # 3) point anchors to hardcode in brdf_tests.cpp (float64 truth)
    print("\n-- anchors (print with repr precision) --")
    v1 = v_lambda_exact(0.7, 0.6, 0.5)
    v2 = v_lambda_exact(0.25, 0.9, 0.2)
    d1 = d_ggx(0.9, 0.25)
    d2 = d_ggx(0.35, 0.7)
    print(f"V_LAMBDA_A  NoV=0.70 NoL=0.60 alpha=0.50 : {v1:.10f}")
    print(f"V_LAMBDA_B  NoV=0.25 NoL=0.90 alpha=0.20 : {v2:.10f}")
    print(f"D_GGX_A     NoH=0.90 alpha=0.25           : {d1:.10f}")
    print(f"D_GGX_B     NoH=0.35 alpha=0.70           : {d2:.10f}")
    print(f"D normalization anchor D(NoH=1, a=0.5)    : {d_ggx(1.0, 0.5):.10f}  (expect {1.0/(PI*0.25):.10f})")

    # 4) white furnace (F=1): correct V conserves, old V does not
    print("\n-- white furnace integral D*V*NoL dOmega (target 1.0) --")
    for nov in (0.3, 0.7, 1.0):
        for alpha in (0.15, 0.35, 0.7):
            ok = white_furnace(v_filament, nov, alpha)
            bad = white_furnace(v_old_buggy, nov, alpha)
            print(f"NoV={nov:<4} alpha={alpha:<4} fixed={ok:.5f}  old={bad:.5f}")

    # 5) fresnel anchors
    def f_schlick(voh, f0):
        f = 1.0 - voh
        f5 = f * f * f * f * f
        return f0 + (1.0 - f0) * f5

    # Established pre-IBL ambient Fresnel (LearnOpenGL / Karis ambient split):
    # F0 stays the normal-incidence value; the GRAZING ceiling is capped at
    # max(1 - roughness, F0) instead of 1.
    def f_schlick_rough(nov, f0, rough):
        fmax0 = max(1.0 - rough, f0)
        f5 = (1.0 - nov) ** 5
        return f0 + (fmax0 - f0) * f5

    print("\n-- fresnel anchors --")
    print(f"F_Schlick(0.30, 0.04)          : {f_schlick(0.30, 0.04):.10f}")
    print(f"F_SchlickRough(0.40, 0.04,0.8) : {f_schlick_rough(0.40, 0.04, 0.8):.10f}")
    print(f"F_SchlickRough(1.00, 0.04,0.8) : {f_schlick_rough(1.00, 0.04, 0.8):.10f}  (must equal f0)")
    print(f"F_SchlickRough(0.40, 0.04,0.0) : {f_schlick_rough(0.40, 0.04, 0.0):.10f}  (must equal F_Schlick: {f_schlick(0.40, 0.04):.10f})")


if __name__ == "__main__":
    main()
