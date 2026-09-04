# Cornell Box Ledger — two-axis results per milestone

The research question: **can we increase visual agreement with a trusted
reference while maintaining a high frame rate?**

- Axis 1 — **similarity** (0–100): SSIM against the CLEAN reference path
  trace, via `tools/benchmark_compare.py` at the frozen protocol (960×540).
  The full metric suite (RMSE, MAE, SSIM, ΔE2000, edge/shadow RMSE) prints
  alongside; SSIM stays the ledger headline.
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
| **Clean reference (measurement target, since M4)** | `reference/cbox0*_clean.ppm` — 960×540, **320 spp**, seed 7, per-spp sample streams (`seed*1000003 + spp_index`), 8 bounces. Chunked accumulation reproduces byte-exact results (verified); md5s below. Generated M4, never regenerate |
| Raw reference (provenance archive) | `reference/cbox0*_reference.ppm` — 960×540; cbox01 64 spp, cbox02 80 spp, cbox03 64 spp; single sequential stream, seed 7. Kept for history; its MC noise measurably depressed SSIM (raw-vs-clean self-check: similarity 54.32), which is why the clean set became the measurement target |
| Validated signatures | cbox01: left wall 195/16/13 (red), right wall 35/147/22 (green), emitter saturates 255. cbox02: gold sphere warm (R≫B), mirror sphere spans 0→255 (dark room reflection + blown emitter spot). cbox03: dimmer + flatter than cbox01 (bounce-dominated) — all verified at generation time |
| Integrity — clean | `cbox01_clean.ppm` md5 `361e4085682f66f05d865aadada3866f` · `cbox02_clean.ppm` md5 `69acf8d01f34dac5075062c397eb60db` · `cbox03_clean.ppm` md5 `d621b9d91af6a35d7478e2f4ac55f061` |
| Integrity — raw | `cbox01_reference.ppm` md5 `7aa5358ece5b70ce240b88f21596cd7c` · `cbox02_reference.ppm` md5 `6b01a66f41c5c1ecb0ae19d8da5660c6` · `cbox03_reference.ppm` md5 `ede263ebc02418ef254b6c1933c2001a` |
| Generated | raw: M3.3; clean: M4 — never regenerate either (see README rules) |

## Known gaps at M4 (what the first rows will measure)

> **Geometry note (M3.3.1):** the first on-hardware render exposed an r1
> authoring bug — every room quad's normal pointed away from the room
> interior, and the one-sided rasterizer rendered the room black (96.5%
> black pixels). Geometry r2 corrects orientation only (see README);
> constants and reference images above are unchanged, and no ledger rows
> existed yet — **the first row below is measured against r2 geometry.**
> Acceptance gate before recording: `python3 tools/verify_cornell_shot.py
> <shot.ppm>` must print ACCEPTED.
>
> **Reference note (M4):** the measurement target moved from the raw
> 64/80-spp renders to the 320-spp clean set — a RE-BASELINING event per
> the ledger rules, clean in the only way that matters here: **no ledger
> rows existed yet**, so nothing above is invalidated. From M4 on,
> similarity is measured against `*_clean.ppm`.
>
> **Renderer note (M4):** the rasterizer gained HEIGHTFIELD SHADOWS for the
> point-light rig (`--no-shadows` restores the exact M3.3 direct-only
> behavior for A/B). Remaining known gaps unchanged: no GI, no IBL, and
> the penumbra is the 16-superposition approximation (per-pixel emitter
> integration is the M5 slot).

The rasterizer now has **heightfield shadows** for the rig (M4) but still
**no GI and no true area lights** (the emitter is a flux-preserving 4×4
point-light grid whose 16 superposed shadows approximate the penumbra), and
**no environment specular** (the M3.2 ambient placeholder is zeroed for
Cornell scenes). Similarity will therefore still sit well below 100 —
especially on CBox-03, whose reference is bounce-only light. That is the
point: the ledger quantifies exactly what M5 (area-light integration), M6
(probes/IBL), and M7+ (indirect) must climb.

## Ledger

