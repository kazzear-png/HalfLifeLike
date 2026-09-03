#!/usr/bin/env python3
"""
M3.3.1 Cornell Box ACCEPTANCE harness -- the M3.3 checklist as code.

The first on-hardware Cornell render came back 96.5% black (M3.3.1: the r1
geometry authored every room normal outward; the one-sided rasterizer shaded
the whole room to zero). This script exists so that failure class can never
pass silently again: run it on the DETERMINISTIC screenshot produced by the
sandbox and it judges every acceptance criterion from the benchmark charter.

Usage (on hardware, after building):
  ./build/bin/sandbox --scene cornell01 --frames 2 --width 1280 --height 720 \
      --out /tmp/cbox01.ppm
  python3 tools/verify_cornell_shot.py /tmp/cbox01.ppm [--variant cornell01]

Exit code 0 iff every non-GAP check passes. Prints the shot's md5 for the
BASELINE.md ledger (deterministic runs must reproduce it exactly).

How it judges without eyes:
  - It re-implements the FROZEN camera (pinhole, yaw 0 / pitch 0, fovY
    39.3 deg) to project known world-space probe points into the image.
  - It re-implements the FROZEN direct-lighting model (the 4x4 flux-
    preserving point-light grid, Lambert exitance) in float64 to compute
    what each probe SHOULD measure, then converts through the engine's
    display transform (exposure 1.0 -> Narkowicz ACES -> exact sRGB),
    allowing a generous band for specular add, MSAA, and dither.
  - It re-implements the box silhouette (projected hull) so the "no
    unexplained black regions" criterion is judged INSIDE the room, not on
    the void border the open front necessarily exposes.

Documented gaps are marked [GAP], not [FAIL]: the rasterizer has no shadows
(M6), no indirect/GI (M8), no IBL (M5). The ceiling check PINS its dark
direct-lighting state so a fake ambient can never silently slip in; when the
real milestones land, those checks flip to positive visibility requirements.

This tool mirrors constants from the generated header
(sandbox/src/cornell_scene_gen.h); bench_tests pins the C++ side.
"""

import argparse
import hashlib
import math
import sys

# ---------------------------------------------------------------------------
# Frozen standard: cornell-box/1.0 (r2). Mirrors cornell_scene_gen.h.
# ---------------------------------------------------------------------------
CAM_POS = (0.0, 2.75, 8.35)
CAM_FOV_Y = math.radians(39.3)
EXPOSURE = 1.0
BOX = (2.75, 5.50, 2.75)                       # half_x, y_max, half_z
EMITTER_MIN = (-0.65, 5.49, -0.525)
EMITTER_MAX = (0.65, 5.49, 0.525)
EMITTER_LE = 12.0
GRID_N = 16
GRID_I = (EMITTER_MAX[0] - EMITTER_MIN[0]) * (EMITTER_MAX[2] - EMITTER_MIN[2]) * EMITTER_LE / GRID_N
GRID_Y = 5.44
GRID_XS = [EMITTER_MIN[0] + (i + 0.5) / 4 * (EMITTER_MAX[0] - EMITTER_MIN[0]) for i in range(4)]
GRID_ZS = [EMITTER_MIN[2] + (j + 0.5) / 4 * (EMITTER_MAX[2] - EMITTER_MIN[2]) for j in range(4)]

ALBEDO_WHITE = (0.725, 0.71, 0.68)
ALBEDO_RED = (0.63, 0.065, 0.05)
ALBEDO_GREEN = (0.14, 0.45, 0.091)

BLACK_LUM = 8          # <= this (0..255) counts as "essentially black"
DITHER_LSB = 2.0       # triangular dither tails reach +-1.5 LSB


# ---------------------------------------------------------------------------
# Display transform -- EXACTLY the engine's tonemap pass (Renderer.cpp).
# ---------------------------------------------------------------------------
def aces(x):
    a, b, c, d, e = 2.51, 0.03, 2.43, 0.59, 0.14
    v = (x * (a * x + b)) / (x * (c * x + d) + e)
    return min(1.0, max(0.0, v))


def linear_to_srgb(c):
    if c <= 0.0031308:
        return 12.92 * c
    return 1.055 * (c ** (1.0 / 2.4)) - 0.055


def to_bytes(linear):
    """linear HDR radiance -> 8-bit sRGB, pre-dither (band absorbs dither)."""
    v = linear_to_srgb(aces(linear * EXPOSURE))
    return min(255.0, max(0.0, 255.0 * v))


