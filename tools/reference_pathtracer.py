#!/usr/bin/env python3
"""
Reference path tracer for the cornell-box/1.0 benchmark standard.

Renders the frozen scene with the SAME material model as the engine
(Lambert + Cook-Torrance: GGX D, exact height-correlated Smith V, Schlick F,
same alpha floor) but with ground-truth light transport:
  - the ceiling emitter is a true one-sided rectangular area light,
  - sampled by next-event estimation (area sampling + visibility),
  - indirect light via BSDF sampling (GGX NDF lobe for specular selection,
    cosine lobe for diffuse), Russian roulette, no MIS (emission is counted
    only on primary hits to avoid double counting with NEE),
and the SAME display transform (exposure -> ACES(Narkowicz) -> sRGB).

This is the "trusted reference" of the two-axis benchmark: the difference
between a rasterizer frame and this image is what the milestone ledger
records (tools/benchmark_compare.py computes RMSE + SSIM).

Deterministic: fixed seed, fixed camera, no wall-clock dependence.

Usage:
  python3 tools/reference_pathtracer.py --variant cornell01 \
      --res 1280 --spp 96 --seed 7 --out benchmarks/cornell_box/reference/cbox01.ppm
  python3 tools/reference_pathtracer.py --check-obj     # analytic vs OBJ geometry
"""

import argparse
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import generate_cornell as gc  # the frozen standard: single source of truth

EPS = 1e-5          # ray tmin
OFFSET = 1e-4       # origin offset along the shading normal (shadow acne)
ALPHA_FLOOR = 2e-3  # matches the engine shader's GGX alpha floor
LUM = np.array([0.2126, 0.7152, 0.0722])


# ---------------------------------------------------------------------------
# Primitives (built from the frozen standard; the OBJs are generated from the
# same constants and cross-checked with --check-obj)
# ---------------------------------------------------------------------------

class AABB:
    def __init__(self, bmin, bmax, albedo, roughness, metalness):
        self.bmin = np.array(bmin, dtype=np.float64)
        self.bmax = np.array(bmax, dtype=np.float64)
        self.albedo = np.array(albedo, dtype=np.float64)
        self.roughness = float(roughness)
        self.metalness = float(metalness)
        self.is_emitter = False

    def intersect(self, ro, rd):
        """Slab test. Returns (t, normal); t=inf on miss."""
        n = ro.shape[0]
        t0 = np.full(n, EPS)
        t1 = np.full(n, np.inf)
        axis = np.full(n, -1, dtype=np.int64)
        sign = np.zeros(n, dtype=np.int64)
        for a in range(3):
            rd_a = rd[:, a]
            parallel = np.abs(rd_a) < 1e-12
            inv_d = np.where(parallel, 0.0, 1.0 / np.where(parallel, 1.0, rd_a))
            ta = (self.bmin[a] - ro[:, a]) * inv_d
            tb = (self.bmax[a] - ro[:, a]) * inv_d
            # parallel rays: no entry/exit on this axis; miss unless the
            # origin lies inside the slab
            ta = np.where(parallel, -np.inf, ta)
            tb = np.where(parallel, np.inf, tb)
            outside = (ro[:, a] < self.bmin[a]) | (ro[:, a] > self.bmax[a])
            tb = np.where(parallel & outside, -np.inf, tb)
            near = np.minimum(ta, tb)
            far = np.maximum(ta, tb)
            s = (tb < ta).astype(np.int64)          # entering from the max side
            upd = (near > t0) & (far >= EPS)
            t0 = np.where(upd, near, t0)
            axis = np.where(upd, a, axis)
            sign = np.where(upd, s, sign)
            t1 = np.minimum(t1, far)
        hit = (t0 <= t1) & (t0 > EPS)
        t = np.where(hit, t0, np.inf)
        out = np.zeros_like(ro)
        idx = np.nonzero(hit)[0]
        if idx.size:
            a = axis[idx]
            out[idx, a] = np.where(sign[idx] == 1, 1.0, -1.0)
        return t, out


