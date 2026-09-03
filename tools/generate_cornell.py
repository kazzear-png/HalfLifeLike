#!/usr/bin/env python3
"""
cornell-box/1.0 -- the repo's frozen benchmark standard (M3.3).

Emits, deterministically, from the single set of constants below:
  benchmarks/cornell_box/geometry/*.obj   the box solids (v/vn/f triangles)
  benchmarks/cornell_box/scene.json       the authoritative scene description
  sandbox/src/cornell_scene_gen.h         the same constants as C++ (codegen)

Provenance & rules:
  - Synthetic Cornell Box variant (meter scale, Y-up), reflectances taken from
    the pbrt book's cornell_box scene (white 0.725/0.71/0.68, left red
    0.63/0.065/0.05, right green 0.14/0.45/0.091). Layout inspired by the
    classic box: open front, two white blocks, rectangular ceiling emitter.
  - THIS FILE DEFINES THE STANDARD. Once generated and landed, the geometry,
    camera, lights, materials, and exposure are FROZEN. Renderer changes are
    measured against this scene; the scene never changes to flatter them.

Geometry revision history (part of the standard's provenance):
  r1 (M3.3 initial): all five room quads were authored with normals pointing
    AWAY from the room interior (floor -Y, ceiling +Y, walls outward), three
    of them with winding inconsistent with their own vn. The reference path
    tracer shades two-sided and never noticed; the rasterizer shades
    one-sided (NoL <= 0 -> zero light, zero ambient by design) and rendered
    the entire room black -- caught by the first on-hardware Cornell
    screenshot (96.5% black pixels), M3.3.1.
  r2 (M3.3.1, current): room quads' normals AND winding face the room
    interior. Positions, sizes, materials, camera, lights, exposure: all
    unchanged from r1. Generator now self-validates orientation on every
    run, and bench_tests pins it on every build.
  - The engine has no area lights and no GI (yet): the rasterizer illuminates
    the room through a deterministic point-light grid approximating the
    emitter, while the reference path tracer (tools/reference_pathtracer.py)
    samples the true area light with full bounces. The gap between the two is
    exactly what the benchmark measures.
"""

import json
import math
import os
import sys

# ---------------------------------------------------------------------------
# The frozen standard. Every number below is part of cornell-box/1.0.
# ---------------------------------------------------------------------------

STANDARD = "cornell-box/1.0"

# Interior dimensions (meters): x across, y up, z toward camera.
BOX_HALF_X = 2.75
BOX_Y_MAX  = 5.50
BOX_HALF_Z = 2.75

# pbrt book reflectances (linear).
ALBEDO_WHITE = (0.725, 0.71, 0.68)
ALBEDO_RED   = (0.63, 0.065, 0.05)
ALBEDO_GREEN = (0.14, 0.45, 0.091)

# Blocks (meters), standing on the floor.
TALL_BLOCK  = {"name": "block_tall",  "min": (-1.55, 0.0, -2.05), "max": (-0.55, 3.30, 0.35)}
SHORT_BLOCK = {"name": "block_short", "min": ( 0.40, 0.0, -0.70), "max": ( 1.40, 1.65, 1.70)}

# Rectangular ceiling emitter (one-sided, emits DOWN).
EMITTER = {
    "min": (-0.65, 5.49, -0.525),
    "max": ( 0.65, 5.49,  0.525),
    "radiance": 12.0,          # linear Lambertian emitter radiance L_e
    "color": (1.0, 1.0, 1.0),
}

# CBox-02 spheres (radius 0.55, resting on the floor).
SPHERES = [
    {"name": "sphere_mirror",     "center": (-1.70, 0.55, 1.55), "radius": 0.55,
     "albedo": (0.95, 0.96, 0.98), "roughness": 0.02, "metalness": 1.0},
    {"name": "sphere_gold",       "center": ( 0.00, 0.55, 0.90), "radius": 0.55,
     "albedo": (1.00, 0.77, 0.34), "roughness": 0.45, "metalness": 1.0},
    {"name": "sphere_dielectric", "center": ( 1.70, 0.55, 1.55), "radius": 0.55,
     "albedo": (0.75, 0.75, 0.75), "roughness": 0.18, "metalness": 0.0},
]

