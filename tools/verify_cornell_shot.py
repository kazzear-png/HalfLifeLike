#!/usr/bin/env python3
"""
M4 Cornell Box ACCEPTANCE harness -- the M3.3 checklist as code.

The first on-hardware Cornell render came back 96.5% black (M3.3.1: the r1
geometry authored every room normal outward; the one-sided rasterizer shaded
the whole room to zero). This script exists so that failure class can never
pass silently again: run it on the DETERMINISTIC screenshot produced by the
sandbox and it judges every acceptance criterion from the benchmark charter.

M4 addition: the frozen lighting model now includes the HEIGHTFIELD SHADOW
MARCH (the exact algorithm the PBR shader runs, in float64), so probe
expectations account for occlusion and the harness makes its first POSITIVE
shadow checks: a full-umbra probe (all 16 grid lights provably blocked) and
a penumbra probe (partially blocked, expected value computed exactly).

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
    preserving point-light grid, Lambert exitance) + the heightfield shadow
    march in float64 to compute what each probe SHOULD measure, then
    converts through the engine's display transform (exposure 1.0 ->
    Narkowicz ACES -> exact sRGB), allowing a generous band for specular
    add, MSAA, and dither.
  - It re-implements the box silhouette (projected hull) so the "no
    unexplained black regions" criterion is judged INSIDE the room, not on
    the void border the open front necessarily exposes.

Documented gaps are marked [GAP], not [FAIL]: the rasterizer still has no
indirect/GI (M8) and no IBL (M5). The ceiling check PINS its dark
direct-lighting state so a fake ambient can never silently slip in; when
the real milestones land, those checks flip to positive visibility
requirements.

This tool mirrors constants from the generated header
(sandbox/src/cornell_scene_gen.h) and tools/generate_cornell.py; bench_tests
pins the C++ side.
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

# ---------------------------------------------------------------------------
# Occluder geometry (mirrors tools/generate_cornell.py r2) + the heightfield
# shadow march (mirrors sandbox/src/shaders.h shadowVisibility / engine
# constants in sandbox/src/main.cpp).
# ---------------------------------------------------------------------------
TALL_BLOCK = ((-1.55, -0.55), (-2.05, 0.35), 0.0, 3.30)    # (x0,x1), (z0,z1), ymin, ymax
SHORT_BLOCK = ((0.40, 1.40), (-0.70, 1.70), 0.0, 1.65)
BAFFLE = ((-1.00, 1.00), (-0.10, 0.10), 3.40, 5.40)        # hanging plate
SPHERES = (  # (cx, cz, r, floor_y) -- column interval is exact for convex solids
    (-1.70, 1.55, 0.55, 0.0),   # mirror
    (0.00, 0.90, 0.55, 0.0),    # gold
    (1.70, 1.55, 0.55, 0.0),    # dielectric
)
MARCH_STEP = 0.08        # sandbox per-light default (kMarchStep, M4.0.6)
MARCH_STEP_CENTROID = 0.04  # sandbox centroid default (kCentroidMarchStep, M4.0.9)
MARCH_BIAS = 0.01
PENUMBRA = 0.1625        # kShadowPenumbra (M4.0.9 default = half grid pitch;
                         # re-derived after the hardware A/B matrix -- the
                         # full-pitch first default double-counted the 4x4
                         # grid's own parallax; --shadow-penumbra overrides)
LIGHT_SIZE = 1.175       # kShadowLightSize (M4.0.9 centroid half-plane S, emitter mean)
EMITTER_HALF = (0.65, 0.525)  # the frozen patch's half-extents (M4.0.9.1
                              # lateral support; EMITTER_MIN/MAX / 2)
LATERAL_DEFAULT = 1      # kShadowLateralDefault (M4.0.9.1 analytic lateral
                         # half-plane; 0 = exact M4.0.9 centroid march)
AREA_LIGHT_DEFAULT = 1   # kAreaLightDefault (M5.0 true area-light transport;
                         # 0 = the exact M4.0.9.1 grid transport, all pins)
LE = 12.0                # kEmitterRadiance (frozen standard)

# M5.0 area-model constants (mirror the shader / AreaLight.h exactly).
AREA_FAR_CLAMP = 50.0    # sub-horizontal generator clamp (the room is 5.5 m)
AREA_RING = 32           # sphere cone boundary directions (+1 vertex sample)

# M4.0.9 shadow-mode arguments (must mirror the sandbox flags used for the
# shot; the defaults track the sandbox defaults). The float64 march below is
# the EXACT algorithm the PBR shader runs, so the probe expectations stay
# honest under every mode.
class ShadowMode:
    def __init__(self, centroid=False, light_size=LIGHT_SIZE,
                 step=None, penumbra=PENUMBRA, lateral=LATERAL_DEFAULT,
                 area=AREA_LIGHT_DEFAULT):
        self.centroid = centroid
        self.light_size = light_size
        self.step = step if step is not None else \
            (MARCH_STEP_CENTROID if centroid else MARCH_STEP)
        self.penumbra = 0.0 if centroid and light_size <= 0.0 else penumbra
        self.lateral = lateral
        self.area = area
MAXH_BOXES = 3.30
MAXH_BAFFLE = 5.40


def solid_intervals(variant):
    """Occluder column intervals [(ymin_fn, ymax_fn), ...] per variant."""
    def box_interval(xr, zr, y0, y1):
        return ((lambda x, z, a=xr, b=zr, lo=y0: lo if a[0] <= x <= a[1] and b[0] <= z <= b[1] else 1.0),
                (lambda x, z, a=xr, b=zr, hi=y1: hi if a[0] <= x <= a[1] and b[0] <= z <= b[1] else 0.0))
    def sphere_interval(cx, cz, r, floor_y):
        def lo(x, z):
            d2 = (x - cx) ** 2 + (z - cz) ** 2
            return (floor_y + r - math.sqrt(r * r - d2)) if d2 < r * r else 1.0
        def hi(x, z):
            d2 = (x - cx) ** 2 + (z - cz) ** 2
            return (floor_y + r + math.sqrt(r * r - d2)) if d2 < r * r else 0.0
        return (lo, hi)
    solids = [box_interval(*TALL_BLOCK), box_interval(*SHORT_BLOCK)]
    if variant == "cornell02":
        solids += [sphere_interval(*s) for s in SPHERES]
    if variant == "cornell03":
        solids += [box_interval(*BAFFLE)]
    return solids


def occluder_primitives(variant):
    """M4.0.9.1: the analytic occluder set for the lateral half-plane grade --
    the SAME solids solid_intervals() rasterizes, as primitive data:
    ("box", (minX, minZ, minH, maxX, maxZ, maxH)) or
    ("sphere", (cx, cy, cz, r)). Mirrors the sandbox's OccluderSet (built
    from the loaded meshes' AABBs; walls are not occluder materials)."""
    def box_prim(geom):
        (x0, x1), (z0, z1), y0, y1 = geom
        return ("box", (x0, z0, y0, x1, z1, y1))
    prims = [box_prim(TALL_BLOCK), box_prim(SHORT_BLOCK)]
    if variant == "cornell02":
        for cx, cz, r, fy in SPHERES:
            prims.append(("sphere", (cx, fy + r, cz, r)))
    if variant == "cornell03":
        prims.append(box_prim(BAFFLE))
    return prims