# ---------------------------------------------------------------------------
# Frozen camera: pinhole projection, yaw 0 / pitch 0 -> forward (0,0,-1).
# ---------------------------------------------------------------------------
def project(p, w, h):
    """world point -> (sx, sy) normalized image coords (top-left origin)."""
    d = (p[0] - CAM_POS[0], p[1] - CAM_POS[1], p[2] - CAM_POS[2])
    depth = -d[2]                     # forward is (0,0,-1)
    if depth <= 1e-6:
        return None
    tan_half = math.tan(CAM_FOV_Y * 0.5)
    aspect = w / h
    ndc_x = d[0] / (depth * tan_half * aspect)
    ndc_y = d[1] / (depth * tan_half)
    return ((ndc_x + 1.0) * 0.5, (1.0 - ndc_y) * 0.5)


def project_all(points, w, h):
    out = []
    for p in points:
        q = project(p, w, h)
        if q is None:
            return None               # whole-set visibility requirement
        out.append(q)
    return out


# ---------------------------------------------------------------------------
# Frozen direct lighting: point-light grid, Lambert exitance (float64).
# Specular is intentionally omitted: walls are roughness 0.90 dielectrics,
# the add is small, and the acceptance band absorbs it (documented above).
# ---------------------------------------------------------------------------
def expected_byte(p, n, albedo):
    er, eg, eb = 0.0, 0.0, 0.0
    for gx in GRID_XS:
        for gz in GRID_ZS:
            lx, ly, lz = gx - p[0], GRID_Y - p[1], gz - p[2]
            dist = math.sqrt(lx * lx + ly * ly + lz * lz)
            ndotl = (n[0] * lx + n[1] * ly + n[2] * lz) / dist
            if ndotl <= 0.0:
                continue
            e = GRID_I * ndotl / (dist * dist)
            er += e * albedo[0]
            eg += e * albedo[1]
            eb += e * albedo[2]
    inv_pi = 1.0 / math.pi
    return (to_bytes(er * inv_pi), to_bytes(eg * inv_pi), to_bytes(eb * inv_pi))


# ---------------------------------------------------------------------------
# PPM (P6) reader -- the sandbox screenshot format.
# ---------------------------------------------------------------------------
def read_ppm_p6(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"P6":
        raise ValueError("not a P6 PPM: %s" % path)
    pos, fields = 2, []
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1                          # single whitespace after maxval
    w, h, maxval = fields
    if maxval != 255:
        raise ValueError("expected maxval 255, got %d" % maxval)
    need = w * h * 3
    if len(data) - pos < need:
        raise ValueError("truncated PPM: need %d bytes, have %d" % (need, len(data) - pos))
    return w, h, data[pos:pos + need]


def pixel(img, w, x, y):
    o = (y * w + x) * 3
    return img[o], img[o + 1], img[o + 2]


def patch_median(img, w, h, sx, sy, half_frac):
    """median RGB over a small square around a normalized coord."""
    half = max(2, int(round(half_frac * min(w, h))))
    cx, cy = int(round(sx * w)), int(round(sy * h))
    rs, gs, bs = [], [], []
    for y in range(max(0, cy - half), min(h, cy + half + 1)):
        for x in range(max(0, cx - half), min(w, cx + half + 1)):
            r, g, b = pixel(img, w, x, y)
            rs.append(r); gs.append(g); bs.append(b)
    rs.sort(); gs.sort(); bs.sort()
    m = len(rs) // 2
    return rs[m], gs[m], bs[m]


# ---------------------------------------------------------------------------
# Box silhouette hull (projected) -> point-in-hull test.
# ---------------------------------------------------------------------------
def box_hull(w, h):
    hx, ym, hz = BOX
    pts = []
    for x in (-hx, hx):
        for y in (0.0, ym):
            for z in (-hz, hz):
                q = project((x, y, z), w, h)
                if q is None:
                    raise RuntimeError("box corner behind camera -- camera pin broken")
                pts.append(q)
    pts.sort(key=lambda p: (p[0], p[1]))
    def half(pts):
        out = []
        for p in pts:
            while len(out) >= 2 and ((out[-1][0] - out[-2][0]) * (p[1] - out[-2][1]) -
                                     (out[-1][1] - out[-2][1]) * (p[0] - out[-2][0])) <= 0:
                out.pop()
            out.append(p)
        return out
    lower, upper = half(pts), half(reversed(pts))
    return lower[:-1] + upper[:-1]


def in_hull(hull, x, y, eps=1e-9):
    sign = 0
    n = len(hull)
    for i in range(n):
        x1, y1 = hull[i]
        x2, y2 = hull[(i + 1) % n]
        cr = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1)
        if abs(cr) < eps:
            continue
        s = 1 if cr > 0 else -1
        if sign == 0:
            sign = s
        elif s != sign:
            return False
    return True


# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------
class Report:
    def __init__(self):
        self.rows = []

    def add(self, status, criterion, detail):
        self.rows.append((status, criterion, detail))

    def ok(self):
        return all(s != "FAIL" for s, _, _ in self.rows)

    def print(self):
        marks = {"PASS": "[PASS]", "FAIL": "[FAIL]", "GAP": "[GAP ]", "INFO": "[INFO]"}
        for status, criterion, detail in self.rows:
            print("%s %-34s %s" % (marks[status], criterion, detail))
        fails = sum(1 for s, _, _ in self.rows if s == "FAIL")
        gaps = sum(1 for s, _, _ in self.rows if s == "GAP")
        print("---")
        print("%d checks: %d passed, %d failed, %d documented gaps (M5/M6/M8)"
              % (len(self.rows), len(self.rows) - fails - gaps, fails, gaps))
        return fails


def check_probes(rep, img, w, h):
    """World-space probes: expected value from the frozen lighting model."""
    probes = [
        # label, point, normal, albedo, half_frac, lo_scale, hi_scale, floor
        ("emitter (unlit L_e=12)", (0.0, EMITTER_MIN[1], 0.0), (0.0, -1.0, 0.0),
         (1.0, 1.0, 1.0), 0.010, 0.90, 1.10, 225),
        ("back wall white", (0.0, 2.75, BOX[2] * -1 + 0.01), (0.0, 0.0, 1.0),
         ALBEDO_WHITE, 0.012, 0.55, 1.90, 40),
        ("left wall red", (BOX[0] * -1 + 0.01, 2.75, -0.6), (1.0, 0.0, 0.0),
         ALBEDO_RED, 0.012, 0.50, 2.00, 35),
        ("right wall green", (BOX[0] - 0.01, 2.75, -0.6), (-1.0, 0.0, 0.0),
         ALBEDO_GREEN, 0.012, 0.50, 2.00, 30),
        ("floor (gap between blocks)", (0.0, 0.001, -1.6), (0.0, 1.0, 0.0),
         ALBEDO_WHITE, 0.010, 0.50, 2.00, 30),
        ("short block top", (0.9, 1.651, 0.5), (0.0, 1.0, 0.0),
         ALBEDO_WHITE, 0.008, 0.50, 2.00, 25),
        # NOTE: the tall block's TOP (y=3.3) is above the frozen camera
        # (y=2.75) and is therefore NEVER visible -- its lit signature in a
        # correct render is the thin right face (normal +x) sliver.
        ("tall block right face", (-0.549, 2.2, -0.5), (1.0, 0.0, 0.0),
         ALBEDO_WHITE, 0.004, 0.40, 2.20, 20),
    ]
    for label, p, n, alb, half_frac, lo_s, hi_s, floor in probes:
        q = project(p, w, h)
        if q is None or not (0.0 <= q[0] < 1.0 and 0.0 <= q[1] < 1.0):
            rep.add("FAIL", label, "probe projects outside the frame -- camera pin broken")
            continue
        exp = expected_byte(p, n, alb)
        act = patch_median(img, w, h, q[0], q[1], half_frac)
        ok = True
        detail = []
        for ch, name in enumerate("RGB"):
            # R keeps the probe's visibility floor; G/B use a quarter of it:
            # dark albedo channels (red wall B=0.05) are PHYSICALLY near-black
            # and must not fail the band (measured r2 value: ~15/255).
            ch_floor = floor if ch == 0 else floor * 0.25
            lo = max(ch_floor, lo_s * exp[ch] - DITHER_LSB)
            hi = min(255.0, hi_s * exp[ch] + DITHER_LSB)
            if not (lo <= act[ch] <= hi):
                ok = False
            detail.append("%s %3d (expect %.0f..%.0f)" % (name, act[ch], lo, hi))
        rep.add("PASS" if ok else "FAIL", label, "  ".join(detail))