# CBox-03 baffle: blocks the DIRECT view of the emitter from most of the room;
# whatever light remains in the reference is interreflection. The current
# rasterizer (no shadows) leaks direct light through it -- the measured gap.
BAFFLE = {"name": "baffle", "min": (-1.00, 3.40, -0.10), "max": (1.00, 5.40, 0.10)}

# Camera (engine convention: yaw 0 looks down -Z).
CAMERA = {"position": (0.0, 2.75, 8.35), "yaw": 0.0, "pitch": 0.0,
          "fovY_degrees": 39.3, "near": 0.1, "far": 100.0}

EXPOSURE = 1.0

# Rasterizer approximation of the area emitter: deterministic grid of point
# lights. Flux of a one-sided Lambertian emitter: Phi = pi * A * L_e; an
# engine point light with intensity I produces irradiance I/d^2, and an
# isotropic point light carrying flux I*pi matches that convention, so each
# of the N grid lights gets I = A * L_e / N (derived in scene.json).
LIGHT_GRID = {"nx": 4, "nz": 4, "y_offset": -0.05}

# ---------------------------------------------------------------------------


def aabb_faces(bmin, bmax):
    """Six CCW-outward quads of an axis-aligned box: (4 corner positions, normal)."""
    x0, y0, z0 = bmin
    x1, y1, z1 = bmax
    faces = [
        # (corners in CCW order seen from outside, outward normal)
        ([(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0, 0, 1)),    # +Z
        ([(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)], (0, 0, -1)),   # -Z
        ([(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)], (1, 0, 0)),    # +X
        ([(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)], (-1, 0, 0)),   # -X
        ([(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)], (0, 1, 0)),    # +Y
        ([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0)),   # -Y
    ]
    return faces


def write_obj(path, name, faces, radiance=None):
    """faces: list of (corners, normal). Emits v/vn/f triangles (white vertex color)."""
    lines = [f"# cornell-box/1.0 -- {name} (generated by tools/generate_cornell.py; FROZEN)"]
    if radiance is not None:
        lines.append(f"# emissive: one-sided Lambertian radiance {radiance} (emits -Y)")
    v, vn, f = [], [], []
    for corners, n in faces:
        base_v = len(v) + 1
        base_n = len(vn) + 1
        for c in corners:
            v.append("v %.6f %.6f %.6f" % c)
        vn.append("vn %.6f %.6f %.6f" % n)
        if len(corners) == 4:
            f.append(f"f {base_v}//{base_n} {base_v+1}//{base_n} {base_v+2}//{base_n}")
            f.append(f"f {base_v}//{base_n} {base_v+2}//{base_n} {base_v+3}//{base_n}")
        else:
            f.append(f"f {base_v}//{base_n} {base_v+1}//{base_n} {base_v+2}//{base_n}")
    lines += v + vn + f
    with open(path, "w", newline="\n") as fp:
        fp.write("\n".join(lines) + "\n")


def sphere_faces(center, radius, slices=48, stacks=32):
    cx, cy, cz = center
    faces = []
    def pt(s, t):
        phi = math.pi * s / stacks
        theta = 2.0 * math.pi * t / slices
        n = (math.sin(phi) * math.cos(theta), math.cos(phi), math.sin(phi) * math.sin(theta))
        return (cx + radius * n[0], cy + radius * n[1], cz + radius * n[2]), n
    for s in range(stacks):
        for t in range(slices):
            a, na = pt(s, t)
            b, nb = pt(s + 1, t)
            c, nc = pt(s + 1, t + 1)
            d, nd = pt(s, t + 1)
            # Winding is CCW seen from OUTSIDE (r2, M3.3.1): the (a,b,c)
            # order the rings naturally suggest is CW from outside -- the
            # orientation validator caught it (geometric normal pointed
            # INTO the sphere against the authored outward vn).
            if s == 0:
                # top cap: a == d == pole; emit the fan (pole, ring1_t+1, ring1_t)
                faces.append(([a, c, b], na))
            elif s == stacks - 1:
                # bottom cap: b == c == pole; emit (ring_t, ring_t+1, pole)
                faces.append(([a, d, b], na))
            else:
                faces.append(([a, c, b], na))
                faces.append(([a, d, c], na))
    return faces


def wall_quad(p0, p1, p2, p3, normal):
    return [((p0, p1, p2, p3), normal)]


def build_geometry():
    """name -> (faces, radiance or None) for every solid in the standard."""
    hx, ym, hz = BOX_HALF_X, BOX_Y_MAX, BOX_HALF_Z
    geo = {}

    # Room: five quads (front open). Orientation contract (r2, M3.3.1):
    # normals AND winding face the room INTERIOR -- the rasterizer shades
    # one-sided, so a room surface whose normal leaves the room is a black
    # surface. Each corner order below is CCW as seen from inside the room,
    # i.e. cross(v1-v0, v2-v0) equals the authored normal (validate_geometry
    # enforces this on every generation; bench_tests re-pins it on build).
    geo["wall_left"]  = (wall_quad((-hx, 0,  hz), (-hx, 0, -hz), (-hx, ym, -hz), (-hx, ym,  hz), ( 1, 0, 0)), None)
    geo["wall_right"] = (wall_quad(( hx, 0, -hz), ( hx, 0,  hz), ( hx, ym,  hz), ( hx, ym, -hz), (-1, 0, 0)), None)
    geo["wall_back"]  = (wall_quad((-hx, 0, -hz), ( hx, 0, -hz), ( hx, ym, -hz), (-hx, ym, -hz), (0, 0,  1)), None)
    geo["floor"]      = (wall_quad((-hx, 0,  hz), ( hx, 0,  hz), ( hx, 0, -hz), (-hx, 0, -hz), (0,  1, 0)), None)
    geo["ceiling"]    = (wall_quad((-hx, ym, -hz), ( hx, ym, -hz), ( hx, ym,  hz), (-hx, ym,  hz), (0, -1, 0)), None)

    # Emitter quad, one-sided (emits -Y); single quad facing down.
    e = EMITTER
    geo["ceiling_emitter"] = (
        wall_quad((e["min"][0], e["min"][1], e["min"][2]),
                  (e["max"][0], e["min"][1], e["min"][2]),
                  (e["max"][0], e["min"][1], e["max"][2]),
                  (e["min"][0], e["min"][1], e["max"][2]), (0, -1, 0)),
        e["radiance"])

    for blk in (TALL_BLOCK, SHORT_BLOCK, BAFFLE):
        geo[blk["name"]] = (aabb_faces(blk["min"], blk["max"]), None)

    for sph in SPHERES:
        geo[sph["name"]] = (sphere_faces(sph["center"], sph["radius"]), None)

    return geo


# Material assignments per mesh (engine metallic-roughness; the emitter is
# unlit-emissive, not a PBR material).
MATERIALS = {
    "wall_left":  {"albedo": ALBEDO_RED,   "roughness": 0.90, "metalness": 0.0},
    "wall_right": {"albedo": ALBEDO_GREEN, "roughness": 0.90, "metalness": 0.0},
    "wall_back":  {"albedo": ALBEDO_WHITE, "roughness": 0.90, "metalness": 0.0},
    "floor":      {"albedo": ALBEDO_WHITE, "roughness": 0.90, "metalness": 0.0},
    "ceiling":    {"albedo": ALBEDO_WHITE, "roughness": 0.90, "metalness": 0.0},
    "block_tall": {"albedo": ALBEDO_WHITE, "roughness": 0.90, "metalness": 0.0},
    "block_short": {"albedo": ALBEDO_WHITE, "roughness": 0.90, "metalness": 0.0},
    "baffle":     {"albedo": ALBEDO_WHITE, "roughness": 0.90, "metalness": 0.0},
}
for sph in SPHERES:
    MATERIALS[sph["name"]] = {"albedo": sph["albedo"], "roughness": sph["roughness"],
                              "metalness": sph["metalness"]}

VARIANTS = {
    "cornell01": {
        "title": "CBox-01 baseline",
        "tests": "direct lighting, diffuse response, color balance, falloff, tonemap "
                 "(shadowing is measured as a GAP until the shadows milestone lands)",
        "meshes": ["floor", "ceiling", "wall_left", "wall_right", "wall_back",
                   "block_tall", "block_short", "ceiling_emitter"],
    },
    "cornell02": {
        "title": "CBox-02 reflective materials",
        "tests": "GGX, Fresnel, roughness, specular environment (placeholder quality "
                 "gap expected: no IBL yet -- the mirror shows lights, the reference shows the room)",
        "meshes": ["floor", "ceiling", "wall_left", "wall_right", "wall_back",
                   "block_tall", "block_short", "sphere_mirror", "sphere_gold",
                   "sphere_dielectric", "ceiling_emitter"],
    },
    "cornell03": {
        "title": "CBox-03 indirect illumination",
        "tests": "color bleeding / interreflection (the baffle blocks the direct "
                 "view of the emitter; the rasterizer has no shadows or GI yet, so it "
                 "leaks direct light -- the reference shows bounce-only light)",
        "meshes": ["floor", "ceiling", "wall_left", "wall_right", "wall_back",
                   "block_tall", "block_short", "baffle", "ceiling_emitter"],
    },
}


def _cross(a, b):
    return (a[1]*b[2] - a[2]*b[1],
            a[2]*b[0] - a[0]*b[2],
            a[0]*b[1] - a[1]*b[0])


def _sub(a, b):
    return (a[0]-b[0], a[1]-b[1], a[2]-b[2])


def _dot(a, b):
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]