def march_visibility(px, py, pz, lx, ly, lz, solids, max_h,
                     step=MARCH_STEP, bias=MARCH_BIAS, penumbra=PENUMBRA,
                     refine=True):
    """float64 port of the shader's shadowVisibility (M4.0.9 form):
    parallax window penumbra * t / (1 - t) capped at its occluder-bearing
    value, march span tCap (binary) / tEnd (soft), 2*step start zone,
    M4.0.8 bracket refinement. penumbra = 0 reduces to the exact binary
    march (the M4.0.6 regression form)."""
    dx, dz = lx - px, lz - pz
    horiz = math.sqrt(dx * dx + dz * dz)
    if horiz < 1e-4:
        return 1.0
    if ly > py:
        t_cap = (max_h - bias - py) / (ly - py)
        t_cap = min(1.0, max(0.0, t_cap))
    elif py > max_h - bias:
        t_cap = 0.0
    else:
        t_cap = 1.0
    t_end = t_cap
    if penumbra > 0.0:
        w_cap = penumbra * t_cap / max(1.0 - t_cap, 1e-4)
        dy = ly - py
        t_end = min(t_cap + w_cap / dy, 1.0) if dy > 0.0 else 1.0
    t_cap_ratio = t_cap / max(1.0 - t_cap, 1e-4)
    steps = int(horiz / step) + 1
    seg_len = math.sqrt(dx * dx + dz * dz + (ly - py) * (ly - py))
    vis, t_best = 1.0, -1.0
    for s in range(1, steps + 1):
        t = s / steps
        if t > t_end:
            break
        ray_y = py + (ly - py) * t
        x, z = px + dx * t, pz + dz * t
        d = 1e30
        for lo_fn, hi_fn in solids:
            h_max, h_min = hi_fn(x, z), lo_fn(x, z)
            d = min(d, max(h_min + bias - ray_y, ray_y - (h_max - bias)))
        if d < 0.0:
            return 0.0
        if penumbra > 0.0:
            traveled = t * seg_len
            if traveled > 2.0 * step:
                window = penumbra * min(t / max(1.0 - t, 1e-4), t_cap_ratio)
                graded = min(1.0, max(0.0, d / max(window, 1e-5)))
                if graded < vis:
                    vis, t_best = graded, t
    if penumbra > 0.0 and refine and vis < 1.0 and t_best >= 0.0:
        half_t = 0.5 / steps
        for r in range(2):
            m = t_best + (-half_t if r == 0 else half_t)
            if m <= 0.0 or m > t_end:
                continue
            ray_yr = py + (ly - py) * m
            xr, zr = px + dx * m, pz + dz * m
            d = 1e30
            for lo_fn, hi_fn in solids:
                h_max, h_min = hi_fn(xr, zr), lo_fn(xr, zr)
                d = min(d, max(h_min + bias - ray_yr,
                               ray_yr - (h_max - bias)))
            if d < 0.0:
                return 0.0
            if m * seg_len > 2.0 * step:
                window_r = penumbra * min(m / max(1.0 - m, 1e-4),
                                          t_cap_ratio)
                vis = min(vis, min(1.0, max(0.0, d / max(window_r, 1e-5))))
    return vis


