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
| M4.0.9 (parallax penumbra window + centroid experiment) | **52.48** (shipped default config; shot md5 `209fe2941ef779f65ca202a15ec480be`) | *to measure* (default-config runs on cornell02/03) | *to measure* | *to measure* | *to measure* | The DEFAULT soft window form was upgraded in place (`w(t) = pitch·t/(1−t)`), then its scale was **re-derived from the hardware matrix**: `kShadowPenumbra = 0.5·kLightPitch = 0.1625`. **Full 8-row matrix measured** (first hardware pass, Debug build, 960×540, 300 frames; similarity via `benchmark_compare` vs `cbox01_clean`, GPU timer avg). Similarity axis: binary pin **0.52630** / edge 80.069 (md5 `949d61ab…`, harness 19/0/2 ACCEPTED — the FIRST binary md5 ever recorded; M4.0.6–8 shipped no rows) · half-pitch **0.52479** / 80.182 · full-pitch 0.52382 / 80.272 · double-pitch 0.52199 / 80.423 · centroid S=1.175 0.52370 / 81.389 · centroid binary 0.52321 / 81.765 · jitter ±0.00001 vs its no-jitter twin (SSIM-neutral, both lattices). Cost axis: binary 1.237 ms GPU · half-pitch 2.141 · full-pitch 2.266 · double 2.316 · centroid 0.719 · centroid-binary 0.553. **Verdicts (pre-registered rules, docs/SHADOW_EDGE_REFERENCES.md):** (1) the emitter-extent window is FALSIFIED — similarity degrades monotonically with the scale because the 4×4 grid's own parallax already synthesizes the physical band; the window's job is de-quantizing each edge against the march cadence → default = half pitch ≈ 2× kMarchStep; (2) binary is the metric king — the SSIM axis cannot see the staircase under the GI gap (all-row spread 0.43 pts; raw-vs-clean noise ceiling 54.32), the soft default is a recorded perceptual-polish trade; (3) the centroid experiment closed WITHOUT a similarity win (+1.2 edge RMSE = the pinned lateral blindness, first hardware confirmation) despite −42% GPU — kept as the M5 prototype; (4) jitter is SSIM-neutral on a still image — kept default-OFF for the M8 TAA slot. History: the first hardware pass ran with the full-pitch default and POSIX-only `/tmp` capture paths (all images failed to write; ab5 measured nothing — jitter was centroid-only in that build). Do NOT compare FPS against the M4.0.5 row (different build config); GPU ms is the cross-entry metric. |
| M4.0.9.1 (analytic lateral half-plane) | *pending hardware* | | | | | **What changed:** the centroid soft path gains the analytic lateral half-plane — the occluder set (the same convex prisms/spheres the heightfield rasterizes, built from the loaded meshes' AABBs) is passed as uniforms and each march sample grades the signed ground distance to the nearest eligible primitive: `g_lat = clamp(0.5 + r/(E_perp·min(t,tCap)), 0, 1)`, min-combined with the vertical grade. This CLOSES the pinned lateral blindness — the "very sharp edges" field verdict (the lateral silhouette was a full cliff: receivers whose centroid ray misses every real column never graded; no penumbra width fixes a cliff, which is why shorter penumbra never smoothed). Zero new texture taps (pure ALU), continuous at every width; the early-out gains the correctness-driven `B(t) ≥ 0` gate and a value-neutral vis==0 break. Float64 model: the M4.0.9 blind receiver grades 0.5689 (was exactly 1.0), monotone brightening to 0.7172 at z=1.30. Default transport byte-identical (binary pin `949d61ab…` and default pin `209fe294…` hold). **A/B lever:** `--shadow-lateral 0` reproduces the M4.0.9 centroid march bit-for-bit — its similarity cell MUST equal the M4.0.9 centroid row (0.52370 / edge 81.389) as a cross-build consistency check. Expected cost: ~0.72–0.80 ms GPU (≤ 4 boxes + ≤ 4 spheres of SDF ALU per sample, no fetches; deep-umbra pixels get cheaper from the vis==0 break). Adjudication of the two external reviews that triggered this milestone: window-asymptotics claim stale (already the M4.0.9 shipped form), PCSS-at-blocker rejected (drops non-argmin occluders in overlap zones), backprojection = the M5 headline, `int steps` lattice-phase seam queued. See docs/SHADOW_EDGE_REFERENCES.md, M4.0.9.1 section. |

| M5.0 (true area light: exact visible-patch form factor + backprojection) | *pending hardware* | | | | | **What changed:** the transport itself — the emitter stops being the 16-point quadrature and becomes the frozen 1.30 × 1.05 m patch (L_e = 12). DIFFUSE = the Arvo form factor of the VISIBLE patch polygon (emitter rect minus each occluder's backprojected region — the occluder projected through the receiver onto the emitter plane — subtracted by the first-outside-edge piece decomposition; exact unions, no double-count). Box regions exact (the radial sweep of the [ylo,yhi] band; the cbox03 baffle's under-pass survives exactly); sphere regions = the tangent cone sampled by 33 direction-space directions (one-sided under-block ≤ 0.9% of K end-to-end, documented). SPECULAR = representative-point GGX with the VOS solid angle. Zero texture taps, zero march, zero jitter, C1-continuous penumbrae, contact hardening + emitter anisotropy for free. Validated against float64 brute force BEFORE any GLSL (scripts/check_area_model.py; the naive per-blocker product this design replaced over-darkened a cbox03 overlap band by 18.4%). CPU mirror pinned in bench_tests (197 → 225 checks). **A/B lever:** `--area-light 0` reproduces the exact M4.0.9.1 transport — ab9 MUST reproduce the default pin `209fe294…` byte-for-bit as the cross-build check. Expected cost: ≤ the centroid path (pure ALU; the 16-march and centroid transports both did hundreds of texture taps per pixel — this does zero), the first cost drop since the centroid experiment. Expect the similarity axis to move on the falloff (area integration vs point quadrature) — direction = toward the reference, magnitude measured, not assumed. See docs/SHADOW_EDGE_REFERENCES.md, M5.0 section. |
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
# M4.0.9 A/B matrix (the parallax window is ON by default; every row records
# similarity + edge/shadow RMSE + GPU ms + md5):
#   1. binary replay (regression pin): --shadow-penumbra 0 must byte-match
#      the M4.0.6 binary run's md5 (the M4.0.9 span collapses to the legacy
#      early-out at scale 0 -- structural, port-pinned);
#   2. M4.0.9 first default (parallax window, scale = grid pitch 0.325):
#      SUPERSEDED after this matrix measured monotone degradation with the
#      scale -- the shipped default is row 3a's half-pitch 0.1625 (0.325
#      stays reachable via --shadow-penumbra 0.325). The original
#      expectation ("dominates every earlier soft row on edge/shadow
#      RMSE -- the window now spans the physical band the reference
#      integrates over") was itself falsified: binary leads the edge axis;
#   3. window bracket: --shadow-penumbra 0.1625 (half pitch) and 0.65
#      (double pitch) -- DECIDED the default scale on hardware (0.1625,
#      monotone best);
#   4. the centroid EXPERIMENT rows (--shadow-centroid 1; plus
#      --shadow-light-size 0.65 / 2.0 brackets, and --shadow-centroid 1
#      --shadow-light-size 0 for the binary-centroid cost row): measured
#      for the 16x march-cost reduction vs the documented lateral-band
#      loss -- NOT candidates for the default without a similarity win;
#   5. jitter diagnostic (--shadow-jitter 1, with and without the
#      centroid experiment): grain vs SSIM on a STILL image -- expected to
#      regress SSIM without TAA; the row quantifies the TAA payoff.
# HARDWARE NOTE (first M4.0.9 pass): /tmp does not exist on Windows cmd and
#   fopen() will not create directories -- all seven captures failed to
#   write ("Cannot open ... for writing") while the perf numbers printed
#   fine. Write captures into out/ under the repo root (create it first) or
#   any other EXISTING directory:
mkdir -p out                     # bash/WSL -- Windows cmd:  mkdir out
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 --shadow-penumbra 0                          --out out/m9_ab1_binary.ppm
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540                                              --out out/m9_ab2_default.ppm
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 --shadow-penumbra 0.1625                     --out out/m9_ab3a_halfpitch.ppm
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 --shadow-penumbra 0.65                       --out out/m9_ab3b_doublepitch.ppm
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 --shadow-centroid 1                          --out out/m9_ab4_centroid.ppm
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 --shadow-centroid 1 --shadow-light-size 0    --out out/m9_ab4b_centroid_binary.ppm
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 --shadow-jitter 1                            --out out/m9_ab5_jitter.ppm
./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 --shadow-centroid 1 --shadow-jitter 1        --out out/m9_ab5b_centroid_jitter.ppm
```

```bash
# M4.0.9.1 rows (the lateral half-plane; run whenever the centroid config is
# next measured — no new matrix required). ab7 is the new default centroid
# behavior; ab8 is the A/B lever and MUST reproduce the M4.0.9 centroid row's
# similarity (0.52370 / 81.389) bit-for-bit as the cross-build check.
#
# ./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 \
#     --shadow-centroid 1                                        --out out/m91_ab7_centroid_lateral.ppm
# ./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 \
#     --shadow-centroid 1 --shadow-lateral 0                     --out out/m91_ab8_centroid_409.ppm
# python3 tools/verify_cornell_shot.py out/m91_ab7_centroid_lateral.ppm --shadow-centroid 1 --shadow-lateral 1
# python3 tools/verify_cornell_shot.py out/m91_ab8_centroid_409.ppm    --shadow-centroid 1 --shadow-lateral 0
# python3 tools/benchmark_compare.py out/m91_ab7_centroid_lateral.ppm benchmarks/cornell_box/reference/cbox01_clean.ppm
# python3 tools/benchmark_compare.py out/m91_ab8_centroid_409.ppm    benchmarks/cornell_box/reference/cbox01_clean.ppm
```

```bash
# M5.0 rows (the area-light transport; the first default-transport change
# since M4.0.9). ab9 is the legacy cross-build check: --area-light 0 keeps
# the M4.0.9.1 default transport and MUST reproduce the default pin
# 209fe2941ef779f65ca202a15ec480be byte-for-bit. ab10 is the new default
# (record its md5 as the M5.0 default pin). ab11/ab12 extend the similarity
# axis to the other variants (cbox02 exercises the sphere cones).
#
# ./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 #     --area-light 0                                             --out out/m50_ab9_legacy.ppm
# ./build/bin/sandbox --scene cornell01 --benchmark 300 --width 960 --height 540 #                                                                --out out/m50_ab10_area.ppm
# ./build/bin/sandbox --scene cornell02 --benchmark 300 --width 960 --height 540 #                                                                --out out/m50_ab11_area_c02.ppm
# ./build/bin/sandbox --scene cornell03 --benchmark 300 --width 960 --height 540 #                                                                --out out/m50_ab12_area_c03.ppm
# python3 tools/verify_cornell_shot.py out/m50_ab9_legacy.ppm  --area-light 0
# python3 tools/verify_cornell_shot.py out/m50_ab10_area.ppm   --area-light 1
# python3 tools/benchmark_compare.py out/m50_ab9_legacy.ppm  benchmarks/cornell_box/reference/cbox01_clean.ppm
# python3 tools/benchmark_compare.py out/m50_ab10_area.ppm   benchmarks/cornell_box/reference/cbox01_clean.ppm
# python3 tools/benchmark_compare.py out/m50_ab11_area_c02.ppm benchmarks/cornell_box/reference/cbox02_clean.ppm
# python3 tools/benchmark_compare.py out/m50_ab12_area_c03.ppm benchmarks/cornell_box/reference/cbox03_clean.ppm
```

M5.0.1 update (exact-reject fast paths; transport math unchanged): the ab
rows above remain the protocol, to be measured on an IDLE machine — the
110-vs-475 field report was taken while the box was otherwise busy, so
treat absolute fps as noise and the M5.0.1-vs-M5.0 A/B (same command, new
build) as the signal. ab9's `--area-light 0` pin is untouched by 0.5.1
(the legacy branch was not modified); ab10 records the M5.0.1 default pin.

Row template:

```
| Mx.y | <sim> | <sim> | <sim> | <fps> | <fps> | <what changed, what moved and why> |
```