# Which side must a solid's normals face? "in" -> toward the room center
# (the room shell), "out" -> away from the solid's own center (blocks,
# baffle, spheres -- they sit inside the room, their outward side is the
# room). The emitter is special-cased: one-sided, emits -Y.
ROOM_CENTER = (0.0, BOX_Y_MAX * 0.5, 0.0)


def validate_geometry(geo):
    """Fail loudly if orientation ever drifts again (r1 bug class, M3.3.1).

    Checks, per solid:
      1. winding self-consistency: cross(v1-v0, v2-v0) . vn > 0 for every
         face (three r1 quads violated this against their own vn);
      2. normals face the side the standard requires (room shell -> in,
         solids -> out, emitter -> exactly -Y).
    """
    inward = {"floor", "ceiling", "wall_left", "wall_right", "wall_back"}
    sphere_names = {s["name"] for s in SPHERES}
    for name, (faces, radiance) in sorted(geo.items()):
        # Flat faces: winding normal must EQUAL the authored normal (they are
        # axis-aligned, so demand near-coplanarity). Curved tessellations
        # (spheres): vertex normals legitimately deviate from the face plane
        # by the subtended angle -- demand the same hemisphere only.
        ndot_min = 1e-6 if name in sphere_names else 0.999
        for corners, n in faces:
            gn = _cross(_sub(corners[1], corners[0]), _sub(corners[2], corners[0]))
            gl = math.sqrt(_dot(gn, gn))
            assert gl > 1e-9, "%s: degenerate face" % name
            gn = (gn[0] / gl, gn[1] / gl, gn[2] / gl)
            ndot = _dot(gn, n)
            assert ndot > ndot_min, (
                "%s: winding contradicts authored normal (dot %.6f)" % (name, ndot))
            fc = tuple(sum(c[a] for c in corners) / len(corners) for a in range(3))
            if radiance is not None:
                assert n == (0.0, -1.0, 0.0) or n == (0, -1, 0), \
                    "%s: emitter must emit -Y" % name
            elif name in inward:
                toward = _sub(ROOM_CENTER, fc)
                assert _dot(n, toward) > 0.0, \
                    "%s: room surface normal must face the room interior" % name
            else:
                center = {"block_tall": TALL_BLOCK, "block_short": SHORT_BLOCK,
                          "baffle": BAFFLE}.get(name)
                if center is not None:
                    bc = tuple((center["min"][a] + center["max"][a]) * 0.5 for a in range(3))
                    assert _dot(n, _sub(fc, bc)) > 0.0, \
                        "%s: box face normal must face away from the box" % name
                else:  # sphere: vertex normals must point away from the center
                    sph = next(s for s in SPHERES if s["name"] == name)
                    assert _dot(n, _sub(corners[0], tuple(sph["center"]))) > 0.0, \
                        "%s: sphere normal must face away from the center" % name
    print("orientation check OK: %d solids, winding matches normals, "
          "room faces point inward" % len(geo))