def march_visibility_centroid(px, py, pz, cx, cy, cz, solids, max_h,
                              step=MARCH_STEP_CENTROID, bias=MARCH_BIAS,
                              light_size=LIGHT_SIZE, refine=True,
                              lateral=LATERAL_DEFAULT, prims=(),
                              emitter_half=EMITTER_HALF):
    """float64 port of the shader's shadowVisibilityCentroid (M4.0.9 + the
    M4.0.9.1 lateral half-plane): ONE march to the emitter centroid shared
    by the rig, graded per sample by the half-plane area model
    g = clamp(0.5 + d/(S*t), 0, 1), min-combined with the lateral ramp
    g_lat = clamp(0.5 + r/(E_perp*min(t, tCap)), 0, 1) where r is the signed
    ground distance to the nearest ELIGIBLE primitive (height interval
    mirrors the texture column semantics; lateral = 0 is the exact M4.0.9
    march)."""
    dx, dz = cx - px, cz - pz
    horiz = math.sqrt(dx * dx + dz * dz)
    if horiz < 1e-4:
        d_v = -1e30
        for lo_fn, hi_fn in solids:
            h_max, h_min = hi_fn(px, pz), lo_fn(px, pz)
            d_v = max(d_v, max(h_min + bias - max(py, cy),
                               min(py, cy) - (h_max - bias)))
        return 0.0 if d_v < 0.0 else 1.0
    # M4.0.9.1: the emitter's exact lateral support along this trace.
    perp_x, perp_z = -dz / horiz, dx / horiz
    e_perp = 2.0 * (emitter_half[0] * abs(perp_x) +
                    emitter_half[1] * abs(perp_z))
    lateral_active = bool(lateral) and light_size > 0.0
    if cy > py:
        t_cap = (max_h - bias - py) / (cy - py)
        t_cap = min(1.0, max(0.0, t_cap))
    elif py > max_h - bias:
        t_cap = 0.0
    else:
        t_cap = 1.0
    t_end = t_cap
    if light_size > 0.0:
        dy = cy - py
        t_end = min(t_cap + light_size * t_cap / dy, 1.0) if dy > 0.0 else 1.0
    steps = int(horiz / step) + 1
    seg_len = math.sqrt(dx * dx + dz * dz + (cy - py) * (cy - py))
    vis, t_best = 1.0, -1.0
    for s in range(1, steps + 1):
        t = s / steps
        if t <= 0.0 or t > t_end:
            break
        ray_y = py + (cy - py) * t
        x, z = px + dx * t, pz + dz * t
        d = 1e30
        solid = False
        for lo_fn, hi_fn in solids:
            h_max, h_min = hi_fn(x, z), lo_fn(x, z)
            solid = solid or (h_max > h_min)
            d = min(d, max(h_min + bias - ray_y, ray_y - (h_max - bias)))
        if light_size <= 0.0:
            if d < 0.0:
                return 0.0
            continue
        # M4.0.9.1 early-out: the vertical bound alone is exact only once
        # the ray has cleared the global top (b_bound >= 0) -- before that
        # a future sample can still be occluder-eligible and the lateral
        # grade (a ground distance) can still fall below the bound.
        b_bound = ray_y - (max_h - bias)
        if cy > py and (not lateral_active or b_bound >= 0.0) and \
                0.5 + b_bound / max(light_size * t, 1e-5) >= vis:
            break
        traveled = t * seg_len
        if traveled > 2.0 * step:
            g = 1.0
            if solid:
                g = 0.5 + d / max(light_size * min(t, t_cap), 1e-5)
            if lateral_active:
                # M4.0.9.1 lateral half-plane: signed ground distance to
                # the nearest ELIGIBLE primitive, ramped by the emitter's
                # lateral support (mirrors the shader exactly).
                r_lat = 1e3
                for prim in prims:
                    if prim[0] == "box":
                        bx0, bz0, bh0, bx1, bz1, bh1 = prim[1]
                        if ray_y <= bh0 + bias or ray_y >= bh1 - bias:
                            continue
                        dvx = max(bx0 - x, x - bx1)
                        dvz = max(bz0 - z, z - bz1)
                        outside = math.sqrt(max(dvx, 0.0) ** 2 +
                                            max(dvz, 0.0) ** 2)
                        inside = min(max(dvx, dvz), 0.0)
                        r_lat = min(r_lat, outside + inside)
                    else:
                        scx, scy, scz, sr = prim[1]
                        h_off = abs(ray_y - scy) + bias
                        rho2 = sr * sr - h_off * h_off
                        if rho2 <= 0.0:
                            continue
                        r_lat = min(r_lat,
                                    math.sqrt((x - scx) ** 2 + (z - scz) ** 2)
                                    - math.sqrt(rho2))
                g_lat = min(1.0, max(0.0,
                            0.5 + r_lat / max(e_perp * min(t, t_cap), 1e-5)))
                g = min(g, g_lat)
            g = min(1.0, max(0.0, g))
            if g < vis:
                vis, t_best = g, t
            if vis <= 0.0:
                break   # min cannot go lower (value-neutral)
    if light_size > 0.0 and refine and 0.0 < vis < 1.0 and t_best >= 0.0:
        half_t = 0.5 / steps
        for r in range(2):
            m = t_best + (-half_t if r == 0 else half_t)
            if m <= 0.0 or m > t_end:
                continue
            ray_yr = py + (cy - py) * m
            xr, zr = px + dx * m, pz + dz * m
            d = 1e30
            solid = False
            for lo_fn, hi_fn in solids:
                h_max, h_min = hi_fn(xr, zr), lo_fn(xr, zr)
                solid = solid or (h_max > h_min)
                d = min(d, max(h_min + bias - ray_yr,
                               ray_yr - (h_max - bias)))
            if m * seg_len > 2.0 * step and solid:
                g = min(1.0, max(0.0,
                        0.5 + d / max(light_size * min(m, t_cap), 1e-5)))
                vis = min(vis, g)
    return vis

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
# M5.0 area-light transport model (float64 port of the shader's area path,
# which mirrors rendering/AreaLight.h; the whole chain is validated against
# brute force by scripts/check_area_model.py -- end-to-end worst 0.9% of K on
# the frozen configs, exact for boxes and multi-blocker unions).
# ---------------------------------------------------------------------------
def area_arvo(px, py, pz, n, poly):
    """K = -1/2 * sum gamma_i (N . m_i) over the polygon's (x, z) vertices
    on the emitter plane (Arvo; exact -- check_area_model.py section 1)."""
    if len(poly) < 3:
        return 0.0
    ly = EMITTER_MIN[1]
    s = 0.0
    m = len(poly)
    for i in range(m):
        ax, az = poly[i]
        bx, bz = poly[(i + 1) % m]
        ux, uy, uz = ax - px, ly - py, az - pz
        vx, vy, vz = bx - px, ly - py, bz - pz
        ul = math.sqrt(ux * ux + uy * uy + uz * uz)
        vl = math.sqrt(vx * vx + vy * vy + vz * vz)
        if ul < 1e-12 or vl < 1e-12:
            continue
        a = (ux / ul, uy / ul, uz / ul)
        b = (vx / vl, vy / vl, vz / vl)
        cr = (a[1] * b[2] - a[2] * b[1],
              a[2] * b[0] - a[0] * b[2],
              a[0] * b[1] - a[1] * b[0])
        ln = math.sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2])
        if ln < 1e-12:
            continue
        ca = max(-1.0, min(1.0, a[0] * b[0] + a[1] * b[1] + a[2] * b[2]))
        s += math.acos(ca) * (n[0] * cr[0] + n[1] * cr[1] + n[2] * cr[2]) / ln
    return -0.5 * s


