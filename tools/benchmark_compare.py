#!/usr/bin/env python3
"""
Two-axis benchmark metric for the cornell-box/1.0 standard.

Compares a renderer frame (PPM, written by `sandbox --benchmark N --out ...`)
against the reference path trace produced by tools/reference_pathtracer.py,
and reports a metric suite (M4; all on the 0-255 display scale unless noted):

  - RMSE (L2)      : overall disagreement,
  - MAE  (L1)      : mean absolute error -- robust to a few blown pixels,
  - SSIM           : structural similarity (luminance, standard 11x11
                     gaussian window) -> the "Cornell similarity" ledger axis,
  - dE2000         : mean perceptual color difference (CIELAB Delta E 2000,
                     Sharma 2005 formulation; display-referred sRGB -> Lab),
  - edge/shadow RMSE: RMSE over Sobel gradient-magnitude maps of luminance --
                     sensitive exactly where shadow placement/penumbra differ.

Protocol ("Cornell similarity = X, FPS = Y"):
  1. sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 \
         --out frame.ppm --report frame.txt
  2. python3 tools/benchmark_compare.py frame.ppm \
         benchmarks/cornell_box/reference/cbox01_clean.ppm
     (the CLEAN high-spp reference is the measurement target since M4;
     the *_reference.ppm MC renders stay archived as provenance)
  3. record (similarity, fps) for the milestone in
     benchmarks/cornell_box/BASELINE.md. The reference images NEVER change;
     every renderer change moves X and/or Y.

Both images must have identical dimensions.
Run with --self-test to execute the internal reference-value checks
(Sharma 2005 dE2000 pairs, SSIM/gradient identities).
"""

import argparse
import math
import sys

import numpy as np


def read_ppm(path):
    """Binary P6 PPM -> (H, W, 3) uint8."""
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"P6":
            raise ValueError(f"{path}: not a binary PPM (got {magic!r})")
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        w, h = map(int, line.split())
        maxval = int(f.readline())
        if maxval != 255:
            raise ValueError(f"{path}: unsupported maxval {maxval}")
        data = f.read(w * h * 3)
        if len(data) != w * h * 3:
            raise ValueError(f"{path}: truncated pixel data")
    return np.frombuffer(data, dtype=np.uint8).reshape(h, w, 3)


def rmse(a, b):
    d = a.astype(np.float64) - b.astype(np.float64)
    return math.sqrt(float((d * d).mean()))


def mae(a, b):
    d = a.astype(np.float64) - b.astype(np.float64)
    return float(np.abs(d).mean())


def _gaussian_kernel_1d(sigma, radius):
    x = np.arange(-radius, radius + 1, dtype=np.float64)
    k = np.exp(-(x * x) / (2.0 * sigma * sigma))
    return k / k.sum()


def _luminance(a):
    if a.ndim == 3:
        return a @ np.array([0.299, 0.587, 0.114])
    return a.astype(np.float64)


def ssim(a, b):
    """SSIM on luminance (ITU-R BT.601 weights), standard parameters
    (11x11 gaussian, sigma=1.5, K1=0.01, K2=0.03, L=255)."""
    assert a.shape == b.shape
    la = _luminance(a)
    lb = _luminance(b)

    sigma = 1.5
    radius = 5
    k = _gaussian_kernel_1d(sigma, radius)
    k2 = np.outer(k, k)

    def blur(x):
        # separable convolution with edge clamp (replicate)
        p = np.pad(x, radius, mode="edge")
        tmp = np.apply_along_axis(lambda r: np.convolve(r, k, mode="valid"), 0, p)
        out = np.apply_along_axis(lambda r: np.convolve(r, k, mode="valid"), 1, tmp)
        return out

    C1 = (0.01 * 255.0) ** 2
    C2 = (0.03 * 255.0) ** 2

    mu_a = blur(la)
    mu_b = blur(lb)
    mu_aa = mu_a * mu_a
    mu_bb = mu_b * mu_b
    mu_ab = mu_a * mu_b
    var_a = blur(la * la) - mu_aa
    var_b = blur(lb * lb) - mu_bb
    cov_ab = blur(la * lb) - mu_ab

    ssim_map = ((2.0 * mu_ab + C1) * (2.0 * cov_ab + C2)) / \
               ((mu_aa + mu_bb + C1) * (var_a + var_b + C2))
    return float(ssim_map.mean())


# ---------------------------------------------------------------------------
# CIELAB Delta E 2000 (Sharma 2005 formulation). Display-referred: sRGB
# bytes -> linear -> XYZ (D65) -> Lab, then the full dE00 formula.
# ---------------------------------------------------------------------------

_M_SRGB_XYZ = np.array([
    [0.4124564, 0.3575761, 0.1804375],
    [0.2126729, 0.7151522, 0.0721750],
    [0.0193339, 0.1191920, 0.9503041],
])
_WHITE_D65 = np.array([0.95047, 1.00000, 1.08883])