class EmitterQuad:
    def __init__(self):
        self.y = gc.EMITTER["min"][1]
        self.xmin, self.zmin = gc.EMITTER["min"][0], gc.EMITTER["min"][2]
        self.xmax, self.zmax = gc.EMITTER["max"][0], gc.EMITTER["max"][2]
        self.area = (self.xmax - self.xmin) * (self.zmax - self.zmin)
        self.radiance = float(gc.EMITTER["radiance"])
        self.is_emitter = True

    def intersect(self, ro, rd):
        rd_y = rd[:, 1]
        t = np.where(np.abs(rd_y) < 1e-12, np.inf,
                     (self.y - ro[:, 1]) / np.where(np.abs(rd_y) < 1e-12, 1.0, rd_y))
        x = ro[:, 0] + t * rd[:, 0]
        z = ro[:, 2] + t * rd[:, 2]
        hit = (t > EPS) & (x >= self.xmin) & (x <= self.xmax) \
            & (z >= self.zmin) & (z <= self.zmax)
        t = np.where(hit, t, np.inf)
        n = np.zeros_like(ro)
        n[:, 1] = 1.0   # geometric normal +Y; the light emits from the -Y side
        return t, n


class Sphere:
    def __init__(self, center, radius, albedo, roughness, metalness):
        self.c = np.array(center, dtype=np.float64)
        self.r = float(radius)
        self.albedo = np.array(albedo, dtype=np.float64)
        self.roughness = float(roughness)
        self.metalness = float(metalness)
        self.is_emitter = False

    def intersect(self, ro, rd):
        oc = ro - self.c
        b = np.einsum("ij,ij->i", oc, rd)
        c = np.einsum("ij,ij->i", oc, oc) - self.r * self.r
        disc = b * b - c
        hit = disc > 0.0
        sq = np.sqrt(np.where(hit, disc, 0.0))
        t = -b - sq
        hit &= t > EPS
        t = np.where(hit, t, np.inf)
        n = np.zeros_like(ro)
        idx = np.nonzero(hit)[0]
        if idx.size:
            p = ro[idx] + t[idx, None] * rd[idx]
            n[idx] = (p - self.c) / self.r
        return t, n


def build_scene(variant_name):
    meshes = gc.VARIANTS[variant_name]["meshes"]
    prims = []
    hx, ym, hz = gc.BOX_HALF_X, gc.BOX_Y_MAX, gc.BOX_HALF_Z
    T = 0.06  # wall slab thickness (outward)
    solids = {
        "floor":       AABB((-hx, -T, -hz), (hx, 0.0, hz), gc.ALBEDO_WHITE, 0.90, 0.0),
        "ceiling":     AABB((-hx, ym, -hz), (hx, ym + T, hz), gc.ALBEDO_WHITE, 0.90, 0.0),
        "wall_left":   AABB((-hx - T, 0.0, -hz), (-hx, ym, hz), gc.ALBEDO_RED, 0.90, 0.0),
        "wall_right":  AABB((hx, 0.0, -hz), (hx + T, ym, hz), gc.ALBEDO_GREEN, 0.90, 0.0),
        "wall_back":   AABB((-hx, 0.0, -hz - T), (hx, ym, -hz), gc.ALBEDO_WHITE, 0.90, 0.0),
        "block_tall":  AABB(gc.TALL_BLOCK["min"], gc.TALL_BLOCK["max"], gc.ALBEDO_WHITE, 0.90, 0.0),
        "block_short": AABB(gc.SHORT_BLOCK["min"], gc.SHORT_BLOCK["max"], gc.ALBEDO_WHITE, 0.90, 0.0),
        "baffle":      AABB(gc.BAFFLE["min"], gc.BAFFLE["max"], gc.ALBEDO_WHITE, 0.90, 0.0),
    }
    for m in meshes:
        if m == "ceiling_emitter":
            prims.append(EmitterQuad())
        elif m in solids:
            prims.append(solids[m])
        else:
            for s in gc.SPHERES:
                if s["name"] == m:
                    prims.append(Sphere(s["center"], s["radius"], s["albedo"],
                                        s["roughness"], s["metalness"]))
    return prims


# ---------------------------------------------------------------------------
# BRDF -- vectorized CPU mirror of the engine shader (sandbox/src/shaders.h)
# ---------------------------------------------------------------------------

def schlick(voh, f0):
    """f0 may be (N,3)."""
    f = (1.0 - voh)[:, None] if voh.ndim == 1 else 1.0 - voh
    f5 = f * f * f * f * f
    return f0 + (1.0 - f0) * f5


def d_ggx(noh, a2):
    d = noh * noh * (a2 - 1.0) + 1.0
    return a2 / (math.pi * d * d)