def area_hull2d(pts):
    """Monotone chain convex hull over (x, z) tuples (CCW, no dup last)."""
    pts = sorted(set(pts))
    if len(pts) <= 2:
        return pts
    def half(ps):
        out = []
        for p in ps:
            while len(out) >= 2:
                (ox, oz), (ax, az) = out[-2], out[-1]
                if (ax - ox) * (p[1] - oz) - (az - oz) * (p[0] - ox) <= 1e-15:
                    out.pop()
                else:
                    break
            out.append(p)
        return out
    lower = half(pts)
    upper = half(pts[::-1])
    return lower[:-1] + upper[:-1]


def area_clip_split(poly, a, b):
    """One Sutherland-Hodgman pass against a->b: returns (right, left).
    Hull interior is on the LEFT of each CCW hull edge -> right = outside."""
    right, left = [], []
    m = len(poly)
    ex, ez = b[0] - a[0], b[1] - a[1]
    for i in range(m):
        p, q = poly[i], poly[(i + 1) % m]
        sp = ex * (p[1] - a[1]) - ez * (p[0] - a[0])
        sq = ex * (q[1] - a[1]) - ez * (q[0] - a[0])
        inp = sp <= 0.0
        inq = sq <= 0.0
        if inp:
            right.append(p)
        if not inp:
            left.append(p)
        if inp != inq:
            t = sp / (sp - sq)
            xing = (p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1]))
            right.append(xing)
            left.append(xing)
    return right, left


def area_box_blocker(p, box):
    """Convex hull of the two height-cap footprints projected through the
    receiver (the exact radial sweep of the [ylo, yhi] band). box prim:
    (x0, z0, y0, x1, z1, y1)."""
    x0, z0, ylo, x1, z1, yhi = box
    py = p[1]
    ylo2 = max(ylo, py + MARCH_BIAS)
    yhi2 = min(yhi, EMITTER_MIN[1] - MARCH_BIAS)
    if ylo2 >= yhi2 or yhi2 <= py + MARCH_BIAS:
        return None
    def m(h):
        return (EMITTER_MIN[1] - py) / (h - py)
    m_in, m_out = m(yhi2), m(ylo2)
    px, pz = p[0], p[2]
    pts = []
    for (cx, cz) in ((x0, z0), (x1, z0), (x1, z1), (x0, z1)):
        for mm in (m_in, m_out):
            pts.append((px + mm * (cx - px), pz + mm * (cz - pz)))
    hull = area_hull2d(pts)
    return hull if len(hull) >= 3 else None


