#!/usr/bin/env python3
"""
Two-axis benchmark metric for the cornell-box/1.0 standard.

Compares a renderer frame (PPM, written by `sandbox --benchmark N --out ...`)
against the reference path trace produced by tools/reference_pathtracer.py,
and reports:

  - RMSE (per-channel, 0-255 scale): the raw disagreement,
  - SSIM (structural similarity, luminance, standard 11x11 gaussian window):
    the perceptual agreement used as the "realism axis" of the ledger.

Protocol ("Cornell similarity = X, FPS = Y"):
  1. sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 \
         --out frame.ppm --report frame.txt
  2. python3 tools/benchmark_compare.py frame.ppm \
         benchmarks/cornell_box/reference/cbox01_reference.ppm
  3. record (similarity, fps) for the milestone in
     benchmarks/cornell_box/BASELINE.md. The reference image NEVER changes;
     every renderer change moves X and/or Y.

Both images must have identical dimensions.
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


def _gaussian_kernel_1d(sigma, radius):
    x = np.arange(-radius, radius + 1, dtype=np.float64)
    k = np.exp(-(x * x) / (2.0 * sigma * sigma))
    return k / k.sum()


def ssim(a, b):
    """SSIM on luminance (ITU-R BT.601 weights), standard parameters
    (11x11 gaussian, sigma=1.5, K1=0.01, K2=0.03, L=255)."""
    assert a.shape == b.shape
    la = a @ np.array([0.299, 0.587, 0.114]) if a.ndim == 3 else a.astype(np.float64)
    lb = b @ np.array([0.299, 0.587, 0.114]) if b.ndim == 3 else b.astype(np.float64)

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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rendered", help="renderer frame (PPM)")
    ap.add_argument("reference", help="reference path trace (PPM)")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    args = ap.parse_args()

    a = read_ppm(args.rendered)
    b = read_ppm(args.reference)
    if a.shape != b.shape:
        print(f"ERROR: dimension mismatch: rendered {a.shape[1]}x{a.shape[0]} vs "
              f"reference {b.shape[1]}x{b.shape[0]}", file=sys.stderr)
        return 2

    r = rmse(a, b)
    s = ssim(a, b)
    similarity = 100.0 * s  # ledger units: 0-100

    if args.json:
        print(f'{{"rmse_255": {r:.4f}, "ssim": {s:.5f}, "similarity": {similarity:.2f}}}')
    else:
        print("=== cornell similarity report ===")
        print(f"rendered : {args.rendered}")
        print(f"reference: {args.reference}")
        print(f"resolution: {a.shape[1]}x{a.shape[0]}")
        print(f"rmse (0-255): {r:.3f}")
        print(f"ssim        : {s:.5f}")
        print(f"Cornell similarity: {similarity:.2f} / 100")
        print("Record (similarity, fps) for this milestone in "
              "benchmarks/cornell_box/BASELINE.md.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