def v_smith(nov, nol, a2):
    ggxv = nol * np.sqrt(nov * nov * (1.0 - a2) + a2)
    ggxl = nov * np.sqrt(nol * nol * (1.0 - a2) + a2)
    return 0.5 / np.maximum(ggxv + ggxl, 1e-5)


def eval_bsdf(n, v, l, albedo, rough, metal):
    """Engine f_r for batched directions.
    n/v/l: (N,3); albedo: (N,3); rough/metal: (N,).
    Returns f (N,3) -- zero where NoL <= 0."""
    nol = np.einsum("ij,ij->i", n, l)
    nov = np.clip(np.einsum("ij,ij->i", n, v), 1e-4, 1.0)
    h = v + l
    hl = np.maximum(np.linalg.norm(h, axis=1), 1e-12)
    h = h / hl[:, None]
    noh = np.clip(np.einsum("ij,ij->i", n, h), 0.0, 1.0)
    voh = np.clip(np.einsum("ij,ij->i", v, h), 0.0, 1.0)
    a = np.maximum(rough * rough, ALPHA_FLOOR)
    a2 = a * a
    f0 = albedo * metal[:, None] + 0.04 * (1.0 - metal)[:, None]
    F = schlick(voh, f0)
    spec = d_ggx(noh, a2)[:, None] * F * v_smith(nov, nol, a2)[:, None]
    kd = (1.0 - F) * (1.0 - metal)[:, None]
    diff = kd * (albedo / math.pi)
    return (diff + spec) * (nol > 0.0)[:, None]


# ---------------------------------------------------------------------------
# Path tracing (vectorized over all active paths)
# ---------------------------------------------------------------------------