def area_sphere_blocker(p, sph):
    """Section of the tangent cone {angle(dir, C_hat) <= asin(R/d)}, sampled
    by 33 boundary directions in direction space (robust at the receiver-
    on-surface degeneracy), sub-horizontal generators far-clamped. sph prim:
    (cx, cy, cz, r)."""
    cx, cy, cz, r = sph
    px, py, pz = p
    if cy + r <= py + MARCH_BIAS:
        return None
    gx, gy, gz = cx - px, cy - py, cz - pz
    d = math.sqrt(gx * gx + gy * gy + gz * gz)
    if d * d < (r - MARCH_BIAS) * (r - MARCH_BIAS):
        return [(-1e3, -1e3), (1e3, -1e3), (0.0, 1e3)]   # full-block sentinel
    ch = (gx / d, gy / d, gz / d)
    sa = min(r / d, 1.0)
    ca = math.sqrt(1.0 - sa * sa)
    helper = (0.0, 1.0, 0.0) if abs(ch[1]) < 0.9 else (1.0, 0.0, 0.0)
    e1raw = (ch[1] * helper[2] - ch[2] * helper[1],
             ch[2] * helper[0] - ch[0] * helper[2],
             ch[0] * helper[1] - ch[1] * helper[0])
    e1l = math.sqrt(e1raw[0] ** 2 + e1raw[1] ** 2 + e1raw[2] ** 2)
    e1 = (e1raw[0] / e1l, e1raw[1] / e1l, e1raw[2] / e1l)
    e2 = (ch[1] * e1[2] - ch[2] * e1[1],
          ch[2] * e1[0] - ch[0] * e1[2],
          ch[0] * e1[1] - ch[1] * e1[0])
    thv = math.atan2(e2[1], e1[1])   # the ring's max-d.y direction: conic vertex
    pts = []
    for k in range(AREA_RING + 1):
        th = (2.0 * math.pi * k / AREA_RING) if k < AREA_RING else thv
        ct, st = math.cos(th), math.sin(th)
        dx = ca * ch[0] + sa * (e1[0] * ct + e2[0] * st)
        dy = ca * ch[1] + sa * (e1[1] * ct + e2[1] * st)
        dz = ca * ch[2] + sa * (e1[2] * ct + e2[2] * st)
        if dy > 1e-6:
            s = (EMITTER_MIN[1] - py) / dy
            pts.append((px + s * dx, pz + s * dz))
        else:
            hxz = math.sqrt(dx * dx + dz * dz)
            if hxz > 1e-9:
                pts.append((px + AREA_FAR_CLAMP * dx / hxz,
                            pz + AREA_FAR_CLAMP * dz / hxz))
    if len(pts) < 3:
        return None
    hull = area_hull2d(pts)
    return hull if len(hull) >= 3 else None


def area_subtract(pieces, B):
    """First-outside-edge subtraction of the convex blocker B from every
    piece (exact union; bridges cancel in the Arvo edge sum)."""
    out = []
    for piece in pieces:
        W = piece
        for i in range(len(B)):
            a, b = B[i], B[(i + 1) % len(B)]
            outside, W = area_clip_split(W, a, b)
            if len(outside) >= 3:
                out.append(outside)
            if len(W) < 3:
                break
    return out


def area_emitter_visibility(p, n, prims):
    """(K_vis, K_rect): the exact visible-patch form factor after subtracting
    every occluder's backprojected region from the emitter rect."""
    ly = EMITTER_MIN[1]
    hx, hz = EMITTER_HALF
    rect = [(0.0 - hx, 0.0 - hz), (0.0 + hx, 0.0 - hz),
            (0.0 + hx, 0.0 + hz), (0.0 - hx, 0.0 + hz)]
    k_rect = area_arvo(p[0], p[1], p[2], n, rect)
    if k_rect <= 0.0:
        return 0.0, k_rect
    pieces = [rect]
    for prim in prims:
        if prim[0] == "box":
            B = area_box_blocker(p, prim[1])
        else:
            B = area_sphere_blocker(p, prim[1])
        if B is None:
            continue
        pieces = area_subtract(pieces, B)
        if not pieces:
            return 0.0, k_rect
    k = 0.0
    for piece in pieces:
        k += area_arvo(p[0], p[1], p[2], n, piece)
    return min(max(k, 0.0), k_rect), k_rect


def expected_byte_area(p, n, albedo, prims):
    """M5.0 frozen direct lighting at a probe: the TRUE area emitter --
    E_diffuse = albedo/pi * L_e * K_vis (same Lambert convention as the grid
    model; specular omitted, absorbed by the acceptance band)."""
    k_vis, _k_rect = area_emitter_visibility(p, n, prims)
    e = LE * k_vis
    inv_pi = 1.0 / math.pi
    return (to_bytes(e * albedo[0] * inv_pi),
            to_bytes(e * albedo[1] * inv_pi),
            to_bytes(e * albedo[2] * inv_pi))