| Milestone | CBox-01 similarity | CBox-02 similarity | CBox-03 similarity | FPS 720p | FPS 1080p | Notes |
|---|---|---|---|---|---|---|
| M3.3 (direct-only renderer) | *to measure on hardware* | *to measure on hardware* | *to measure on hardware* | *to measure* | *to measure* | Historical first row: run the CURRENT binary with `--no-shadows` and compare against the clean references (the clean set did not exist at M3.3; measuring retroactively is legitimate — the reference is fixed, the renderer mode is what varies). |
| M4 (heightfield shadows) | *to measure on hardware* | *to measure on hardware* | *to measure on hardware* | *to measure* | *to measure* | Shadows ON by default; `--no-shadows` reproduces the M3.3 row. Record both if possible. |
| M4.0.1 (first hardware attempt) | **51.32** | — | — | *not reported* | *not reported* | **REJECTED** (gate: 17/19 probes, 2 shadow probes FAIL). Shot md5 `5e549b5417311b93fe729e7989184aeb`, 960×540. Metrics: RMSE 71.88, MAE 46.13, SSIM 0.5132, ΔE2000 18.04, edge/shadow RMSE 80.03. **Diagnosis (M4.0.2):** both failing probes read byte-identical to the harness's UNSHADOWED model (umbra meas 50/6/5 vs unshadowed 51/4/2; penumbra meas 105/103/100 vs 105/103/101) — the march never blocked a single light in this run. Row kept as the honest pre-instrumentation measurement; re-measure with M4.0.2 (field verification + honest telemetry + harness failure diagnosis). |
| M4.0.2 (verification instrument) | **51.32** | — | — | *not reported* | *not reported* | **REJECTED** (same gate) — image identical to M4.0.1 as the bug predicts: SSIM 0.51318, RMSE 71.875, edge RMSE 80.030. FPS at the 960×540 compare run: 1523.6. **Instrument value:** first hardware proof the CAPTURE works — console `field verify OK`: coverage 15.65 % ∈ [10.87, 20.87], top 3.299 m (expected 3.30), intervals valid. But the verdict never reached `shadowsActive` (the out-param was never written; fixed in M4.0.3), so the caller reported FAILED, the march stayed off, and the telemetry honestly said so. Re-measure with M4.0.3. |
| M4.0.5 (march swizzle fix: first LIT march) | **52.62** | — | — | *to measure* | *to measure* | **ACCEPTED** (gate: 19 pass / 0 fail / 2 documented gaps M5+M8). Shot md5 `e830e543f6be06b9a2b58e6103fcd827`, 960×540: RMSE 72.25, MAE 45.37, SSIM 0.52622, ΔE2000 17.98, edge/shadow RMSE 80.00. FPS 1089.2 @ 960×540, 300 frames (GPU 0.583 ms avg, vsync off, warmup 30 discarded). **Root cause of the M4.0.1/M4.0.2 inert march:** `.xz` vs `.xy` swizzle on the packed footprint uniforms — uv.y went to ±inf and clamped to the field's empty edge rows, so every fetch returned an empty column forever. Capture verified again this run (coverage 15.65 %, top 3.299 m, registration probes 7/7, `--dump-heightfield` clean). Shadows now visibly land: tall-block umbra on the red wall, floor penumbra, harness shadow probes pass. **Known defect:** shadow boundaries quantized into staircase bands, pitch tracks the 0.16 m march cadence (7.4 field texels; samplers already GL_LINEAR, so filtering is ruled out) → M4.0.6 halves kMarchStep to 0.08 m as a single change. |

### Protocol (copy-paste)