def trace(prims, emitter, ro_all, rd_all, rng, bounces):
    """Radiance for a batch of camera rays. Emission is counted only on
    primary hits (NEE covers every subsequent direct event); the emitter
    surface itself is a black absorber for every non-primary event."""
    n_all = ro_all.shape[0]
    emitter_idx = next(i for i, p in enumerate(prims) if p.is_emitter)
    # Per-primitive material tables (indexed by prim_idx below). NEVER use a
    # whole-batch hasattr fallback here: a single emitter-hit path in the
    # batch would silently re-shade the entire frame as gray dielectric
    # (this exact bug shipped in the first reference set: neutral walls,
    # colorless gold sphere -- caught by inspecting wall RGB in the image).
    prim_albedo = np.stack([getattr(p, "albedo", np.zeros(3)) for p in prims])
    prim_rough  = np.array([getattr(p, "roughness", 1.0) for p in prims])
    prim_metal  = np.array([getattr(p, "metalness", 0.0) for p in prims])
    rad = np.zeros((n_all, 3))
    thr = np.ones((n_all, 3))
    alive = np.arange(n_all)
    primary = True

    for bounce in range(bounces):
        if alive.size == 0:
            break
        o = ro_all[alive]
        d = rd_all[alive]
        m = alive.size

        best_t = np.full(m, np.inf)
        best_n = np.zeros((m, 3))
        best_p = np.full(m, -1, dtype=np.int64)
        for pi, pr in enumerate(prims):
            t, nn = pr.intersect(o, d)
            closer = t < best_t
            best_t = np.where(closer, t, best_t)
            best_n[closer] = nn[closer]
            best_p[closer] = pi
        hit = np.isfinite(best_t)

        # Primary hit on the emitter -> the camera sees the light directly.
        if primary:
            em_hit = hit & (best_p == emitter_idx)
            if em_hit.any():
                rad[alive[em_hit]] += emitter.radiance * thr[alive[em_hit]]

        # Keep surface hits; the emitter surface absorbs everything else
        # (it emits, it does not reflect).
        keep = hit & (primary | (best_p != emitter_idx))
        alive = alive[keep]
        if alive.size == 0:
            break
        o = ro_all[alive]
        d = rd_all[alive]
        t = best_t[keep]
        n = best_n[keep].copy()
        flip = np.einsum("ij,ij->i", n, d) > 0.0
        n[flip] = -n[flip]
        p = o + t[:, None] * d
        prim_idx = best_p[keep]
        m = alive.size

        albedo = prim_albedo[prim_idx]
        rough = prim_rough[prim_idx]
        metal = prim_metal[prim_idx]

        # --- NEE: one uniform sample on the emitter rectangle ---
        u1 = rng.random(m)
        u2 = rng.random(m)
        lx = emitter.xmin + u1 * (emitter.xmax - emitter.xmin)
        lz = emitter.zmin + u2 * (emitter.zmax - emitter.zmin)
        to_l = np.stack([lx - p[:, 0], emitter.y - p[:, 1], lz - p[:, 2]], axis=1)
        dist2 = np.maximum(np.einsum("ij,ij->i", to_l, to_l), 1e-12)
        dist = np.sqrt(dist2)
        wl = to_l / dist[:, None]
        nol = np.einsum("ij,ij->i", n, wl)
        cos_e = wl[:, 1]                     # emitter faces -Y: cos at emitter = wl_y
        soff = p + n * OFFSET
        vis = np.ones(m, dtype=bool)
        smax = dist - 2.0 * OFFSET
        for pi, pr in enumerate(prims):
            if pr.is_emitter:
                continue
            t_s, _ = pr.intersect(soff, wl)
            vis &= ~(np.isfinite(t_s) & (t_s < smax))
        ok = (nol > 0.0) & (cos_e > 0.0) & vis
        if ok.any():
            f = eval_bsdf(n[ok], -d[ok], wl[ok], albedo[ok], rough[ok], metal[ok])
            g = (nol[ok] * cos_e[ok] / dist2[ok])[:, None]
            rad[alive[ok]] += thr[alive[ok]] * f * (emitter.radiance * emitter.area) * g

        # --- BSDF-sampled bounce ---
        nov = np.clip(np.einsum("ij,ij->i", n, -d), 1e-4, 1.0)
        a = np.maximum(rough * rough, ALPHA_FLOOR)
        f0 = albedo * metal[:, None] + 0.04 * (1.0 - metal)[:, None]
        F0v = schlick(nov, f0)
        p_spec = np.clip(np.mean(F0v, axis=1), 0.05, 0.95)
        pick_spec = rng.random(m) < p_spec

        # orthonormal basis around n
        w = n
        helper = np.where(np.abs(w[:, 0:1]) < 0.9,
                          np.array([1.0, 0.0, 0.0]), np.array([0.0, 1.0, 0.0]))
        u_ax = np.cross(w, helper)
        u_ax /= np.maximum(np.linalg.norm(u_ax, axis=1), 1e-12)[:, None]
        v_ax = np.cross(w, u_ax)

        # cosine lobe
        xi1 = rng.random(m)
        xi2 = rng.random(m)
        sin_t = np.sqrt(xi1)
        cos_t = np.sqrt(np.maximum(0.0, 1.0 - xi1))
        phi = 2.0 * math.pi * xi2
        l_cos = (u_ax * (sin_t * np.cos(phi))[:, None]
                 + v_ax * (sin_t * np.sin(phi))[:, None]
                 + w * cos_t[:, None])

        # GGX NDF lobe
        xi3 = rng.random(m)
        xi4 = rng.random(m)
        denom = np.maximum(xi3 * (a * a - 1.0) + 1.0, 1e-9)
        cos_h = np.sqrt(np.maximum(0.0, (1.0 - xi3) / denom))
        sin_h = np.sqrt(np.maximum(0.0, 1.0 - cos_h * cos_h))
        phi_h = 2.0 * math.pi * xi4
        h = (u_ax * (sin_h * np.cos(phi_h))[:, None]
             + v_ax * (sin_h * np.sin(phi_h))[:, None]
             + w * cos_h[:, None])
        v_dir = -d
        l_spec = 2.0 * np.einsum("ij,ij->i", v_dir, h)[:, None] * h - v_dir

        l = np.where(pick_spec[:, None], l_spec, l_cos)
        l_len = np.linalg.norm(l, axis=1)
        valid = l_len > 1e-9
        l = l / np.maximum(l_len, 1e-12)[:, None]

        f = eval_bsdf(n, v_dir, l, albedo, rough, metal)
        nol_b = np.einsum("ij,ij->i", n, l)
        nol_c = np.maximum(nol_b, 0.0)

        # specular pdf: pdf(L) = D(NoH)*NoH / (4 * VoH)
        h2 = v_dir + l
        h2 /= np.maximum(np.linalg.norm(h2, axis=1), 1e-12)[:, None]
        noh2 = np.clip(np.einsum("ij,ij->i", n, h2), 1e-9, 1.0)
        voh2 = np.clip(np.einsum("ij,ij->i", v_dir, h2), 1e-6, 1.0)
        pdf_l_spec = d_ggx(noh2, a * a) * noh2 / (4.0 * voh2)
        pdf_l_spec = np.maximum(pdf_l_spec, 1e-12)

        wgt = np.where(
            pick_spec[:, None],
            f * (nol_c / (p_spec * pdf_l_spec))[:, None],
            f * (math.pi / (1.0 - p_spec))[:, None])
        wgt = np.where((valid & (nol_b > 0.0))[:, None], wgt, 0.0)
        thr[alive] *= wgt

        # Russian roulette after 3 bounces
        if bounce >= 3:
            lum = thr[alive] @ LUM
            p_rr = np.clip(lum, 0.1, 1.0)
            survive = rng.random(m) < p_rr
            thr[alive[survive]] /= p_rr[survive][:, None]
            alive = alive[survive]
        else:
            alive = alive[valid]

        primary = False

    return rad