# ---------------------------------------------------------------------------
# Frozen direct lighting: point-light grid, Lambert exitance (float64).
# Specular is intentionally omitted: walls are roughness 0.90 dielectrics,
# the add is small, and the acceptance band absorbs it (documented above).
# ---------------------------------------------------------------------------
def expected_byte(p, n, albedo, solids=(), max_h=MAXH_BOXES, mode=None,
                  prims=()):
    """Frozen direct lighting at a probe: the 4x4 flux-preserving grid
    (Lambert exitance) with the M4.0.9 shadow transport -- per-light soft
    parallax-window marches (default), or ONE centroid march shared by the
    rig when mode.centroid is set (M4.0.9.1: the lateral half-plane grade
    runs against prims). M5.0: when mode.area is set the grid is REPLACED by
    the true area emitter (exact visible-patch form factor under the
    analytic backprojection). Specular is intentionally omitted (the
    acceptance band absorbs it)."""
    mode = mode or ShadowMode()
    if mode.area:
        return expected_byte_area(p, n, albedo, prims)
    centroid_vis = None
    if mode.centroid:
        centroid_vis = march_visibility_centroid(
            p[0], p[1], p[2], 0.0, GRID_Y, 0.0, solids, max_h,
            step=mode.step, light_size=mode.light_size,
            lateral=mode.lateral, prims=prims)
    er, eg, eb = 0.0, 0.0, 0.0
    for gx in GRID_XS:
        for gz in GRID_ZS:
            lx, ly, lz = gx - p[0], GRID_Y - p[1], gz - p[2]
            dist = math.sqrt(lx * lx + ly * ly + lz * lz)
            ndotl = (n[0] * lx + n[1] * ly + n[2] * lz) / dist
            if ndotl <= 0.0:
                continue
            if mode.centroid:
                vis = centroid_vis
            else:
                vis = march_visibility(p[0], p[1], p[2], gx, GRID_Y, gz,
                                       solids, max_h, step=mode.step,
                                       penumbra=mode.penumbra)
            if vis <= 0.0:
                continue
            e = GRID_I * ndotl / (dist * dist) * vis
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
        print("%d checks: %d passed, %d failed, %d documented gaps (M5/M8)"
              % (len(self.rows), len(self.rows) - fails - gaps, fails, gaps))
        return fails