def light_grid():
    nx, nz = LIGHT_GRID["nx"], LIGHT_GRID["nz"]
    n = nx * nz
    e = EMITTER
    area = (e["max"][0] - e["min"][0]) * (e["max"][2] - e["min"][2])
    intensity = area * e["radiance"] / n
    y = e["min"][1] + LIGHT_GRID["y_offset"]
    pts = []
    for i in range(nx):
        for j in range(nz):
            fx = (i + 0.5) / nx
            fz = (j + 0.5) / nz
            x = e["min"][0] + fx * (e["max"][0] - e["min"][0])
            z = e["min"][2] + fz * (e["max"][2] - e["min"][2])
            pts.append((x, y, z))
    return {"count": n, "intensity": intensity, "positions": pts,
            "flux_model": "Phi = pi * A * L_e; I_per_light = A * L_e / N "
                          "(engine point light: E = I / d^2, isotropic)"}


def cpp_name(s):
    return "".join(p.capitalize() if i else p for i, p in enumerate(s.split("_")))


def write_cpp_header(path):
    grid = light_grid()
    L = []
    L.append("// GENERATED by tools/generate_cornell.py from the frozen cornell-box/1.0")
    L.append("// standard. DO NOT EDIT BY HAND and DO NOT TWEAK NUMBERS: this scene is")
    L.append("// the renderer's permanent laboratory (see benchmarks/cornell_box/README.md).")
    L.append("// M4 note: this codegen header is a stopgap until the scene/material")
    L.append("// abstraction can load benchmarks/cornell_box/scene.json directly.")
    L.append("#pragma once")
    L.append("")
    L.append("#include <array>")
    L.append("#include <cstdint>")
    L.append("")
    L.append("namespace cornell {")
    L.append("")
    L.append("inline constexpr char kStandard[] = \"%s\";" % STANDARD)
    L.append("inline constexpr float kExposure = %.4ff;" % EXPOSURE)
    L.append("")
    L.append("struct MeshDef { const char* file; const char* material; };")
    L.append("struct MaterialDef { float albedo[3]; float roughness; float metalness; };")
    L.append("struct MaterialEntry { const char* name; MaterialDef def; };")
    L.append("struct VariantDef { const char* name; const char* title;")
    L.append("                    std::uint32_t meshCount; const MeshDef* meshes;")
    L.append("                    std::uint32_t lightCount; };")
    L.append("")
    # Materials
    L.append("// clang-format off")
    mat_names = sorted(MATERIALS.keys())
    for m in mat_names:
        mm = MATERIALS[m]
        L.append("inline constexpr MaterialDef kMat_%s {" % cpp_name(m))
        L.append("    { %.4ff, %.4ff, %.4ff }, %.4ff, %.4ff };" %
                 (mm["albedo"][0], mm["albedo"][1], mm["albedo"][2],
                  mm["roughness"], mm["metalness"]))
    L.append("// Name -> material lookup (walked once at load time).")
    L.append("inline constexpr MaterialEntry kMaterialTable[] = {")
    for m in mat_names:
        mm = MATERIALS[m]
        L.append('    { "%s", { { %.4ff, %.4ff, %.4ff }, %.4ff, %.4ff } },' %
                 (m, mm["albedo"][0], mm["albedo"][1], mm["albedo"][2],
                  mm["roughness"], mm["metalness"]))
    L.append("};")
    L.append("inline constexpr std::uint32_t kMaterialCount = %du;" % len(mat_names))
    L.append("")
    # Emitter
    e = EMITTER
    L.append("// Emitter quad (unlit emissive draw) + reference radiance.")
    L.append("inline constexpr float kEmitterMin[3] = { %.4ff, %.4ff, %.4ff };" % e["min"])
    L.append("inline constexpr float kEmitterMax[3] = { %.4ff, %.4ff, %.4ff };" % e["max"])
    L.append("inline constexpr float kEmitterRadiance = %.4ff;   // L_e, linear" % e["radiance"])
    L.append("")
    L.append("// Camera (engine convention: yaw 0 looks down -Z).")
    L.append("inline constexpr float kCameraPosition[3] = { %.4ff, %.4ff, %.4ff };" % CAMERA["position"])
    L.append("inline constexpr float kCameraYawRadians   = %.6ff;" % CAMERA["yaw"])
    L.append("inline constexpr float kCameraPitchRadians = %.6ff;" % CAMERA["pitch"])
    L.append("inline constexpr float kCameraFovYRadians  = %.6ff;  // %.1f degrees" % (math.radians(CAMERA["fovY_degrees"]), CAMERA["fovY_degrees"]))
    L.append("inline constexpr float kCameraNear = %.3ff;" % CAMERA["near"])
    L.append("inline constexpr float kCameraFar  = %.2ff;" % CAMERA["far"])
    L.append("")
    L.append("// Point-light grid: rasterizer approximation of the area emitter.")
    L.append("inline constexpr std::uint32_t kLightCount = %du;" % grid["count"])
    L.append("inline constexpr float kLightIntensity = %.6ff;" % grid["intensity"])
    L.append("inline constexpr std::array<float, 3 * %du> kLightPositions { {" % grid["count"])
    for (x, y, z) in grid["positions"]:
        L.append("    %.4ff, %.4ff, %.4ff," % (x, y, z))
    L.append("} };")
    L.append("")
    # Mesh triangle-count pins (regression: regeneration must be identical)
    geo = build_geometry()
    tri = {name: sum(len(f[0]) - 2 for f in faces) for name, (faces, _) in geo.items()}
    L.append("// Pinned triangle counts (bench_tests fails if generation drifts).")
    for name in sorted(tri):
        L.append("inline constexpr std::uint32_t kTris_%s = %uu;" % (cpp_name(name), tri[name]))
    L.append("")
    # Variants
    for vname, vdef in VARIANTS.items():
        L.append("inline constexpr MeshDef kMeshes_%s[] = {" % vname)
        for m in vdef["meshes"]:
            L.append("    { \"%s.obj\", \"%s\" }," % (m, m))
        L.append("};")
        L.append("inline constexpr VariantDef kVariant_%s {" % vname)
        L.append("    \"%s\", \"%s\", %du, kMeshes_%s, %du };" %
                 (vname, vdef["title"], len(vdef["meshes"]), vname, grid["count"]))
        L.append("")
    L.append("inline constexpr const VariantDef* kVariants[] = { &kVariant_cornell01, &kVariant_cornell02, &kVariant_cornell03 };")
    L.append("inline constexpr std::uint32_t kVariantCount = 3u;")
    L.append("")
    L.append("} // namespace cornell")
    with open(path, "w", newline="\n") as fp:
        fp.write("\n".join(L) + "\n")


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    bench = os.path.join(root, "benchmarks", "cornell_box")
    geo_dir = os.path.join(bench, "geometry")
    os.makedirs(geo_dir, exist_ok=True)

    geo = build_geometry()
    validate_geometry(geo)
    for name, (faces, radiance) in sorted(geo.items()):
        write_obj(os.path.join(geo_dir, name + ".obj"), name, faces, radiance)
        print("wrote geometry/%s.obj (%d triangles)" %
              (name, sum(len(f[0]) - 2 for f in faces)))

    grid = light_grid()
    scene = {
        "standard": STANDARD,
        "frozen": "M3.3 -- never modify geometry, camera, lights, materials, or exposure; "
                  "renderer changes are measured against this scene",
        "units": "meters, Y-up, engine convention (yaw 0 looks -Z)",
        "provenance": "synthetic Cornell Box variant, meter scale; reflectances from the "
                      "pbrt book cornell_box scene (white 0.725/0.71/0.68, red 0.63/0.065/0.05, "
                      "green 0.14/0.45/0.091); layout inspired by the classic open-front box",
        "geometry_revision": "r2 (M3.3.1): room quads' normals/winding face the room "
                      "interior; r1 authored them outward and the one-sided rasterizer "
                      "rendered the room black. Positions/sizes/materials unchanged.",
        "box": {"half_x": BOX_HALF_X, "y_max": BOX_Y_MAX, "half_z": BOX_HALF_Z,
                "front": "open (+Z)"},
        "camera": CAMERA,
        "exposure": EXPOSURE,
        "ambient": {"sky": [0.0, 0.0, 0.0], "ground": [0.0, 0.0, 0.0],
                    "why": "the box is a closed light-transport system; fake ambient "
                           "would disguise the GI gap the benchmark exists to measure"},
        "emitter": {"min": EMITTER["min"], "max": EMITTER["max"], "one_sided": "-Y",
                    "radiance": EMITTER["radiance"], "color": EMITTER["color"]},
        "light_grid_rasterizer_approximation": grid,
        "materials": MATERIALS,
        "geometry_dir": "benchmarks/cornell_box/geometry",
        "reference": "tools/reference_pathtracer.py renders this scene with the SAME "
                     "BRDF (Lambert + Cook-Torrance GGX/Smith/Schlick) plus true area-light "
                     "sampling and full bounces; output goes through the SAME exposure/ACES/"
                     "sRGB transform. It is the 'trusted reference' of the two-axis benchmark.",
        "variants": {
            name: {
                "title": v["title"],
                "tests": v["tests"],
                "meshes": v["meshes"],
            } for name, v in VARIANTS.items()
        },
    }
    scene_path = os.path.join(bench, "scene.json")
    with open(scene_path, "w", newline="\n") as fp:
        json.dump(scene, fp, indent=2, sort_keys=False)
        fp.write("\n")
    print("wrote scene.json")

    write_cpp_header(os.path.join(root, "sandbox", "src", "cornell_scene_gen.h"))
    print("wrote sandbox/src/cornell_scene_gen.h")

    # Self-check: the point-light grid must reconstruct the emitter's total
    # flux under the documented model.
    flux = math.pi * (EMITTER["max"][0] - EMITTER["min"][0]) * (EMITTER["max"][2] - EMITTER["min"][2]) * EMITTER["radiance"]
    grid_flux = math.pi * grid["count"] * grid["intensity"]
    assert abs(flux - grid_flux) < 1e-9, "grid flux mismatch"
    print("flux check OK: emitter %.6f == grid %.6f" % (flux, grid_flux))


if __name__ == "__main__":
    main()