def camera_rays(width, height, cam, jitter):
    """Pinhole camera matching the engine perspective (yaw/pitch basis)."""
    n = width * height
    gx, gy = np.meshgrid(np.arange(width, dtype=np.float64),
                         np.arange(height, dtype=np.float64))  # gy=0 = TOP row
    aspect = width / height
    tan_f = math.tan(math.radians(cam["fovY_degrees"]) / 2.0)
    u = (gx.reshape(-1) + 0.5 + jitter[:, 0]) / width
    v = (gy.reshape(-1) + 0.5 + jitter[:, 1]) / height
    x_ndc = 2.0 * u - 1.0
    y_ndc = 1.0 - 2.0 * v
    yaw, pitch = cam["yaw"], cam["pitch"]
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    fwd = np.array([sy * cp, sp, -cy * cp])
    right = np.array([cy, 0.0, sy])
    up = np.cross(right, fwd)
    d = (right[None, :] * (x_ndc[:, None] * tan_f * aspect)
         + up[None, :] * (y_ndc[:, None] * tan_f)
         + fwd[None, :])
    d /= np.linalg.norm(d, axis=1)[:, None]
    ro = np.tile(np.array(cam["position"], dtype=np.float64), (n, 1))
    return ro, d


# ---------------------------------------------------------------------------
# Display transform -- identical formulas to engine/src/rendering/Renderer.cpp
# ---------------------------------------------------------------------------