def check_probes(rep, img, w, h, solids=(), max_h=MAXH_BOXES, mode=None,
                 prims=()):
    """World-space probes: expected value from the frozen lighting model
    (direct grid + heightfield shadow march)."""
    # Physical albedo per shadow probe, used ONLY by the failure diagnosis
    # (the unshadowed comparison). The umbra row's expectation is ~0, so its
    # table albedo (white) is verdict-neutral; but the wall itself is RED and
    # an inert-march classification must compare against the red-wall model.
    diagnosis_albedo = {
        "tall block umbra (left wall)": ALBEDO_RED,
        "tall block penumbra (floor)": ALBEDO_WHITE,
    }
    probes = [
        # label, point, normal, albedo, half_frac, lo_scale, hi_scale, floor, abs_hi
        ("emitter (unlit L_e=12)", (0.0, EMITTER_MIN[1], 0.0), (0.0, -1.0, 0.0),
         (1.0, 1.0, 1.0), 0.010, 0.90, 1.10, 225, None),
        ("back wall white", (0.0, 2.75, BOX[2] * -1 + 0.01), (0.0, 0.0, 1.0),
         ALBEDO_WHITE, 0.012, 0.55, 1.90, 40, None),
        ("left wall red", (BOX[0] * -1 + 0.01, 2.75, -0.6), (1.0, 0.0, 0.0),
         ALBEDO_RED, 0.012, 0.50, 2.00, 35, None),
        ("right wall green", (BOX[0] - 0.01, 2.75, -0.6), (-1.0, 0.0, 0.0),
         ALBEDO_GREEN, 0.012, 0.50, 2.00, 30, None),
        ("floor (gap between blocks)", (0.0, 0.001, -1.6), (0.0, 1.0, 0.0),
         ALBEDO_WHITE, 0.010, 0.50, 2.00, 30, None),
        ("short block top", (0.9, 1.651, 0.5), (0.0, 1.0, 0.0),
         ALBEDO_WHITE, 0.008, 0.50, 2.00, 25, None),
        # NOTE: the tall block's TOP (y=3.3) is above the frozen camera
        # (y=2.75) and is therefore NEVER visible -- its lit signature in a
        # correct render is the thin right face (normal +x) sliver.
        ("tall block right face", (-0.549, 2.2, -0.5), (1.0, 0.0, 0.0),
         ALBEDO_WHITE, 0.004, 0.40, 2.20, 20, None),
        # --- M4 positive shadow checks -------------------------------------
        # Full umbra: low on the left wall directly beside/behind the tall
        # block's slab -- every grid light's ray enters the block's column
        # interval before reaching this point (verified per light by the
        # march below); camera line of sight passes left of the block.
        ("tall block umbra (left wall)", (BOX[0] * -1 + 0.01, 0.2, -0.85), (1.0, 0.0, 0.0),
         ALBEDO_WHITE, 0.008, 0.0, 0.0, 0, 14),
        # Penumbra: floor point left-front of the tall block -- the front
        # grid row reaches it, the other three rows are blocked by the block.
        # The expected value is the exact 16-light marched result. (The point
        # must stay z <= ~0.45: the frozen camera's frame bottom cuts the
        # floor at z ~ 0.65.)
        ("tall block penumbra (floor)", (-1.7, 0.001, 0.45), (0.0, 1.0, 0.0),
         ALBEDO_WHITE, 0.008, 0.50, 2.00, 15, None),
    ]
    for label, p, n, alb, half_frac, lo_s, hi_s, floor, abs_hi in probes:
        q = project(p, w, h)
        if q is None or not (0.0 <= q[0] < 1.0 and 0.0 <= q[1] < 1.0):
            rep.add("FAIL", label, "probe projects outside the frame -- camera pin broken")
            continue
        # M5.0: the emitter probe reads the UNLIT quad's raw radiance -- its
        # expectation is transport-independent, so it always uses the grid
        # model regardless of mode.area.
        probe_mode = mode
        if mode.area and label.startswith("emitter (unlit"):
            probe_mode = ShadowMode(centroid=mode.centroid,
                                    light_size=mode.light_size,
                                    step=mode.step, penumbra=mode.penumbra,
                                    lateral=mode.lateral, area=False)
        exp = expected_byte(p, n, alb, solids, max_h, probe_mode, prims)
        # M4.0.9/4.0.9.1 centroid mode: the penumbra probe's centroid ray
        # HARD-PIERCES the block (the receiver sits behind it relative to
        # the emitter centroid), so the vertical grade saturates to 0 no
        # matter what the lateral band does -- a pierce is a pierce in any
        # direction. That expected 0 would turn the probe's band
        # [0.5*exp, 2*exp] inside out, so when the mode's model expects
        # black here, judge absolute-dark instead -- the same contract the
        # umbra probe already uses.
        eff_abs_hi = abs_hi
        if (mode.centroid and label == "tall block penumbra (floor)"
                and max(exp) <= 2.0):
            eff_abs_hi = 20.0
        act = patch_median(img, w, h, q[0], q[1], half_frac)
        ok = True
        detail = []
        act_all = []
        for ch, name in enumerate("RGB"):
            # R keeps the probe's visibility floor; G/B use a quarter of it:
            # dark albedo channels (red wall B=0.05) are PHYSICALLY near-black
            # and must not fail the band (measured r2 value: ~15/255).
            ch_floor = floor if ch == 0 else floor * 0.25
            lo = max(ch_floor, lo_s * exp[ch] - DITHER_LSB)
            hi = min(255.0, hi_s * exp[ch] + DITHER_LSB)
            if eff_abs_hi is not None and exp[ch] <= 2.0:
                # Expected-dark probe (umbra, or the centroid-blinded
                # penumbra): judge against an absolute ceiling instead of a
                # band around zero.
                hi = float(eff_abs_hi)
            if not (lo <= act[ch] <= hi):
                ok = False
            detail.append("%s %3d (expect %.0f..%.0f)" % (name, act[ch], lo, hi))
            act_all.append(act[ch])
        if not ok and label in diagnosis_albedo:
            # M4.0.2 failure diagnosis: the two shadow probes are the only
            # checks that can fail because the march never blocked anything
            # (empty field / shadows-off driver behavior looks identical to
            # --no-shadows in the image). Compare against the UNSHADOWED
            # model with the probe's PHYSICAL albedo to classify the failure
            # instead of leaving it unexplained.
            unsh = expected_byte(p, n, diagnosis_albedo[label], (), max_h, mode)
            tol = 3.0
            if all(abs(act_all[c] - unsh[c]) <= tol for c in range(3)):
                kind = ("DIAGNOSIS: matches the UNSHADOWED model (~%d/%d/%d) "
                        "-- march inert: field empty, shadows off, or "
                        "--no-shadows; see telemetry 'shadows:' line and the "
                        "[ShadowHeightfield] field verify console line"
                        % (unsh[0], unsh[1], unsh[2]))
            elif all(act_all[c] <= unsh[c] + tol for c in range(3)):
                kind = ("DIAGNOSIS: partial leak (between shadowed and "
                        "unshadowed models ~%d/%d/%d) -- march active but "
                        "grazing rays leak (field erosion / probe inside the "
                        "silhouette band)"
                        % (unsh[0], unsh[1], unsh[2]))
            else:
                kind = "DIAGNOSIS: darker than the shadowed model -- over-blocking"
            detail.append(kind)
        rep.add("PASS" if ok else "FAIL", label, "  ".join(detail))