```bash
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 \
    --out /tmp/cbox01.ppm --report /tmp/cbox01_report.txt
python3 tools/verify_cornell_shot.py /tmp/cbox01.ppm        # must print ACCEPTED
python3 tools/benchmark_compare.py /tmp/cbox01.ppm \
    benchmarks/cornell_box/reference/cbox01_clean.ppm
# repeat with cornell02 / cornell03, then the perf-only runs:
./build/bin/sandbox --scene cornell01 --benchmark 1000 --width 1280 --height 720 --report /tmp/perf720.txt
./build/bin/sandbox --scene cornell01 --benchmark 1000 --width 1920 --height 1080 --report /tmp/perf1080.txt
# A/B against the M3.3 direct-only renderer whenever the shadow delta matters:
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 \
    --no-shadows --out /tmp/cbox01_noshadow.ppm
# M4.0.4 shadow-defect discriminator (temporary instrument): dump the captured
# height fields through the march's sampler wiring; expect the tall block at
# x -1.55..-0.55 / z -2.05..0.35 and the short block at x 0.40..1.40 / z
# -0.70..1.70 in <prefix>_hmax.ppm (white = 3.30 m, black = empty):
./build/bin/sandbox --scene cornell01 --frames 1 --dump-heightfield /tmp/hfield \
    --out /tmp/dump_shot.ppm
# M4.0.5 GPU-side shadow-march panel (temporary instrument, NOT ledger images --
# the debug view REPLACES the shaded output). Three views from inside the PBR
# shader, for whenever layers 1-4 + the dump all pass but the image is inert:
#   vis   : black = at least one point light blocked by the march (the march's
#           verdict, independent of the BRDF). All-bright = march never blocks.
#   field : R = hMax at the fragment's own footprint uv (block FACES light up;
#           tall faces brighter than short), G = march calls / 16 (0 = march
#           never ran), B = uShadowMaxHeight / 6 (0 = early-out uniform dead).
#   uv    : the march's uv formula as RG. Correct = smooth 0->1 red (along X)
#           and green (along Z) gradients over the room. A HARD BINARY green
#           seam at z = 0 = uv.y is ±inf (the M4.0.5 .xz/.xy swizzle bug).
./build/bin/sandbox --scene cornell01 --frames 1 --shadow-debug vis   --out /tmp/dbg_vis.ppm
./build/bin/sandbox --scene cornell01 --frames 1 --shadow-debug field --out /tmp/dbg_field.ppm
./build/bin/sandbox --scene cornell01 --frames 1 --shadow-debug uv    --out /tmp/dbg_uv.ppm
# M4.0.7 soft-penumbra A/B matrix (each row = one ledger data point; record
# similarity + edge/shadow RMSE + GPU ms + md5 for every row):
#   1. binary replay (regression pin): --shadow-penumbra 0 must byte-match
#      the M4.0.6 binary run's md5 (the soft path is provably decision-
#      identical at scale 0);
#   2. soft @ 0.08 m (default build): the quality row;
#   3. soft @ 0.16 m: the "smooth AND fast" candidate — expected to dominate
#      M4.0.5 (smoother boundaries at M4.0.5's cost);
#   4. binary @ 0.16 m replay: must byte-match the M4.0.5 shot
#      (md5 e830e543f6be06b9a2b58e6103fcd827) — proves the flag machinery
#      itself is byte-neutral.
#   Tuning, only if the default window misses: --shadow-penumbra 0.015 and
#   0.06 bracket the derived 0.0299 (half grid pitch / grid height).
# M4.0.8 bracket refinement is ON by default in every soft row above
#   (docs/SHADOW_EDGE_REFERENCES.md: the refinement phase every reference
#   heightfield-ray solver has -- POM/relief mapping). It engages only where
#   the window fired, so the binary rows above are UNAFFECTED and their
#   md5-replay pins still hold. --shadow-refine 0 reproduces the pure
#   M4.0.7 soft rows exactly (the refinement A/B lever). The "smooth AND
#   fast" flagship row is row 3 (soft @ 0.16): M4.0.5's tap count plus two
#   fetch pairs on penumbra-band pixels only.
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 --shadow-penumbra 0    --out /tmp/ab1_binary008.ppm
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540                        --out /tmp/ab2_soft008.ppm
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 --shadow-step 0.16     --out /tmp/ab3_soft016.ppm
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 --shadow-penumbra 0 --shadow-step 0.16 --out /tmp/ab4_binary016.ppm
```

Row template:

```
| Mx.y | <sim> | <sim> | <sim> | <fps> | <fps> | <what changed, what moved and why> |
```