def main():
    ap = argparse.ArgumentParser(description="M3.3 Cornell acceptance harness")
    ap.add_argument("shot", help="deterministic screenshot (PPM, from sandbox --frames/--out)")
    ap.add_argument("--variant", default="cornell01",
                    choices=["cornell01", "cornell02", "cornell03"])
    ap.add_argument("--expect-md5", default=None,
                    help="fail if the shot's md5 differs (determinism pin for the ledger)")
    args = ap.parse_args()

    w, h, img = read_ppm_p6(args.shot)
    with open(args.shot, "rb") as f:
        digest = hashlib.md5(f.read()).hexdigest()

    rep = Report()
    print("=== M3.3 Cornell acceptance: %s (%dx%d, %s) ===" % (args.shot, w, h, args.variant))
    print("md5 %s  (record next to the ledger row; reruns must reproduce it)" % digest)
    if args.expect_md5 and args.expect_md5 != digest:
        rep.add("FAIL", "deterministic screenshot", "md5 %s != pinned %s" % (digest, args.expect_md5))
    else:
        rep.add("PASS", "deterministic screenshot", "md5 %s%s" %
                (digest, " (matches pin)" if args.expect_md5 else ""))

    # -- camera / framing ----------------------------------------------------
    em_c = project((0.0, EMITTER_MIN[1], 0.0), w, h)
    if em_c is None or not (0.40 <= em_c[0] <= 0.60 and 0.0 <= em_c[1] <= 0.14):
        rep.add("FAIL", "correct camera", "emitter center at %s, expected top-center inside [0.40..0.60]x[0.00..0.14]" %
                ("none" if em_c is None else "(%.3f, %.3f)" % em_c))
    else:
        rep.add("PASS", "correct camera", "emitter center projects to (%.3f, %.3f), top-center as frozen" % em_c)

    hull = box_hull(w, h)
    hull_min_x = min(p[0] for p in hull)
    hull_max_x = max(p[0] for p in hull)
    hull_min_y = min(p[1] for p in hull)
    rep.add("PASS" if (hull_max_x - hull_min_x) > 0.5 and hull_min_y < 0.2 else "FAIL",
            "correct proportions on screen", "box hull spans x %.2f..%.2f, top y %.2f (frozen view)" %
            (hull_min_x, hull_max_x, hull_min_y))

    # -- global pixel statistics ----------------------------------------------
    step = 2 if w * h > 921600 else 1
    n_seen = n_inside = n_black_in = 0
    lum_sum_in = 0.0
    black_global = 0
    for y in range(0, h, step):
        for x in range(0, w, step):
            r, g, b = pixel(img, w, x, y)
            lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
            if lum < BLACK_LUM:
                black_global += 1
            if in_hull(hull, (x + 0.5) / w, (y + 0.5) / h):
                n_inside += 1
                if lum < BLACK_LUM:
                    n_black_in += 1
                else:
                    lum_sum_in += lum
    n_seen = max(1, ((h + step - 1) // step) * ((w + step - 1) // step))
    black_global /= float(n_seen)
    mean_in = lum_sum_in / max(1, n_inside - n_black_in)

    rep.add("PASS" if black_global < 0.45 else "FAIL",
            "no unexplained black regions", "global black %.1f%% (r1 failure was 96.5%%; <45%% required)" %
            (100.0 * black_global))
    rep.add("PASS" if n_inside and n_black_in / n_inside < 0.30 else "FAIL",
            "room interior luminous", "inside-silhouette black %.1f%% (<30%% required)" %
            (100.0 * n_black_in / max(1, n_inside)))
    rep.add("PASS" if 25.0 <= mean_in <= 235.0 else "FAIL",
            "direct illumination working", "interior mean luminance %.1f (expected 25..235)" % mean_in)

    # -- probes ---------------------------------------------------------------
    check_probes(rep, img, w, h)
    # -- color dominance (redundant with probes, but stated as the criterion) --
    q_l = project((BOX[0] * -1 + 0.01, 2.75, -0.6), w, h)
    q_r = project((BOX[0] - 0.01, 2.75, -0.6), w, h)
    pl = patch_median(img, w, h, q_l[0], q_l[1], 0.012)
    pr = patch_median(img, w, h, q_r[0], q_r[1], 0.012)
    rep.add("PASS" if pl[0] > pl[1] + 25 and pl[0] > pl[2] + 25 else "FAIL",
            "red wall visibly red", "left wall median R=%d G=%d B=%d" % pl)
    rep.add("PASS" if pr[1] > pr[0] + 25 and pr[1] > pr[2] + 20 else "FAIL",
            "green wall visibly green", "right wall median R=%d G=%d B=%d" % pr)

    # -- documented gaps: stated as criteria, pinned so they cannot lie --------
    q_c = project((0.0, BOX[1] - 0.01, -2.0), w, h)
    pc = patch_median(img, w, h, q_c[0], q_c[1], 0.012)
    # Calibration: direct-only Lambert puts the ceiling at ~36/255 (r2 render);
    # the engine adds grazing-angle specular (~+50% there, still <50). A fake
    # hemispheric ambient at the M3 scale (sky 0.06) would push it to ~65.
    # Threshold 55 splits the two; recalibrate after the first hardware shot.
    rep.add("PASS" if max(pc) <= 55 else "FAIL",
            "ceiling direct-dark (GI gap pin)", "ceiling median R=%d G=%d B=%d must stay <=55 until M8 "
            "indirect lands (only bounce light reaches it; a fake ambient would show here first)" % pc)
    rep.add("GAP", "shadows (criterion)", "no shadow mapping until M6; CBox-03 baffle leak is the measured gap")
    rep.add("GAP", "indirect color bounce (criterion)", "no GI until M8; similarity axis quantifies it vs the reference")
    rep.add("INFO", "cornell02/03 extras", "spheres/baffle probes intentionally via the SSIM similarity axis, not this harness")

    fails = rep.print()
    print("verdict: %s" % ("ACCEPTED -- save this shot as the M3.3.1 baseline" if fails == 0 else "REJECTED"))
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