def main():
    ap = argparse.ArgumentParser(description="M3.3 Cornell acceptance harness")
    ap.add_argument("shot", nargs="?", default=None,
                    help="deterministic screenshot (PPM, from sandbox --frames/--out)")
    ap.add_argument("--variant", default="cornell01",
                    choices=["cornell01", "cornell02", "cornell03"])
    ap.add_argument("--shadow-centroid", type=int, default=0, choices=[0, 1],
                    help="shadow transport of the shot: 0 = per-light soft "
                         "parallax-window marches (sandbox M4.0.9 default), "
                         "1 = centroid rig march (--shadow-centroid 1)")
    ap.add_argument("--shadow-light-size", type=float, default=LIGHT_SIZE,
                    help="centroid half-plane window S in meters (mirrors "
                         "--shadow-light-size; 0 = binary centroid march)")
    ap.add_argument("--shadow-step", type=float, default=None,
                    help="march step override in meters (mirrors --shadow-step)")
    ap.add_argument("--shadow-penumbra", type=float, default=PENUMBRA,
                    help="parallax window scale (mirrors --shadow-penumbra; "
                         "0 = the exact binary march)")
    ap.add_argument("--shadow-lateral", type=int, default=LATERAL_DEFAULT,
                    choices=[0, 1],
                    help="centroid-path lateral half-plane (mirrors "
                         "--shadow-lateral; 0 = the exact M4.0.9 centroid "
                         "march)")
    ap.add_argument("--area-light", type=int, default=AREA_LIGHT_DEFAULT,
                    choices=[0, 1],
                    help="shadow transport of the shot: 1 = M5.0 true "
                         "area-light transport (sandbox default), 0 = the "
                         "exact M4.0.9.1 grid transport (all replay pins)")
    ap.add_argument("--self-test-area", action="store_true",
                    help="validate the float64 area model against the frozen "
                         "check_area_model.py fixtures and exit")
    ap.add_argument("--expect-md5", default=None,
                    help="fail if the shot's md5 differs (determinism pin for the ledger)")
    args = ap.parse_args()

    if not args.self_test_area and not args.shot:
        ap.error("the following arguments are required: shot")

    if args.self_test_area:
        # Validate THIS harness's float64 area port against the same frozen
        # fixtures bench_tests pins (check_area_model.py section 7 output).
        prims01 = occluder_primitives("cornell01")
        prims02 = occluder_primitives("cornell02")
        prims03 = occluder_primitives("cornell03")
        fixtures = [
            ("c01 floor mid   ", (0.0, 0.0, 2.3), (0.0, 1.0, 0.0), prims01, 0.032497),
            ("c01 floor center", (0.0, 0.0, 0.0), (0.0, 1.0, 0.0), prims01, 0.044600),
            ("c01 adjacent    ", (-0.70, 0.0, 0.55), (0.0, 1.0, 0.0), prims01, 0.031218),
            ("c03 under both  ", (-0.80, 0.0, 0.60), (0.0, 1.0, 0.0), prims03, 0.016727),
            ("c02 sphere band ", (0.0, 0.0, 1.70), (0.0, 1.0, 0.0),
             [pr for pr in prims02 if pr[0] == "sphere" or pr == prims02[0]], 0.034376),
            ("lit open floor  ", (2.20, 0.0, 2.20), (0.0, 1.0, 0.0), prims01, 0.022582),
        ]
        bad = 0
        for label, p, n, prims, expected in fixtures:
            k_vis, _ = area_emitter_visibility(p, n, prims)
            rel = abs(k_vis - expected) / max(expected, 1e-12)
            okk = rel <= 5e-3
            bad += 0 if okk else 1
            print("  %s K_vis=%.6f (fixture %.6f, rel %.2e) %s"
                  % (label, k_vis, expected, rel, "OK" if okk else "FAIL"))
        print("area model self-test: %s" % ("ALL OK" if bad == 0 else "%d FAILURE(S)" % bad))
        return 0 if bad == 0 else 1

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
    solids = solid_intervals(args.variant)
    max_h = MAXH_BAFFLE if args.variant == "cornell03" else MAXH_BOXES
    prims = occluder_primitives(args.variant)
    mode = ShadowMode(centroid=bool(args.shadow_centroid),
                      light_size=args.shadow_light_size,
                      step=args.shadow_step,
                      penumbra=args.shadow_penumbra,
                      lateral=bool(args.shadow_lateral),
                      area=bool(args.area_light))
    check_probes(rep, img, w, h, solids, max_h, mode, prims)
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
    rep.add("GAP", "indirect color bounce (criterion)", "no GI until M8; similarity axis quantifies it vs the reference")
    rep.add("GAP", "area-light residuals (criterion)", "M5.0 transport: exact for boxes and unions; documented "
            "residuals are the sphere's inscribed-conic under-block (<=0.9% "
            "of K, one-sided), the representative-point specular, and the "
            "similarity axis measures them vs the reference")
    rep.add("INFO", "cornell02/03 extras", "spheres/baffle probes intentionally via the SSIM similarity axis, not this harness")

    fails = rep.print()
    print("verdict: %s" % ("ACCEPTED -- record this shot (md5 + similarity vs the clean reference) as the milestone ledger row" if fails == 0 else "REJECTED"))
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