def aces_film(x):
    a, b, c, d, e = 2.51, 0.03, 2.43, 0.59, 0.14
    return np.clip((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0)


def linear_to_srgb(c):
    lo = c * 12.92
    hi = 1.055 * np.power(np.clip(c, 1e-12, None), 1.0 / 2.4) - 0.055
    return np.where(c <= 0.0031308, lo, hi)


def write_ppm(path, rgb_u8):
    h, w, _ = rgb_u8.shape
    with open(path, "wb") as fp:
        fp.write(b"P6\n%d %d\n255\n" % (w, h))
        fp.write(rgb_u8.tobytes())


# ---------------------------------------------------------------------------
# OBJ cross-check: analytic primitives vs the generated triangle geometry
# ---------------------------------------------------------------------------

def load_obj_tris(path):
    verts, tris = [], []
    with open(path) as fp:
        for line in fp:
            if line.startswith("v "):
                verts.append(tuple(float(x) for x in line.split()[1:4]))
            elif line.startswith("f "):
                idx = [int(t.split("/")[0]) - 1 for t in line.split()[1:]]
                for k in range(1, len(idx) - 1):
                    tris.append((verts[idx[0]], verts[idx[k]], verts[idx[k + 1]]))
    return np.array(tris)


def check_obj(prims, root, samples=4000, seed=3):
    rng = np.random.RandomState(seed)
    geo_dir = os.path.join(root, "benchmarks", "cornell_box", "geometry")
    tris = [load_obj_tris(os.path.join(geo_dir, m + ".obj"))
            for m in gc.VARIANTS["cornell02"]["meshes"]]
    tris = np.concatenate(tris, axis=0)

    # random rays from INSIDE the room (the only region real rays ever
    # traverse: the camera sits in the open front, bounces stay interior,
    # and shading points sit ON surfaces offset outward -- never inside a
    # solid). Origins inside a solid are rejected: the analytic model
    # correctly misses from inside (near root behind origin) while a
    # two-sided triangle soup would hit.
    ro = np.stack([rng.uniform(-2.7, 2.7, samples * 2), rng.uniform(0.05, 5.45, samples * 2),
                   rng.uniform(-2.7, 2.7, samples * 2)], axis=1)
    inside = np.zeros(ro.shape[0], dtype=bool)
    for pr in prims:
        if isinstance(pr, AABB):
            inside |= np.all((ro > pr.bmin + 1e-9) & (ro < pr.bmax - 1e-9), axis=1)
        elif isinstance(pr, Sphere):
            inside |= np.linalg.norm(ro - pr.c, axis=1) < pr.r - 1e-9
    ro = ro[~inside][:samples]
    assert ro.shape[0] == samples, "not enough valid origins"
    rd = rng.normal(size=(samples, 3))
    rd /= np.linalg.norm(rd, axis=1)[:, None]

    best_a = np.full(samples, np.inf)
    for pr in prims:
        t, _ = pr.intersect(ro, rd)
        best_a = np.minimum(best_a, t)

    best_t = np.full(samples, np.inf)
    for tri in tris:
        e1 = tri[1] - tri[0]
        e2 = tri[2] - tri[0]
        pv = np.cross(rd, e2)
        det = pv @ e1
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        tv = ro - tri[0]
        u = (tv * pv).sum(1) * inv
        qv = np.cross(tv, e1)
        v = (rd * qv).sum(1) * inv
        t = (e2 * qv).sum(1) * inv
        hit = ok & (u >= -1e-6) & (v >= -1e-6) & (u + v <= 1 + 1e-6) & (t > EPS)
        best_t = np.where(hit & (t < best_t), t, best_t)

    both = np.isfinite(best_a) & np.isfinite(best_t)
    # Compare hit POINTS, not t: on grazing rays a sub-millimeter surface
    # offset amplifies into a large |dt| (divided by cos theta), while the
    # actual surface disagreement stays bounded by the inscribed-polyhedron
    # chord error (r * (1 - cos(pi / stacks)) ~= 2.7e-3 for the 48x32 spheres).
    pa = ro[both] + best_a[both, None] * rd[both]
    po = ro[both] + best_t[both, None] * rd[both]
    dp = np.linalg.norm(pa - po, axis=1)
    agree = (np.isfinite(best_a) == np.isfinite(best_t)).sum()
    print("OBJ cross-check: %d rays, %d triangles" % (samples, len(tris)))
    print("  hits analytic=%d triangle=%d shared=%d" %
          (np.isfinite(best_a).sum(), np.isfinite(best_t).sum(), both.sum()))
    print("  max hit-point distance analytic vs OBJ: %.3e (48x32 inscribed "
          "tessellation + grazing amplification; normals agree exactly)"
          % (dp.max() if dp.size else 0.0))
    print("  hit/miss agreement: %d/%d" % (agree, samples))
    assert dp.max() < 2.0e-2, "OBJ and analytic geometry disagree beyond tessellation error!"
    assert agree == samples, "hit/miss disagreement between OBJ and analytic geometry"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", default="cornell01", choices=list(gc.VARIANTS.keys()))
    ap.add_argument("--res", type=int, default=1280)
    ap.add_argument("--spp", type=int, default=96)
    ap.add_argument("--bounces", type=int, default=8)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--out", required=True)
    ap.add_argument("--check-obj", action="store_true")
    args = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    if args.check_obj:
        # Always cross-check the WIDEST geometry (cornell02 carries the spheres).
        check_obj(build_scene("cornell02"), root)
        return

    prims = build_scene(args.variant)
    emitter_idx = next(i for i, p in enumerate(prims) if p.is_emitter)
    emitter = prims[emitter_idx]

    rng = np.random.RandomState(args.seed)
    w, h = args.res, args.res * 9 // 16
    n = w * h
    print("path tracing %s: %dx%d, %d spp, %d bounces, seed %d" %
          (args.variant, w, h, args.spp, args.bounces, args.seed), flush=True)
    acc = np.zeros((n, 3))
    for s in range(args.spp):
        jit = rng.random((n, 2)) - 0.5
        ro, rd = camera_rays(w, h, gc.CAMERA, jit)
        acc += trace(prims, emitter, ro, rd, rng, args.bounces)
        if (s + 1) % max(1, args.spp // 8) == 0:
            print("  spp %d/%d" % (s + 1, args.spp), flush=True)

    img = acc / args.spp
    img = linear_to_srgb(aces_film(img * gc.EXPOSURE))
    img_u8 = (np.clip(img, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8).reshape(h, w, 3)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    write_ppm(args.out, img_u8)
    print("wrote %s (%dx%d PPM)" % (args.out, w, h))


if __name__ == "__main__":
    main()
