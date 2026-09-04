# Changelog

One entry per change to **interfaces gameplay can see**. Written **before** the
change lands. Format: `MAJOR.MINOR.PATCH — <short title>` followed by the
sections Added / Changed / Removed / Deprecated / Fixed as needed.

---

## 0.4.8 — M4.0.8: bracket refinement (the reference-standard second phase of the march)

User verdict after the first lit march: "no other engine has blocky
penumbra — look up how it's done in other code." The research verdict
(docs/SHADOW_EDGE_REFERENCES.md): the user is right. Every shipped
heightfield-ray solver is two-phase — a coarse linear search brackets the
feature, a refinement phase resolves it — and GPU Gems 3 ch. 18 names our
exact defect ("the linear search ... is prone to aliasing"). Our marcher had
phase 1 (M4.0.5) and the filtered visibility signal (M4.0.7, iq's terrain
soft-shadow trick) but no refinement phase: the graded edge was computed
from the SAMPLED minimum clearance, so it inherited the march cadence as
staircase noise near silhouettes.

### Added

- **Bracket refinement in `shadowVisibility()`** (POM / relief-mapping
  family): the argmin sample's `t` is tracked, and two extra fetch pairs at
  `t_best ± half-step` re-evaluate the interval clearance against the same
  window at half-step resolution. Engaged ONLY where the window actually
  fired (`vis < 1`) — fully lit and hard-blocked pixels pay nothing. A
  refined fetch landing inside the interval returns the hard 0 (a crossing
  the cadence jumped over is a real crossing). Cost on the ledger rows:
  M4.0.5/M4.0.6 tap counts unchanged; penumbra-band pixels pay 2 fetch
  pairs per light.
- **`--shadow-refine 0|1`** (default 1 = ON; `0` = the exact M4.0.7 soft
  march, byte-for-byte). Telemetry line now reports
  `refine: on (M4.0.8) / off (exact M4.0.7)`.
- **Reference ledger**: docs/SHADOW_EDGE_REFERENCES.md — the research
  verdict with citations (GPU Gems 3 ch. 18 relaxed cone stepping;
  Tatarchuk POM + approximate soft shadows; iq terrain marching /
  SDF soft shadows; PCSS blocker→penumbra estimation; VSM moments and why
  we did NOT adopt them for a 16-light rig), the component mapping table,
  and the documented residual limits (window narrower than the cadence
  cannot fire the refinement; sub-half-step lobes need the mip/cone
  hierarchy, M4.1+ candidate).
- **`testBracketRefinement()`** (bench 163 -> 173 checks): ridge fixture
  proves the refinement strictly sharpens a sub-step clearance dip the
  0.08 m cadence sampled past; flat-bottom fixture proves the exit-edge
  sharpening when the window grows over constant clearance; binary
  neutrality pins (penumbra = 0: refine 0 vs 1 identical on umbra and lit
  rays); hard outcomes stay exact; monotone-safety pins (refinement never
  raises visibility, stays in [0, 1]).

### Changed

- `bench_tests` count 163 -> 173; VERIFICATION updated.
- The M4.0.7 A/B matrix rows in BASELINE.md now measure WITH refinement on
  by default; `--shadow-refine 0` reproduces the pure M4.0.7 soft rows.

### Preserved (regression surface untouched)

- `--shadow-penumbra 0` still byte-reproduces the M4.0.6 binary march and
  the M4.0.5 shot (`e830e543f6be06b9a2b58e6103fcd827`): the refinement is
  unreachable at penumbra 0 (structural, port-pinned).
- BRDF, light rig, bias, Cornell geometry, capture: all frozen.
- Local: sandbox + all suites build 0 errors / 0 warnings; bench 173/173,
  math 73/73, brdf 38/38, obj 24/24.

---

## 0.4.7 — M4.0.7: soft penumbra extracted from the march (smooth edges, zero extra taps)

M4.0.5's first lit march put shadows on screen but quantized: the boundary
staircase tracked the 0.16 m march cadence (7.4 field texels per sample).
M4.0.6 halved the step (0.16 -> 0.08 m) and the bands halved with it — but
the cost curve is linear in samples (GPU 0.583 -> 0.881 ms at 960x540), and
sampling density is not the only information in the march. Each sample
already computes HOW FAR the ray clears the occluder column; the binary
march threw that number away.

### Added

- **Soft penumbra in `shadowVisibility()`** (horizon-style accumulation):
  each sample reports its signed clearance `d` to the column interval
  `[hMin + bias, hMax - bias]`; the march returns the minimum of `d` against
  a penumbra window `penumbra * traveled` that grows with distance along the
  ray. A ray that merely GRAZES the interval shades partially instead of
  flipping hard — the staircase boundary becomes a graded edge with zero
  extra texture taps (a few ALU per sample). Deep umbra stays hard
  (`d < 0` returns 0 exactly as before), and a `2 * step` start zone keeps
  the receiver's own neighborhood binary: contact shadows stay crisp and
  top-surface receivers cannot self-shade.
- **Derived, not tuned:** the default scale is `kShadowPenumbra =
  0.5 * kLightPitch / kLightHeight = 0.5 * 0.325 / 5.44 ~= 0.0299` — half
  the frozen 4x4 grid pitch over the grid height, i.e. the reconstruction
  blur of the point-light quadrature of the area emitter. The 16 hard edges
  were always a quadrature; this restores the continuum the rig approximates.
