# Cornell Box Ledger — two-axis results per milestone

The research question: **can we increase visual agreement with a trusted
reference while maintaining a high frame rate?**

- Axis 1 — **similarity** (0–100): SSIM against the reference path trace,
  via `tools/benchmark_compare.py` at the frozen protocol (960×540).
- Axis 2 — **cost**: FPS / frame ms from `sandbox --benchmark N` (VSync off,
  warmup discarded), plus CPU/GPU frame ms, draw calls, triangles, lights.

One row per milestone. **Append only.** The reference images and the scene
never change; if they ever do, that is a re-baselining event and every row
above the change gets marked as measured against the old standard.

---

## Reference set (frozen)

| Artifact | Value |
|---|---|
| Standard | `cornell-box/1.0` (see `README.md`, pinned by `bench_tests`) |
| Reference renderer | `tools/reference_pathtracer.py` — same BRDF (Lambert + Cook-Torrance GGX / exact height-correlated Smith / Schlick, same alpha floor) + same display transform (exposure 1.0 → ACES → sRGB); ground-truth transport: true area-light NEE + visibility, 8 bounces, GGX lobe sampling |
| Resolution / samples | 960×540; cbox01 64 spp, cbox02 80 spp, cbox03 64 spp; seed 7; deterministic |
| Validated signatures | cbox01: left wall 195/16/13 (red), right wall 35/147/22 (green), emitter saturates 255. cbox02: gold sphere warm (R≫B), mirror sphere spans 0→255 (dark room reflection + blown emitter spot). cbox03: dimmer + flatter than cbox01 (bounce-dominated) — all verified at generation time |
| Integrity | `cbox01_reference.ppm` md5 `7aa5358ece5b70ce240b88f21596cd7c` · `cbox02_reference.ppm` md5 `6b01a66f41c5c1ecb0ae19d8da5660c6` · `cbox03_reference.ppm` md5 `ede263ebc02418ef254b6c1933c2001a` |
| Generated | M3.3 — never regenerate (see README rules) |

## Known gaps at M3.3 (what the first rows will measure)

> **Geometry note (M3.3.1):** the first on-hardware render exposed an r1
> authoring bug — every room quad's normal pointed away from the room
> interior, and the one-sided rasterizer rendered the room black (96.5%
> black pixels). Geometry r2 corrects orientation only (see README);
> constants and reference images above are unchanged, and no ledger rows
> existed yet — **the first row below is measured against r2 geometry.**
> Acceptance gate before recording: `python3 tools/verify_cornell_shot.py
> <shot.ppm>` must print ACCEPTED.

The rasterizer currently has **no shadows, no GI, no area lights** (the
emitter is a flux-preserving 4×4 point-light grid), and **no environment
specular** (the M3.2 ambient placeholder is zeroed for Cornell scenes).
Similarity will therefore start LOW — especially on CBox-03, whose reference
is bounce-only light. That is the point: the ledger quantifies exactly what
M4 (area lights), M5 (IBL), M6 (shadows), and M8 (indirect) must climb.

## Ledger

| Milestone | CBox-01 similarity | CBox-02 similarity | CBox-03 similarity | FPS 720p | FPS 1080p | Notes |
|---|---|---|---|---|---|---|
| M3.3 (first baseline) | *to measure on hardware* | *to measure on hardware* | *to measure on hardware* | *to measure* | *to measure* | Headless build environment cannot open a GL context; run the protocol below on real hardware and fill this row. |

### Protocol (copy-paste)

```bash
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 \
    --out /tmp/cbox01.ppm --report /tmp/cbox01_report.txt
python3 tools/benchmark_compare.py /tmp/cbox01.ppm \
    benchmarks/cornell_box/reference/cbox01_reference.ppm
# repeat with cornell02 / cornell03, then the perf-only runs:
./build/bin/sandbox --scene cornell01 --benchmark 1000 --width 1280 --height 720 --report /tmp/perf720.txt
./build/bin/sandbox --scene cornell01 --benchmark 1000 --width 1920 --height 1080 --report /tmp/perf1080.txt
```

Row template:

```
| Mx.y | <sim> | <sim> | <sim> | <fps> | <fps> | <what changed, what moved and why> |
```