def _srgb8_to_lab(img):
    c = img.astype(np.float64) / 255.0
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    xyz = lin @ _M_SRGB_XYZ.T / _WHITE_D65

    delta = 6.0 / 29.0
    f = np.where(xyz > delta ** 3, np.cbrt(xyz), xyz / (3 * delta * delta) + 4.0 / 29.0)
    L = 116.0 * f[..., 1] - 16.0
    a = 500.0 * (f[..., 0] - f[..., 1])
    b = 200.0 * (f[..., 1] - f[..., 2])
    return np.stack([L, a, b], axis=-1)


def delta_e_2000(lab1, lab2):
    """Full CIE Delta E 2000 (vectorized, Sharma 2005 reference formulation)."""
    L1, a1, b1 = lab1[..., 0], lab1[..., 1], lab1[..., 2]
    L2, a2, b2 = lab2[..., 0], lab2[..., 1], lab2[..., 2]

    C1 = np.hypot(a1, b1)
    C2 = np.hypot(a2, b2)
    Cbar = 0.5 * (C1 + C2)
    c7 = Cbar ** 7
    G = 0.5 * (1.0 - np.sqrt(c7 / (c7 + 25.0 ** 7)))
    ap1 = (1.0 + G) * a1
    ap2 = (1.0 + G) * a2
    Cp1 = np.hypot(ap1, b1)
    Cp2 = np.hypot(ap2, b2)
    hp1 = np.degrees(np.arctan2(b1, ap1)) % 360.0
    hp2 = np.degrees(np.arctan2(b2, ap2)) % 360.0

    dLp = L2 - L1
    dCp = Cp2 - Cp1
    dhp = hp2 - hp1
    dhp = np.where(dhp > 180.0, dhp - 360.0, dhp)
    dhp = np.where(dhp < -180.0, dhp + 360.0, dhp)
    dhp = np.where((Cp1 * Cp2) == 0.0, 0.0, dhp)
    dHp = 2.0 * np.sqrt(Cp1 * Cp2) * np.sin(np.radians(dhp) / 2.0)

    Lbp = 0.5 * (L1 + L2)
    Cbp = 0.5 * (Cp1 + Cp2)
    hsum = hp1 + hp2
    hdiff = np.abs(hp1 - hp2)
    hmean = np.where(hdiff <= 180.0, hsum / 2.0,
                     np.where(hsum < 360.0, (hsum + 360.0) / 2.0, (hsum - 360.0) / 2.0))
    hmean = np.where((Cp1 * Cp2) == 0.0, hsum, hmean)

    T = (1.0 - 0.17 * np.cos(np.radians(hmean - 30.0))
         + 0.24 * np.cos(np.radians(2.0 * hmean))
         + 0.32 * np.cos(np.radians(3.0 * hmean + 6.0))
         - 0.20 * np.cos(np.radians(4.0 * hmean - 63.0)))
    dtheta = 30.0 * np.exp(-(((hmean - 275.0) / 25.0) ** 2))
    c7 = Cbp ** 7
    RC = 2.0 * np.sqrt(c7 / (c7 + 25.0 ** 7))
    SL = 1.0 + (0.015 * (Lbp - 50.0) ** 2) / np.sqrt(20.0 + (Lbp - 50.0) ** 2)
    SC = 1.0 + 0.045 * Cbp
    SH = 1.0 + 0.015 * Cbp * T
    RT = -np.sin(np.radians(2.0 * dtheta)) * RC

    return np.sqrt(
        (dLp / SL) ** 2 + (dCp / SC) ** 2 + (dHp / SH) ** 2
        + RT * (dCp / SC) * (dHp / SH))


def mean_delta_e_2000(a, b):
    lab1 = _srgb8_to_lab(a.astype(np.uint8))
    lab2 = _srgb8_to_lab(b.astype(np.uint8))
    return float(delta_e_2000(lab1, lab2).mean())


# ---------------------------------------------------------------------------
# Edge / shadow error: RMSE between Sobel gradient-magnitude maps of the
# luminance. Shadow placement and penumbra width are edge phenomena; two
# images can share a mean and still disagree completely here.
# ---------------------------------------------------------------------------

_SOBEL_X = np.array([[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]], dtype=np.float64)
_SOBEL_Y = np.array([[-1, -2, -1], [0, 0, 0], [1, 2, 1]], dtype=np.float64)


def _gradient_magnitude(lum):
    p = np.pad(lum, 1, mode="edge")
    gx = np.zeros_like(lum)
    gy = np.zeros_like(lum)
    # correlate (symmetric kernels; no flip needed)
    h, w = lum.shape
    for dy in range(3):
        for dx in range(3):
            wgt_x = _SOBEL_X[dy, dx]
            wgt_y = _SOBEL_Y[dy, dx]
            if wgt_x:
                gx += wgt_x * p[dy:dy + h, dx:dx + w]
            if wgt_y:
                gy += wgt_y * p[dy:dy + h, dx:dx + w]
    return np.hypot(gx, gy)


def edge_rmse(a, b):
    la = _gradient_magnitude(_luminance(a))
    lb = _gradient_magnitude(_luminance(b))
    d = la - lb
    return math.sqrt(float((d * d).mean()))


# ---------------------------------------------------------------------------
# Self test: reference values with published/derivable answers.
# ---------------------------------------------------------------------------