- **`--shadow-penumbra <scale>`** and **`--shadow-step <meters>`** CLI
  overrides (both default to the derived constants; overrides exist so the
  ledger's A/B matrix runs without rebuilds). `--shadow-penumbra 0` disables
  the soft path and must reproduce the M4.0.6 binary image byte-for-byte
  (regression + A/B pin; the binary decision and the early-out are provably
  unchanged — `max(a, b) < 0` iff both old inequalities held). Telemetry
  gains a `shadow penumbra:` line with the active scale and march step
  (report `lines[]` 13 -> 14).

### Changed

- The march's early-out is penumbra-aware: it breaks once the ray's global
  clearance bound `rayY - (maxHeight - bias)` exceeds the largest window the
  ray can ever have (`penumbra * segLen`). With penumbra = 0 this breaks one
  bias earlier than M4.0.6 — strictly on samples whose interval test could
  never fire — so the binary image is unchanged.
- `bench_tests` march port mirrors the soft path: **156 -> 163 checks**
  (deep-umbra hardness, exact-1 clear rays, top-receiver acne guard, a 1 m
  box graze fixture landing strictly inside (0, 1), monotonicity in the
  window scale, and the binary-reference equivalence pin).

### Evidence

- Local: bench 163/163, math 73/73, brdf 38/38, obj 24/24; build 0 errors /
  0 warnings. On-hardware: run the M4.0.7 A/B matrix in
  `benchmarks/cornell_box/BASELINE.md` (soft@0.08 default, soft@0.16 via
  `--shadow-step`-style constant override or binary replay via
  `--shadow-penumbra 0`); expected: edge/shadow RMSE down from 80.0 at
  M4.0.5, GPU within ~0.05 ms of the binary run at the same cadence.

---

## 0.4.6 — M4.0.6: march step halved (0.16 -> 0.08 m) — the boundary quantizer fix

The first lit march (M4.0.5, ledger ACCEPTED at similarity 52.62) exposed
the march cadence as the shadow boundary's dominant quantizer: the field is
21.5 mm/texel and both samplers are already GL_LINEAR, so the staircase
band pitch tracked the 0.16 m sample spacing (7.4 texels per step), not the
capture and not the filter.

### Changed

- `kMarchStep` 0.16 -> 0.08 m in the sandbox (single change; the
  `static_assert` against the cbox03 baffle's 0.2 m thickness still holds
  with 2.5x margin, so the no-leak invariant is intact). `bench_tests`
  `MarchParams::step` default and the literal invariant pin mirrored.
- Documented refinement floor: ~0.04 m (~1.9 texels) — steps finer than the
  field's texel pitch buy no information the capture can represent.

### Evidence

- Local: bench 156/156 at the new cadence. On-hardware (M4.0.6 run):
  GPU 0.583 -> 0.881 ms at 960x540 (805.8 FPS) — the marcher is ~0.3 ms of
  the frame at 0.16 m and scales linearly with sample count, which is
  precisely why M4.0.7 extracts quality from the existing taps instead.

---

## 0.4.5 — M4.0.5: shadow-march uv swizzle fix (the inert-march root cause) + GPU-side debug panel

M4.0.4's instruments cleared everything they could see: registration probes
7/7 (the field is in the correct world-space location and orientation), the
heightfield dump showed both block footprints in place through the march's
sampler wiring, telemetry said `shadows: on`, and GPU time rose by the cost
of real texture fetches (0.627 → 0.652 ms) — while the rendered image stayed
byte-identical to `--no-shadows` for the fourth consecutive run (umbra and
penumbra probes at their unshadowed values, shot md5
`5e549b5417311b93fe729e7989184aeb`). That combination — perfect field, live
sampler path, executing march, zero occlusion — left exactly one untested
surface: **how the march interprets the footprint uniforms**. The uniforms
pack (worldX, worldZ, pad) into a vec3 (no Uniform2f loader entry), but
`shadowVisibility()` unpacked the footprint with `.xz` — which reads
(worldX, pad) — instead of `.xy`. Deterministic consequences on every
driver: footprint span computed as (5.5, 0.0), `invSpan.z = 1/0 = +inf`,
`uv.y = worldZ * inf` clamped to the field's edge rows, where the capture is
floor (hMax = 0) — so `rayY < hMax - bias` could never fire, and the march
executed every fetch while returning 1.0 forever. The dump could not see it
(it computes uv from gl_FragCoord and never touches the footprint uniforms);
the registration probes could not see it (they test the CPU-side mapping
against the captured field — both correct); the bench_tests C++ port could
not see it (it implements the intended mapping, not the shader's swizzle).
Root-cause class: **a GLSL-only divergence between the shader and its CPU
port** — from now on, instruments must also look from inside the shader.

### Fixed
- `shadowVisibility()` unpacks the packed footprint uniforms with `.xy`
  (was `.xz`): `vec2 invSpan = 1.0 / (uShadowFootprintMax.xy -
  uShadowFootprintMin.xy)` and `uv = (mix(p0, p1, t) -
  uShadowFootprintMin.xy) * invSpan`. Two tokens; no march constant, bias,
  BRDF, light position, or Cornell geometry changed. The pack convention
  (worldX, worldZ, pad) is now documented at the uniform block itself.

### Added
- Sandbox `--shadow-debug vis|field|uv` (M4.0.5, temporary GPU-side
  instrument, default OFF): a `uShadowDebug` uniform drives three blatant
  views computed inside the PBR shader, so the next "every instrument
  passes but the image is inert" failure splits in one run without leaving
  the GPU. `vis` — black where any point light is blocked, bright where
  none (the march's verdict made blatant, independent of the BRDF; the
  M4.0.5 request). `field` — R: hMax fetched at the fragment's own
  footprint uv through the march's formula; G: march calls / 16 (did
  `shadowVisibility` run at all); B: uShadowMaxHeight / 6 (did the
  early-out uniform land). `uv` — the march's uv formula shown directly:
  smooth 0→1 red/green gradients when correct; a hard binary green seam at
  z = 0 is the signature of the ±inf uv.y this build fixes. Debug views
  REPLACE the shaded image — never ledger images; telemetry prints a
  `shadow-debug:` line whenever one is active.

### Evidence
- Local: rebuild clean, 0 warnings; bench 156/156, math 73/73, obj 24/24,
  brdf 38/38. The fix is a GLSL source change, so the confirming run is the
  user's re-measurement of the BASELINE.md protocol (CBox-01/02/03): the
  shadow probes must read shadowed for the first time and similarity must
  clear 51.32. The debug modes exist as independent confirmation and as the
  standing instrument if anything in the march is still inert.

## 0.4.4 — M4.0.4: field registration probes + sampler-path dump (spatial instrument)

M4.0.3 hardware run: the propagation fix worked (field verify OK, telemetry
`shadows: on (heightfield march, field verified)`) — and the image was STILL
byte-identical to M4.0.1 (same md5, umbra 50/6/5, penumbra 105/103/100). The
march is now provably enabled and provably returns 1.0 for every sample of
every pixel. Byte-identical output rules out a merely DISPLACED field (a
mirrored/transposed field would still occlude some ray somewhere and move
some pixel); the surviving worlds are (A) the captured field is spatially
misregistered in a way aggregate statistics cannot see, (B) the sampler read
path at draw time returns ~0 (wrong texture actually bound, wrong
coordinates), or (C) the march's uniforms/logic never test a real value.
M4.0.2's verifyField() proves coverage %, top, and interval validity — but a
square room makes a mirrored or X↔Z-transposed field INVISIBLE to those
statistics: same area, same top. Two instruments close that hole.

### Added
- `engine::ShadowHeightfield::readbackHeights()` (M4.0.4): raw readback of
  both captured fields (R channel as float, row 0 = minZ edge — the layout
  the sampler sees). Scene-side code can now check WHERE the content sits,
  not just how much of it there is. ReadPixels is still the only new GL
  surface.
- `engine::ShadowHeightfield::worldToTexel()` (M4.0.4, static, pure math):
  the CPU form of the GLSL march's `uv = (world.xz - footprintMin) / span`
  mapping. Shared by every diagnostic that must agree with the march;
  pinned headlessly by bench_tests (corners, center, tall-block probe
  point, outside-refusal, texel-center round trip; 133 → 156 checks).
- Sandbox REGISTRATION PROBES (M4.0.4, frozen table): after verifyField
  passes, both fields are read back and probed at frozen world points —
  tall block center (expect 3.30/0.00), short block center (1.65/0.00),
  gold sphere center for cbox02 (1.10/0.00), baffle center for cbox03
  (5.40/3.40 — the one probe whose underside is not the floor, so it
  validates the MIN capture), plus five sentinels/empties: mirror-X,
  mirror-Z, transpose, and two empty corners. The sentinels sit OUTSIDE
  every real footprint but INSIDE the footprint a flipped/swapped capture
  would have — any mirroring failure mode flips exactly one sentinel to
  occluder. One loud line per probe; any FAIL disables shadows (the same
  never-render-a-lie rule as verifyField) and telemetry says so.
- Sandbox `--dump-heightfield <prefix>` (M4.0.4, temporary diagnostic):
  renders the captured fields DIRECTLY through the march's sampler wiring —
  `bindTextures(0, 1)` + `setInt("uShadowHeightsMax"/"Min")` with the SAME
  uniform names — one texel per pixel, raw linear gray, NO tonemap, via the
  floor quad drawn with the capture's own ortho through
  `renderer.drawIndexed` (so it exercises the same GL state sequence as the
  PBR draw). Writes `<prefix>_hmax.ppm` / `<prefix>_hmin.ppm`. Reading:
  dump shows footprints in place → field + sampler path fine, the bug is in
  the march's uniforms/logic; dump black/displaced → field or sampler path.

### Evidence
- Local: rebuild clean, 0 warnings; bench 156/156, math 73/73, obj 24/24,
  brdf 38/38. The registration probes and dump are hardware-GL instruments;
  the discriminating run happens on user hardware.

---

## 0.4.3 — M4.0.3: shadow-verdict propagation bugfix (verified field never enabled the march)

The M4.0.2 instrument worked exactly as designed — and then the verdict was
thrown away. On the first M4.0.2 hardware run, `captureCornellHeightfield()`
computed `hf.verifyField(...)` and returned it, but the function's contract
is 3-state: the return value only means "a capture was made"; the
verification verdict must travel through the `bool* verifiedOut` out-param.
`*verifiedOut` was initialized `false` and never written, so a PASSING
hardware field (console proof: coverage 15.65 % ∈ [10.87, 20.87], top
3.299 m vs 3.30 expected, intervals valid) still left `shadowsActive ==
false`, the caller printed `shadow field verification FAILED`, and the
march stayed off — reproducing the M4.0.1 image bit-for-bit while the
telemetry told the truth about a condition the code itself had created.
Software bug, not a rendering/math bug: no shader, scene, capture, or test
change.

### Fixed
- `sandbox/src/main.cpp` `captureCornellHeightfield()`: the `verifyField()`
  verdict is now stored to `*verifiedOut` before returning `true`. The
  3-state contract (documented on the function) is finally in force:
  return `false` = capture unavailable; `true` + `*verifiedOut == false` =
  field failed acceptance; `true` + `*verifiedOut == true` = valid, verified
  shadow field.

### Evidence
- M4.0.2 hardware run (same inert-march image as M4.0.1, as the bug
  predicts): SSIM 0.51318, RMSE 71.875, edge RMSE 80.030 — recorded in the
  ledger as the M4.0.2 row alongside the field-verify-OK log, which is the
  first hardware proof that the capture itself works (M4.0.1 could not
  distinguish empty field from software-off).
- Local: engine + sandbox rebuild clean; `bench_tests` 133/133 (contract
  pins unaffected — the bug was in sandbox state plumbing, below the
  tested layer).

---

## 0.4.2 — M4.0.2: shadow-field verification (the inert-march instrument)

First on-hardware M4 acceptance run: 17/19 probes pass, both new shadow
probes FAIL, similarity 51.32. Local float64 diagnosis with the harness's
own frozen model: the two failing probes read byte-identical to the
UNSHADOWED model (umbra measured 50/6/5 vs unshadowed 51/4/2; penumbra
105/103/100 vs 105/103/101) — **the shadow march never blocked a single
light in that run**. Empty capture field, shadows-off mode, and black
sampler reads are byte-indistinguishable in the image, so the pipeline now
proves itself at runtime instead of claiming.

### Added
- `engine::ShadowHeightfield::verifyField()` (M4.0.2): reads back both R16F
  capture targets after the one-time capture and verifies them against the
  frozen scene (occluder coverage band from the pinned footprints, tallest
  surface 3.30/5.40 m, per-texel interval validity min ≤ max). Zero new GL
  loader entry points (ReadPixels is already engine surface; R16F content is
  read as RGBA/FLOAT). Prints one loud line either way. An inert field now
  FAILS LOUDLY at startup instead of rendering as silent light leak.
- Sandbox: `shadowsActive` now carries the field-verification verdict — a
  failed verification disables shadows rather than rendering a lie, and the
  benchmark telemetry distinguishes all four states: `off (--no-shadows)`,
  `on (heightfield march, field verified)`,
  `off (capture failed or field verify FAILED)`, demo `n/a`.
- Acceptance harness (`verify_cornell_shot.py`): failing shadow probes now
  carry a DIAGNOSIS — the unshadowed float64 model is computed at the probe
  point with its PHYSICAL albedo (the red wall for the umbra probe) and the
  failure is classified as "matches UNSHADOWED (march inert)", "partial
  leak (between models)", or "darker than shadowed (over-blocking)".
- `bench_tests`: verifyField must refuse a context-less instance (the
  check can never silently pass on an un-captured field); 132 → 133 checks.

### Fixed
- Ledger: M4.0.1 hardware row recorded honestly (similarity 51.32, REJECTED,
  diagnosis attached) — kept as the pre-instrumentation measurement.

---

## 0.4.1 — M4.0.1: MSVC build hotfix (missing `<utility>`)

First on-hardware MSVC build of the M4 tree failed to compile:
`ShadowHeightfield.cpp(64): error C2039: 'move': is not a member of 'std'`.
libstdc++ leaks `std::move` through the transitive header chain; MSVC's STL
does not, and the file included only `<cstdio>`. Portability bug, not a math
or rendering bug — no shader, scene, or test change.

### Fixed
- `engine/src/rendering/ShadowHeightfield.cpp`: added the missing
  `#include <utility>` for `std::move` (the MSVC build blocker).
- `engine/src/assets/OBJ.cpp`: added `#include <utility>` too — it used
  `std::move` while relying on `<algorithm>` leaking it (compiled on MSVC
  today, but the same latent bug class).
- Root `CMakeLists.txt`: `DOWNLOAD_EXTRACT_TIMESTAMP TRUE` on the GLFW
  FetchContent declaration, silencing the CMP0135 dev warning seen during
  configure on CMake 4.x.

---

## 0.4.0 — M4: heightfield shadows for the Cornell rig + clean-reference methodology

The second on-hardware Cornell render was structurally valid (acceptance
gate passed) but exposed the expected direct-lighting gap: hard shadows
nowhere, everything else quantified by the ledger. This milestone attacks
the two highest-leverage items from that review — **real shadowing** and a
**trustworthy measurement target** — without touching the PBR BRDF math.

### Added
- **Heightfield shadow capture** (`engine::ShadowHeightfield`, new engine
  module): TWO one-time ortho captures of the scene's occluders into R16F
  targets — the highest surface per footprint texel (camera above) and the
  lowest (camera below). Together they store each texel's vertical occluder
  INTERVAL. GL surface area unchanged: zero new loader entry points
  (TexImage2D covers R16F; the default LESS depth test is exactly the right
  keep rule for both cameras).
- **Per-light shadow march** in the PBR shader (`shadowVisibility`): the
  receiver→light segment is marched in XZ with a fixed 0.16 m world step;
  a light is blocked where the ray height falls inside the local column
  interval. EXACT for convex occluders — the entire frozen Cornell set
  (blocks, baffle, spheres), because a convex solid's column interval IS
  its full vertical extent. The hanging cbox03 baffle (y 3.4..5.4) is the
  payoff of the interval representation: rays beneath it pass, rays through
  it block — a max-only heightfield would falsely shadow the under-pass.
- **16-light superposition penumbra**: each grid light gets its own correct
  hard shadow; their superposition forms a 16-level penumbra for free.
- `--no-shadows` CLI flag: restores the exact M3.3 direct-only behavior for
  A/B ledger comparisons. Shadows are ON by default for cornell scenes and
  reported in the benchmark telemetry ("shadows: on/off").
- **Clean reference set** `reference/cbox0*_clean.ppm`: 320 spp path traces
  (per-spp sample streams, byte-exact resumable accumulation in
  `reference_pathtracer.py`). The MC noise in the raw 64/80-spp renders
  measurably depressed SSIM (raw-vs-clean self-check: similarity 54.32);
  the clean set is the measurement target from M4 on. Raw renders stay
  archived as provenance. All six reference md5s pinned in BASELINE.md.
- **Metric suite** in `tools/benchmark_compare.py`: MAE (L1), perceptual
  ΔE2000 (CIELAB, Sharma 2005 formulation, validated against 7 published
  reference pairs), and edge/shadow RMSE (Sobel gradient-magnitude error —
  sensitive exactly where shadow placement/penumbra differ), alongside the
  existing RMSE/SSIM. `--self-test` runs the reference-value checks.
- **Positive shadow acceptance checks** in `tools/verify_cornell_shot.py`:
  the frozen lighting model now includes the exact shadow march (float64),
  and two new probes pin it on hardware — a full-umbra point (all 16 grid
  lights provably blocked, expected ≈ 0, ceiling 14) and a penumbra point
  (4/16 lights lit, exact expected value 36 ± band). The "shadows GAP" row
  is retired; gaps are now indirect (M7+) and penumbra quality (M5).

### Changed
- `bench_tests` 100 → 132 checks: the shadow march's C++ port pinned against
  analytic column-interval fields (tall/short blocks, hanging baffle,
  sphere, empty room), the 16-light lit/blocked census for the acceptance
  probes, the march-step-vs-thinnest-occluder safety invariant, and both
  capture matrices' corner mappings + depth orderings.
- `reference_pathtracer.py` gained resumable accumulation (`--spp-start/
  --spp-end/--acc-in/--acc-out/--finish`) with per-spp sample streams
  (documented in BASELINE.md; chunked runs verified byte-identical to
  single runs).
- Roadmap re-anchored to the Cornell experiment ladder (README): M5
  area-light approximation → M6 probes/irradiance (IBL folds in) → M7
  GI (screen-space/voxel/probe) → M8 hybrid renderer; scene/material
  abstraction lands alongside M5 (the `Light`/`AreaLight` objects it
  needs are the same work).

### Deliberately NOT done
- No BRDF change of any kind (directive: fix the scene/solver layer, never
  tune the math against a broken benchmark).
- No classic shadow maps / cube maps: one centroid map would misplace each
  light's shadow by up to ~0.4 m and 16 cube maps is not an incremental
  milestone. Heightfield coverage (exact for convex solids, conservative
  for non-convex columns) is documented as the M4 contract; general
  omnidirectional shadowing lands with the scene-abstraction work.
- No denoiser on the reference: "clean" = 5× the samples of the same
  trusted estimator, no invented post-processing.

---

## 0.3.4 — M3.3.1: Cornell hotfix — the box renders black no more (geometry r2)

The first on-hardware Cornell render came back **96.5% black pixels**: only
the ceiling emitter, the short block's lit top, and a thin sliver on the tall
block were visible. Correctly diagnosed as a benchmark-scene failure, not a
shading failure — acceptance testing before any PBR tuning.

### Root cause (one categorical data bug, four instances)
`tools/generate_cornell.py` r1 authored every room quad's normal pointing
**away from the room interior** (floor −Y, ceiling +Y, walls outward), and
three of the five quads had winding that contradicted their own `vn`. The
engine shades one-sided (`NoL <= 0 → zero light`) and Cornell scenes pin
ambient to zero by design (closed light-transport system) — so the entire
room shaded to zero. The reference path tracer shades two-sided, which is
why no offline check caught it. The spheres carried a fourth instance of the
same class (winding inward vs. outward `vn`; harmless only because the
engine does not backface-cull).

### Fixed
- **Geometry r2** (regenerated by `tools/generate_cornell.py`): all five room
  quads now have normals AND winding facing the room interior; sphere winding
  flipped to agree with its outward `vn`. Positions, sizes, materials,
  camera, lights, exposure: **byte-identical to r1** (`cornell_scene_gen.h`
  md5 unchanged — the frozen constants never moved; only orientation data
  was wrong). Reference images unaffected (the path tracer is two-sided;
  `--check-obj` re-passed 4000/4000).
- **Orientation fail-loud everywhere**:
  - `generate_cornell.py` self-validates on every run (winding·vn agreement
    per face; room faces point inward; solids face outward; emitter −Y).
  - `bench_tests` grows 56 → **100 checks**: pinned room-quad normals
    (floor +Y, ceiling −Y, left +X, right −X, back +Z, emitter −Y),
    winding/vn agreement for every triangle of every variant mesh, and
    solids-face-the-room center tests.
- **`tools/verify_cornell_shot.py` (new)**: the M3.3 acceptance checklist as
  code. Reads the deterministic screenshot PPM, re-implements the frozen
  camera projection + point-light grid + display transform in float64, and
  judges every acceptance criterion (black-pixel ratio inside the box
  silhouette, red/green wall dominance, white surfaces, block visibility,
  emitter framing, ceiling GI-gap pin) with analytically derived expected
  values. Prints the shot's md5 for the ledger; exit 0 iff accepted.
  Validated headlessly against a CPU preview of the r2 geometry (ACCEPTED)
  and an r1-style black render (REJECTED).
- Sandbox emitter-quad winding now matches the authored −Y normal (data
  hygiene; the unlit draw never depended on it).

### Changed
- **Do NOT touch the shader**: the PBR/lighting math is untouched by design.
  No automatic two-sided normal flip was added — it would silently mask
  future asset bugs in a laboratory built to expose them. The one-sided
  contract is now explicit, documented, generator-enforced, and test-pinned.
- `benchmarks/cornell_box/README.md` + `scene.json` record the r2 revision;
  `BASELINE.md` notes that the first ledger row will be measured against r2
  geometry (no re-baselining: no rows existed yet and reference images are
  unchanged, md5s intact).

### Verification
- Clean-room build 0 errors/warnings; ctest 4/4 (math 73/0, obj 24/0,
  brdf 38/0, bench 100/0). Headless sandbox fails gracefully as before.
- On-hardware acceptance: render + `verify_cornell_shot.py` must print
  ACCEPTED; save the shot as the M3.3.1 baseline and record its md5 in
  `BASELINE.md` (protocol in the harness output and VERIFICATION.md).

---

## 0.3.3 — M3.3: Cornell Box renderer benchmark (cornell-box/1.0, frozen)

The engine gains its permanent laboratory: a frozen Cornell Box standard in
three variants, a deterministic benchmark mode with full telemetry, a
reference path tracer, and a two-axis ledger (visual similarity vs. frame
rate). From this milestone on, every renderer change is measured against the
same room — "can we increase visual agreement with a trusted reference while
maintaining a high frame rate?"

### Added
- **`cornell-box/1.0` frozen standard** (`benchmarks/cornell_box/`): three
  variants — CBox-01 baseline (direct/diffuse/color balance), CBox-02
  reflective materials (GGX/Fresnel/specular environment), CBox-03 indirect
  illumination (baffle blocks the emitter; the reference is bounce-only).
  Geometry (pbrt-book reflectances), camera, emitter, materials, and exposure
  are FROZEN; `tools/generate_cornell.py` emits the OBJs, `scene.json`, and a
  generated C++ header (stopgap until M4 scene loading). Constants are pinned
  by the new `bench_tests` CTest suite (56 checks). Full rules:
  `benchmarks/cornell_box/README.md`.
- **Benchmark mode** (`sandbox`): `--scene demo|cornell01|cornell02|cornell03`,
  `--benchmark N` (deterministic run: VSync off, fixed camera/lights/exposure,
  first 10% frames discarded), `--report file`, `--width/--height`. The report
  covers FPS, frame time (avg/min/p95), CPU frame time, GPU frame time (GL
  `TIME_ELAPSED` timer query, one-frame-lag readback, self-disabling on
  misbehaving drivers), draw calls, triangles, lights. VRAM is reported as
  "not reported (no core GL query)".
- **Reference path tracer** (`tools/reference_pathtracer.py`): renders the
  frozen scene with the SAME material model and display transform as the
  engine, plus ground-truth transport (true area-light NEE + visibility,
  full bounces, GGX lobe sampling). Includes an analytic-vs-OBJ geometry
  cross-check (`--check-obj`: 100% hit/miss agreement, surfaces within the
  inscribed-tessellation bound, identical shading normals).
- **Two-axis metric** (`tools/benchmark_compare.py`): RMSE + SSIM →
  "Cornell similarity" (0–100). Protocol: 960×540, 300 frames. Results are
  recorded per milestone in `benchmarks/cornell_box/BASELINE.md`.
- **`engine::Renderer` GPU timing** — `enableGpuTiming(bool)`,
  `lastGpuFrameMs()`, `gpuTimingActive()`.
- **`engine_bench_tests`** CTest target (56 checks: frozen-constant pins,
  light-grid flux model, on-disk geometry bounds/triangle pins).

### Changed
- **`engine::kMaxPointLights` 8 → 16** (`rendering/Lighting.h`; shader
  uniform arrays and loop bound updated to match). Gameplay-facing: upload
  paths sized off the constant pick up the new value automatically. Motivated
  by the Cornell emitter grid (4×4 deterministic area-light samples); also
  raises the demo scene's point-light ceiling.
- GL loader: +5 entry points (`GenQueries`, `DeleteQueries`, `BeginQuery`,
  `EndQuery`, `GetQueryObjectui64v`) — the surface now sits AT the documented
  glad2 exit threshold (~60); see ARCHITECTURE decisions.

---

## 0.3.2 — M3.2: PBR correctness pass (reference-verified math)

External review of M3.1 found one wrong equation and one energy-inconsistency
in the shading model. This milestone fixes both, then makes "correct" a test
instead of an opinion: the shader equations are pinned to independently
derived reference values. It also adds Valve-style material debug/validation
views — the tool that makes such mistakes visible on screen.

### Fixed
- **Smith height-correlated visibility was not the equation it claimed**
  (`sandbox/src/shaders.h`). The M3.0/M3.1 form computed
  `NoL * sqrt((NoV - a2*NoV)^2 + a2)` — i.e. it squared `(1 - a2)` inside the
  sqrt — which matches no published Smith variant and bent the specular
  response by up to **~13-15%** at medium/high roughness (verified numerically
  against the exact Heitz 2014 height-correlated term; 15% at the
  NoV=NoL=0.975, alpha=0.7 corner). Replaced with the algebraically exact
  Karis 2013 / Filament reference form:
  `GGXV = NoL * sqrt(NoV^2 * (1-a2) + a2)`, `V = 0.5 / (GGXV + GGXL)`.
- **Ambient was not energy-consistent with the direct term** (same file).
  Ambient multiplied the hemispheric gradient by raw albedo regardless of
  metalness, so metals received fake diffuse ambient while their direct
  diffuse correctly went to zero — chrome glowed with sky color. Ambient now
  splits exactly like the direct term: `kD = (1 - F)(1 - metalness)` weighted
  diffuse, plus the diffuse energy returning as environment specular sampled
  off the same gradient (explicit placeholder for M5 prefiltered IBL).
- **Dither comment vs amplitude mismatch** (`engine/src/rendering/Renderer.cpp`):
  the comment said "±1 LSB" while the triangular-PDF sum with `1.5/255`
  amplitude has tails to ±1.5 LSB (3 LSB peak-to-peak). The comment now
  matches the implementation; the amplitude is unchanged (it fixed real
  banding in M3.1).
- **Painted-hull material preset** (`sandbox/src/main.cpp`): metalness 0.15 →
  0. Paint is a dielectric (clearcoat over pigment); the intermediate value
  was a pre-PBR habit and now trips the new material validation view.

### Added
- **Material debug + validation views** (V key, `uDebugMode` uniform):
  shaded → world normals → albedo → metal-rough → F0 → validation. The
  validation view applies the published Dota 2 material rules (binary
  metalness; dielectric albedo below emitter levels; plausible metal F0; no
  accidental mirrors) and renders violations in saturated red. Debug views
  bail before the light loop — never compute what is not shown.
- **`engine_brdf_tests`** (`engine/tests/brdf_tests.cpp`, 38 ctest checks) —
  reference-value suite for the shader math:
  * the Karis/Filament visibility form vs an INDEPENDENT Lambda-based
    derivation of exact height-correlated Smith (agreement ~3e-7 in float32
    across a 3200-point NoV/NoL/alpha grid);
  * closed-form + float64 point anchors for D_GGX / V / F_Schlick /
    F_SchlickRoughness;
  * ambient-split energy rules (metalness=1 forces kD = 0);
  * deterministic white-furnace integrals pinning the exact D·V pairing
    (single-scattering deficit documented as the multi-scattering research
    slot, not a bug);
  * a regression check proving the removed squared-term form was measurably
    wrong (15% at the grazing corner).
- **`tools/brdf_reference.py`** — float64 re-derivation of every anchor
  (independent algebra + equal-solid-angle quadrature, no RNG). House rule:
  change a shader equation → re-derive anchors → update the test → only then
  judge the image.
- **`engine::Key::V`** mapped in `platform/KeyCodes.h` + `platform/Input.cpp`.

### Changed
- **Roadmap restructured** (README): correctness (M3.2, reference renderer)
  → scene/material abstraction (M4) → image-based lighting (M5) → shadows
  (M6) → clustered lights (M7) → indirect experiments (M8) → research (M9+:
  cheapest-acceptable-solution selection, multi-scattering energy
  compensation). New ground rule: fix known errors FIRST, so a later
  "looks better" is never just "was wrong".

---

## 0.3.1 — M3.1: real-hardware hotfixes (flashlight artifacts, Windows runs)

First M3 feedback round on real Windows hardware surfaced three defects that
headless CI could not catch: NaN speckle/banding inside the flashlight beam,
imported models never loading under MSVC multi-config build trees (keys 1-4
appeared dead), and the test executables being unusable when double-clicked.

### Fixed
- **Flashlight NaN speckle + banding rings** (`sandbox/src/shaders.h`):
  * `F_Schlick` no longer calls `pow(1.0 - VoH, 5.0)`. With the flashlight
    mounted on the camera, `L ≈ V` across the whole beam, so `VoH` hugs 1.0
    to within rounding noise; `pow` of a slightly negative base is undefined
    in GLSL and produced per-pixel NaN speckle, and low-precision driver
    `pow` paths quantized the Fresnel into visible concentric rings.
    Replaced with clamped multiplies (the UE4/Filament form).
  * `shadeLight` clamps `NoV/NoH/VoH` to their mathematical domains and
    guards the degenerate half-vector (`V + L ≈ 0` falls back to `N`).
  * GGX `alpha` floored at `2e-3` (`alpha == 0` divides by zero at
    `NoH == 1`); spot cone `theta` clamped to `[0,1]`.
- **8-bit gradient banding** (`engine/src/rendering/Renderer.cpp`): the
  tonemap pass now applies triangular-PDF dither (+-1 LSB, stateless
  per-pixel hash) before presentation. Smooth HDR gradients — flashlight
  pools especially — no longer quantize into concentric bands after sRGB.
- **Models not found under IDE build trees** (`sandbox/src/main.cpp`):
  the asset search now walks every ancestor of BOTH the CWD and the exe
  directory, so `build/bin/Debug/sandbox.exe` (MSVC multi-config) and
  `out/build/x64-Release` (CMake presets) both resolve the bundled models.
  On failure the sandbox prints every probed path instead of failing
  silently (which is what made keys 1-4 look dead).
- **`engine_obj_tests` crashed on Windows** (`engine/tests/obj_tests.cpp`):
  fixture setup used `system("mkdir -p ...")`, which cmd.exe rejects ("The
  syntax of the command is incorrect"); loads then failed and indexed access
  into the empty result tripped MSVC's vector-bounds assert. Fixture
  creation now uses `std::filesystem::create_directories`, write failures
  are reported, and indexed inspections are guarded (a failed load reports
  FAILs instead of crashing).

### Changed
- **Test executables pause for interactive runs** (both `engine/tests/*`):
  on Windows, when stdin is a real console (double-click launch), the tests
  print "Press Enter to close..." and wait; ctest/CI runs (piped stdio) are
  unaffected.

---

## 0.3.0 — M3: PBR lighting + HDR pipeline + model import

### Added
- **`engine::Renderer` HDR pipeline** — the frame is now:
  *linear HDR scene → RGBA16F (4x MSAA) target → resolve → exposure → ACES
  tonemap → sRGB → present*.
  - `initHDR(width, height, msaaSamples = 4)`, `resizeHDR(width, height)`,
    `setExposure(float)`, `exposure()`, `hdrActive()`.
  - Graceful degradation chain: MSAA → no-MSAA FBO → direct backbuffer
    rendering (legacy behavior), each step logged to stderr.
  - The tonemap program is engine-owned (GLSL lives in `Renderer.cpp`);
    scene shaders remain app-side.
  - `~Renderer` releases all owned GL objects while the context is alive.
- **`engine::loadOBJ`** (`assets/OBJ.h`) — dependency-free Wavefront OBJ
  importer:
  - `v` / `vn` / `vt` (texcoords parsed + reported as ignored), faces with
    3+ corners in `v`, `v/vt`, `v//vn`, `v/vt/vn` forms, negative indices.
  - Fan triangulation with **degeneracy rejection** (zero-area triangles
    dropped and counted, never NaN normals).
  - Flat-normal fallback when a file (or a corner) lacks normals; warnings
    returned in `LoadObjResult::warnings`.
  - `(position, normal)` corner deduplication; optional center-to-origin +
    uniform scale to `targetRadius`.
- **`engine::Lighting`** (`rendering/Lighting.h`) — `DirectionalLight`,
  `PointLight`, `SpotLight`, `PbrMaterial` (metallic-roughness),
  `AmbientTerms` (hemispheric stand-in for IBL), `kMaxPointLights = 8`.
- **`engine::Shader`** — `setFloat`, `setInt`, `setFloat3` (x2 overloads),
  `setFloat3Array` (vec3 array uploads for light loops), `nativeHandle()`.
- **`engine::Mat4`** — `transpose()`, `inverse()` (general, singular-safe),
  `scale(Vec3)` (non-uniform), `normalMatrix()` (inverse-transpose).
- **GL loader** — 24 new entry points (framebuffer objects, renderbuffers,
  MSAA, textures, extra uniforms, `DrawArrays`, `Disable`, `ReadPixels`).
  Still hand-scoped; ~55/60 before the glad2 exit condition triggers.
- **`engine::Key`** — `F` (flashlight), `Digit4` (model swap), mapped in
  `platform/Input.cpp`.
- **Tests** — `math_tests` grows transpose / inverse / normal-matrix suites
  (73 checks total); new **`obj_tests`** ctest target (23 checks: corner
  forms, fan triangulation, negative indices, degenerate faces, missing
  files, normalization).
- **Bundled assets** — `sandbox/assets/models/{torus,sphere,rock,ship}.obj`,
  regenerable via `tools/generate_models.py` (deterministic output).
- **Sandbox M3 demo** — PBR shaders (Cook-Torrance: GGX distribution, Smith
  height-correlated visibility, Schlick Fresnel; Lambert diffuse), relit
  floor + spinning quad (vertex-color albedo preserved), hero OBJ model
  with per-model material presets (keys 1-4), directional sun, two orbiting
  point lights rendered as glowing bulbs, camera flashlight (F), scroll-wheel
  exposure, `--frames N --out shot.ppm` verification screenshot hook.

### Changed
- **`engine::Renderer::clear()`** replaced by `beginFrame()` / `endFrame()`
  (target binding + tonemap bracket the scene). Callers: see sandbox.
- **`engine::Vertex`** layout grew a normal channel:
  `{pos, normal, color}` — locations 0 / 1 / 2. Mesh creation and the M1/M2
  scene meshes updated accordingly.
- **`GLFW_INCLUDE_NONE`** defined before every `<GLFW/glfw3.h>` include —
  the engine ships its own scoped GL loader and no longer depends on system
  GL headers being present (fixes builds where only GL runtime exists).
- Engine CMake: `src/assets/OBJ.cpp` added to the lib; `obj_tests` wired
  into CTest.
- Window title telemetry unchanged; triangle counts now include imported
  geometry.

### Removed
- Nothing public. (`Renderer::clear()` semantics moved into `beginFrame()`.)

---

## 0.2.0 — M2: Event-driven input + fly camera

### Added
- **`engine::Input`** (`platform/Input.h`) — event-driven input system. Owns
  keyboard / mouse state and cursor modes.
  - Edge-detection API: `pressed(Key)`, `released(Key)`,
    `mousePressed(MouseButton)`, `mouseReleased(MouseButton)`.
  - Per-frame deltas: `mouseDX()`, `mouseDY()`, `scrollDelta()`.
  - `CursorMode { Normal, Hidden, Locked }`; raw mouse motion enabled where
    supported when locked.
  - Focus-loss handling: key + mouse state cleared on focus loss to avoid
    stuck-key artifacts.
- **`engine::Key`** enum, with single source of truth in
  `platform/KeyCodes.h` (see Fixed below).
- **`engine::MouseButton`** and **`engine::CursorMode`** enums.
- **`engine::Camera`** (`rendering/Camera.h`) — FPS-style yaw/pitch fly camera
  with clamped pitch (`kMaxPitch ≈ 89.95°`), perspective projection, and
  derived `forward` / `right` / `up` basis vectors.
- **`engine::Application::input()`** accessor.
- **`engine::Application::kMaxDeltaTime`** (`0.1 s`) — `dt` passed to the frame
  callback is clamped; telemetry uses unclamped time.
- Math test target **`engine_math_tests`**, wired into CTest. Covers `Vec3`
  basics, identity laws, translation, rotation, perspective depth mapping,
  `lookAt`, and camera yaw/pitch clamping.

### Changed
- **`engine::Application::run`** now invokes `input.newFrame()` before
  `window.pollEvents()` so edges reflect this frame's events.
- **`platform/Window`** no longer owns keyboard input. The M1 `Window::isKeyDown`
  method and the `toGLFWKey` / `toGLFWKey` helpers have been removed; gameplay
  uses `Input::isKeyDown` / `pressed` / `released` instead.
- Sandbox (`sandbox/src/main.cpp`) updated to the M2 demo: spinning quad +
  reference floor, fly camera with locked-cursor mouse look.
- Engine `CMakeLists.txt` registers the `math_tests` ctest target
  (`BUILD_TESTING`, default ON).

### Removed
- M1 `engine::Window::isKeyDown(Key)` — superseded by `engine::Input`.
- M1 `engine::Key` 6-key enum — replaced by the full `Key` enum in
  `platform/KeyCodes.h`.

### Fixed
- `engine/src/platform/Window.cpp` no longer fails to compile due to leftover
  M1 `Window::isKeyDown` / `toGLFWKey` dead code that referenced an undefined
  `Key` symbol.
- `engine/tests/math_test.cpp` renamed to `engine/tests/math_tests.cpp` to
  match the filename expected by `engine/CMakeLists.txt` (CMake previously
  printed `Skipping engine_math_tests: ... not found` and silently skipped
  tests).

---

## 0.1.0 — M1: Foundation (Hello Quad)

### Added
- **`engine::Application`** (`core/Application.h`) — owns the boot sequence
  and the main loop; GL lifetime contract documented in `docs/ARCHITECTURE.md`.
- **`engine::Window`** (`platform/Window.h`) — GLFW wrapper exposing
  `shouldClose`, `requestClose`, `pollEvents`, `swapBuffers`,
  `getFramebufferSize`, `setTitle`, `setVSync`.
- **`engine::Renderer`** (`rendering/Renderer.h`) — global GL state per frame:
  `setViewport`, `setClearColor`, `clear`, `drawIndexed`, `stats`.
- **`engine::Shader`** (`rendering/Shader.h`) — move-only GLSL program wrapper
  with `fromSource(vs, fs)`, `bind`, `setMat4`, `setFloat4`.
- **`engine::Mesh`** (`rendering/Mesh.h`) — move-only VAO + VBO + EBO for a
  fixed `{position, color}` vertex layout.
- **`engine::Vertex`** layout `{x, y, z, r, g, b}`.
- **`engine::Vec3`** / **`engine::Mat4`** in-house math (column-major,
  OpenGL uniform layout).
- Hand-scoped **GL 3.3 core loader** (`rendering/GL.h`) — no glad/GLEW
  dependency; entry points loaded explicitly on demand.