def self_test():
    failures = 0

    def check(name, got, want, tol):
        nonlocal failures
        ok = abs(got - want) <= tol
        failures += 0 if ok else 1
        print("  [%s] %-44s got %.6f want %.6f (tol %.4g)"
              % ("PASS" if ok else "FAIL", name, got, want, tol))

    # Sharma 2005 dE2000 test data (published expected values).
    pairs = [
        ((50.0000, 2.6772, -79.7751), (50.0000, 0.0000, -82.7485), 2.0425),
        ((50.0000, 3.1571, -77.2803), (50.0000, 0.0000, -82.7485), 2.8615),
        ((50.0000, 2.8361, -74.0200), (50.0000, 0.0000, -82.7485), 3.4412),
        ((50.0000, -1.3802, -84.2814), (50.0000, 0.0000, -82.7485), 1.0000),
        ((50.0000, 2.5000, 0.0000), (50.0000, 3.1736, 0.5854), 1.0000),
        ((50.0000, 2.5000, 0.0000), (73.0000, 25.0000, -18.0000), 27.1492),
        ((50.0000, 2.5000, 0.0000), (61.0000, -5.0000, 29.0000), 22.8977),
    ]
    for i, (c1, c2, want) in enumerate(pairs):
        lab1 = np.array(c1, dtype=np.float64)[None, None, :]
        lab2 = np.array(c2, dtype=np.float64)[None, None, :]
        check("sharma pair %d dE00" % (i + 1), float(delta_e_2000(lab1, lab2)[0, 0]), want, 2e-4)

    # Identity / inversion sanity on synthetic images.
    a = np.zeros((32, 32, 3), dtype=np.uint8)
    a[:16] = (200, 60, 30)
    check("identity rmse", rmse(a, a), 0.0, 1e-12)
    check("identity mae", mae(a, a), 0.0, 1e-12)
    check("identity ssim", ssim(a, a), 1.0, 1e-9)
    check("identity dE00", mean_delta_e_2000(a, a), 0.0, 1e-9)
    check("identity edge rmse", edge_rmse(a, a), 0.0, 1e-12)

    b = 255 - a
    # Inversion flips local covariance -> SSIM must be LOW (structure term
    # goes negative), even though both images are "flat + edge" shapes.
    inverted_ssim = ssim(a, b)
    check("inverted ssim is low", 1.0 if inverted_ssim < 0.5 else 0.0, 1.0, 1e-12)
    inverted_de = mean_delta_e_2000(a, b)
    check("inverted dE00 is large", 1.0 if inverted_de > 60.0 else 0.0, 1.0, 1e-12)

    # Edge detector: a step edge must produce a strong gradient exactly at
    # the step and (near) zero elsewhere.
    lum = np.zeros((16, 16), dtype=np.float64)
    lum[:, 8:] = 100.0
    gm = _gradient_magnitude(lum)
    check("edge magnitude at step", float(gm[:, 8].max()), 400.0, 1e-6)
    check("edge magnitude off step", float(gm[:, 2].max()), 0.0, 1e-9)

    print("  self-test: %d failure(s)" % failures)
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rendered", nargs="?", default=None, help="renderer frame (PPM)")
    ap.add_argument("reference", nargs="?", default=None, help="reference path trace (PPM)")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--self-test", action="store_true",
                    help="run internal reference-value checks and exit")
    args = ap.parse_args()

    if args.self_test:
        return 1 if self_test() else 0

    if args.rendered is None or args.reference is None:
        ap.error("rendered and reference PPM paths are required (or use --self-test)")

    a = read_ppm(args.rendered)
    b = read_ppm(args.reference)
    if a.shape != b.shape:
        print(f"ERROR: dimension mismatch: rendered {a.shape[1]}x{a.shape[0]} vs "
              f"reference {b.shape[1]}x{b.shape[0]}", file=sys.stderr)
        return 2

    r = rmse(a, b)
    l1 = mae(a, b)
    s = ssim(a, b)
    de = mean_delta_e_2000(a, b)
    er = edge_rmse(a, b)
    similarity = 100.0 * s  # ledger units: 0-100

    if args.json:
        print('{"rmse_255": %.4f, "mae_255": %.4f, "ssim": %.5f, "similarity": %.2f, '
              '"dE2000_mean": %.4f, "edge_rmse_255": %.4f}'
              % (r, l1, s, similarity, de, er))
    else:
        print("=== cornell similarity report ===")
        print(f"rendered : {args.rendered}")
        print(f"reference: {args.reference}")
        print(f"resolution: {a.shape[1]}x{a.shape[0]}")
        print(f"rmse (L2, 0-255): {r:.3f}")
        print(f"mae  (L1, 0-255): {l1:.3f}")
        print(f"ssim            : {s:.5f}")
        print(f"dE2000 mean     : {de:.3f}")
        print(f"edge/shadow rmse: {er:.3f}")
        print(f"Cornell similarity: {similarity:.2f} / 100")
        print("Record (similarity, fps) for this milestone in "
              "benchmarks/cornell_box/BASELINE.md.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
