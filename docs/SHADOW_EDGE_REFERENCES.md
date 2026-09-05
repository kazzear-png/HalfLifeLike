# Shadow-edge quality: how shipped engines solve what our marcher hit

**Status:** research verdict, M4.0.8; extended M4.0.9 (parallax window +
the centroid rig-march experiment); extended M4.0.9.1 (the analytic
lateral half-plane closes the centroid path's lateral blindness).
**Question (user):** the blocky penumbra after the first lit march — no other
engine shows that. Is it a bug, and how do other codebases avoid it?

## M4.0.9.1 extension: the centroid path's remaining sharp edges were the LATERAL cliff

Field verdict after the M4.0.9 hardware matrix: `--shadow-centroid 1
--shadow-jitter 1` looks the best of the matrix, but the edges are still
very sharp and shrinking the penumbra never smooths them. Both observations
have ONE root cause. The M4.0.9 centroid grade is a half-plane model driven
by the VERTICAL clearance of the columns the one trace touches; a receiver
whose centroid ray skims PAST a footprint never touches a real column, so
the grade never fires and the lateral silhouette jumps from fully lit
(vis = 1) to pierce-dark in one pixel row — a cliff. No penumbra width
fixes a cliff: `--shadow-light-size` sets the ramp's width, but a cliff
has no ramp, which is exactly why "shorter penumbra" moved the edge
without smoothing it. The in-plane band (the geometry the half-plane model
sees) was already continuous and hardware-matched the 16-light path, so
the surviving perceptual sharpness IS the pinned single-ray blindness (the
hardware row's +1.2 edge RMSE) — now dominant because everything else is
smooth.

The M4.0.9.1 fix grades the LATERAL miss distance analytically against the
occluder set itself (the same convex prisms/spheres the heightfield
rasterizes — walls are not occluder materials), zero new texture taps:
`g_lat = clamp(0.5 + r/(E_perp·min(t,tCap)), 0, 1)` with r the signed 2D
ground distance to the nearest eligible primitive (box footprint SDF;
sphere ground disk of radius `sqrt(R² − (|rayY−cy| + bias)²)`, which is
algebraically the same "column interval contains rayY" the texture
encodes), E_perp the emitter's exact lateral support along the trace
perpendicular (`2·(hx·|px| + hz·|pz|)` — the 1.30 × 1.05 m patch's
anisotropy falls out of the support function for free). Per sample the
march takes `min(g_vertical, g_lat)` — conservative at block corners.
Correctness rides along: the soft early-out now also requires
`B(t) ≥ 0` (with B < 0 a future sample can still be occluder-eligible and
the lateral grade can still undercut the vertical bound; B ≥ 0 puts every
future sample above every top, so the old bound is exact again), and the
march breaks once any grade hits 0. `--shadow-lateral 0` reproduces the
M4.0.9 centroid march bit-for-bit. float64 validation: the M4.0.9 "blind"
receiver (footprint miss) grades 0.5689 (hand-derived 0.5689 from the
~0.056 m closest approach at t ≈ 0.50), sliding away 0.7172 — the cliff is
now the ~1 m continuous band the rim rays physically produce.

Adjudication of the two external repo reviews that triggered this
milestone (kept here because the reasoning is load-bearing):
(1) "the window has wrong asymptotics (`penumbra·traveled`), apply
`t/(1−t)`" — STALE: that is the pushed M4.0.7 GitHub code; M4.0.9 already
ships the parallax form with contact hardening, and the hardware matrix
re-derived its scale. (2) "finish the measurement matrix first" — already
done (ledger row closed with four pre-registered verdicts); both reviews
read the stale push, not the local tree. (3) "fixed step count kills the
seam" — a REAL secondary defect (`int steps` lattice-phase jumps, ~7%
grade steps at wall distances), QUEUED, not mixed into this milestone.
(4) "PCSS taps at the blocker depth" — rejected for this rig: testing
occlusion only at the argmin depth drops every other occluder in
shadow-overlap zones (tall + short block), breaking the exactness story
the heightfield is built on. (5) "backprojection (clip the blocker's
projected silhouette against the emitter rect)" — the exact end-state for
convex occluders and the right M5 headline; these half-plane ramps are its
per-axis 1D reductions. (6) "black block faces = missing GI" — correct,
and the repo already says so (CBox-03, the M7/M8 slots); no ambient cheat.

## M4.0.9 extension: the blockiness after M4.0.8 was the WINDOW, not the march

The external analysis (the "Solution A/B/C" thread) diagnosed two causes:
the 16-level superposition and iq-form min-accumulation quantization. By
M4.0.8 the second was addressed (refinement) and the first was HALF-true:
each light's graded visibility is continuous since M4.0.7, so the
superposition no longer quantizes to 17 levels — but each of the 16 graded
edges was still NARROW (the M4.0.7 window scale ≈ 0.0299·traveled ≈ 0.17 m
at typical depths), and 16 narrow edges offset by the grid pitch compose
into the structured staircase the ledger measured.

**First-principles check against the reference.** The clean reference
integrates visibility over the true emitter area. For a straight edge at
height y_B under an emitter of extent E at height H, the penumbra band on
the receiver plane spans E · y_B / (H − y_B) (similar triangles): the tall
block's top edge alone spans 1.30 · 3.30 / (5.49 − 3.30) ≈ 1.96 m on the
floor; the emitter's z-extent gives 1.05 · 1.54 ≈ 1.62 m. The rig's
adjacent grid lights are kLightPitch = 0.325 m apart, so their shadow edges
land ~pitch · y_B / (H − y_B) apart — the 16-superposition staircase has
~4–5 risers across the physical band. Grading each light over exactly its
neighbor's edge offset — the PARALLAX window w(t) = pitch · t / (1 − t) —
merges the staircase into the continuous band. That is M4.0.9's default
change (one window-form swap, zero extra taps), and it is the same idea
PCSS formalizes: **blocker distance → penumbra width → filtered visibility**
(Fernando 2005), with the width derived from the frozen rig instead of
tuned.

**Why not the literal Solution B (screen-space SSSS)?** Hillaire-style
SSSS renders a hard shadow mask and blurs it in screen space because a
shadow map cannot be filtered analytically. This engine's heightfield is
directly queryable in world space — the blur's effect (neighboring samples'
visibility averaged over the light's projected extent) is computable
exactly where the information lives, without a mask prepass, without a
composite pass (the renderer is forward + MSAA), and without screen-space
edge artifacts (a screen-space blur bleeds shadows across geometry
silhouettes; the heightfield grade stays in world space). The heightfield
adaptation of B is the per-light parallax window (shipped, default) and the
centroid rig march (shipped, experiment — see below).

**Why the centroid march is an experiment, not the default.** One ray to
the emitter centroid + a half-plane grade (`g = clamp(0.5 + d/(S·t), 0, 1)`,
S = the emitter's mean extent 1.175 m) reproduces the top-edge band
smoothly and cuts the march cost ~16x — but a single ray is structurally
BLIND to the LATERAL penumbra: rim rays that pass BESIDE a convex
occluder (the emitter's z-rims past the tall block's z-face) produce
visibility the ray never measures. Concretely: a floor point at
z = 0.5 (just south of the block's z-face at 0.35) sees ~40% of the
emitter past the face rim, while its centroid ray pierces the block face
below the top → the model returns hard 0. Screen-space SSSS solves this
with the spatial ensemble (neighboring pixels' rays straddle the edge);
the 16-light rig solves it with light-domain sampling; the single ray
cannot. The bench pins the blind spots as a contract, and the ledger's
A/B rows decide whether the smoothness wins where the band shape loses.
The M5 slot (true per-pixel emitter integration) retires the question.

**Why not Solution A (jitter) as the default?** The thread's premise —
"your existing MSAA/TAA will smooth the noise" — is false in this engine:
there is no TAA, and standard MSAA shades once per pixel, so per-pixel
jitter ships as static grain. Against a CLEAN 320-spp reference, SSIM
penalizes grain more than a smooth (slightly mis-shaped) band. The jitter
ships flag-gated (`--shadow-jitter 1`) as the TAA-precondition diagnostic;
it shifts BOTH march lattices (the 16 per-light loops and the centroid
loop), so the A/B row quantifies grain with and without the centroid
experiment — the first hardware pass proved why that matters: with the
jitter wired centroid-only, the "without centroid" row silently measured
nothing (its fps matched the default row exactly).
Solution C (min-max mip hierarchy / cone stepping) remains the M4.1+
candidate for the cadence ceiling; the M4.0.9 window work is orthogonal
to it.

## M4.0.9 hardware verdict (the A/B matrix measured)

The full 8-row matrix ran on hardware (960×540, vs the 320-spp clean
reference; md5s + GPU ms in the ledger row). The similarity axis:

| Config | SSIM | edge/shadow RMSE | GPU ms |
|---|---|---|---|
| binary pin (0) | **0.52630** | **80.069** | 1.237 |
| half pitch (0.1625) | 0.52479 | 80.182 | 2.141 |
| full pitch (0.325, first default) | 0.52382 | 80.272 | 2.266 |
| double pitch (0.65) | 0.52199 | 80.423 | 2.316 |
| centroid S=1.175 | 0.52370 | 81.389 | 0.719 |
| centroid binary | 0.52321 | 81.765 | 0.553 |

Four verdicts, all pre-registered in the BASELINE protocol:

1. **The emitter-extent window is falsified.** Similarity degrades
   MONOTONICALLY with the window scale on every metric. Structural
   reason: the 4x4 grid IS the emitter quadrature — the spread of its 16
   hard edges already synthesizes the physical parallax band, so a
   per-light window of that same extent double-counts it (composite band
   ~ spread + w). The window's real job is de-quantizing each per-light
   edge against the march cadence; the default re-derives to half pitch
   (0.1625 ≈ 2× kMarchStep), the best SOFT row on every axis.
2. **Binary is the metric king.** The SSIM axis cannot see the staircase
   at this resolution under the GI gap (best-worst spread across all
   rows = 0.43 similarity points; the raw-vs-clean MC-noise ceiling
   alone sits at 54.32). The soft default is a perceptual-polish trade
   (−0.15 similarity for the band the eye complained about) — recorded,
   not hidden.
3. **The centroid experiment closes without a similarity win** (the
   pre-registered promotion rule): its pinned lateral-band blindness
   measured +1.2 edge/shadow RMSE exactly as the bench contract
   predicted. The ~16× tap cut is real (−42% GPU vs binary); the M5
   per-pixel emitter integration slot retires the question.
4. **Jitter measured SSIM-neutral** (+0.00001 on both lattices) — the
   predicted grain regression does not exist at this metric's
   granularity because the grade is continuous and absorbs a
   half-spacing lattice shift; the TAA-payoff row reads zero. Default
   stays OFF (no benefit without temporal accumulation, M8+).

## Verdict

Yes — by the standard of every shipped heightfield-ray implementation, our
marcher was missing an entire component. The defect is not a wrong value
(anything like the M4.0.5 swizzle); it is structural:

> A fixed-cadence linear search whose *point tests* are also the *final
> decision* uses the detection resolution as the decision resolution.

Every production reference separates three concerns that we collapsed into
one loop, and each reference names the failure mode we hit:

| # | Component | What the references say | Our marcher |
|---|-----------|------------------------|-------------|
| 1 | **Coarse linear search** brackets the feature | "the linear search (required to avoid missing large structures) is **prone to aliasing**, by possibly missing some thin features" — GPU Gems 3 ch. 18 | Had it (M4.0.5) — and hit exactly that aliasing: staircase bands tracking the 0.16 m cadence |
| 2 | **Refinement phase** resolves each bracket to sub-step precision | "the ray-height-field intersection is performed using a **binary search, which refines the result produced by some linear search procedure**" — GPU Gems 3 ch. 18 (relief mapping); same pattern in Tatarchuk's POM, Ammann 2010 heightfield ray-casting, every terrain raymarcher | **Missing until M4.0.8** |
| 3 | **Filtered / continuous visibility signal** (never a raw binary per light) | PCF/PCSS filter depth-compare results; VSM reconstructs continuous visibility from moments; iq: soft shadows "for free by computing how deep the shadow ray went into the terrain. By smoothstep()-ing this amount one can control" the look | Had it (M4.0.7): clearance vs. distance-growing window = iq's terrain trick with the window derived from the frozen rig (PCSS-style derivation, not tuned) |

So the user's instinct is confirmed by the literature: nothing that ships
renders heightfield/parallax shadows as a bare fixed-step point-test march.
The staircase bands were the textbook symptom of missing components 2 and 3;
M4.0.7 shipped 3, M4.0.8 ships 2.

## The references (what other code actually does)

### Relief mapping / parallax occlusion mapping (the same ray-vs-heightfield problem)

- Policarpo & Oliveira, *Relaxed Cone Stepping for Relief Mapping*, GPU Gems
  3 ch. 18 — <https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-18-relaxed-cone-stepping-relief-mapping>.
  Canonical two-phase solver: linear search finds the bracketing interval,
  binary search refines the crossing. The chapter's own caveat — linear
  search alone "is prone to aliasing" — is our M4.0.5/M4.0.6 defect named
  verbatim. Cone stepping adds a precomputed safety bound so steps can grow
  without missing features (the family our future mip-hierarchy idea
  belongs to).
- Tatarchuk, *Dynamic Parallax Occlusion Mapping with Approximate Soft
  Shadows* (I3D 2006; GDC "Practical Parallax Occlusion Mapping…").
  Production POM: linear search + refinement + approximate soft shadows on
  top. Note the ordering — soft shading is layered on a *refined* search,
  not used to hide an unrefined one.
- Baboud & Décoret, *Rendering Geometry with Relief Textures* (2006) —
  <https://artis.inrialpes.fr/Publications/2006/BD06/relief05.pdf>:
  same bracket-then-refine structure.

### Terrain raymarching (heightfield shadows specifically)

- iq, *raymarching terrains* — <https://iquilezles.org/articles/terrainmarching>:
  "compute soft shadows for free by computing how deep the shadow ray went
  into the terrain. By smoothstep()-ing this amount one can control
  softness." This is precisely the M4.0.7 accumulator (signed clearance vs.
  a distance-growing window), independently re-derived from the frozen rig's
  4x4 light-grid quadrature.
- iq, *Soft shadows in raymarched SDFs* — <https://iquilezles.org/articles/rmshadows>:
  penumbra as a function of distance traveled; same family.
- Ammann et al., *Hybrid rendering of dynamic heightfields using ray-casting*
  (2010): coarse march with a fixed-iteration binary-search refinement.
- Community/state-of-the-art threads converge on the same structure
  (<https://computergraphics.stackexchange.com/questions/1725>): coarse
  march, then refine the hit interval; hierarchical bounds for speed.

### Shadow-map world (why "other engines" never show this)

- PCF (Reeves et al. 1986) and PCSS (Fernando 2005,
  <https://www.realtimeshadows.com/sites/default/files/sig2013-course-softshadows.pdf>):
  the visibility signal is *filtered*, and the penumbra width is *estimated
  from light size and distances* (blocker search → penumbra estimation →
  filter). Our kShadowPenumbra = 0.5 · pitch / height is the same idea with
  the 4x4 point-grid quadrature playing the light-size role — derived from
  the frozen rig, not tuned.
- VSM (Donnelly & Lauritzen 2006; summed-area variant GPU Gems 3 ch. 8):
  continuous visibility from prefilterable moments — the other route to a
  smooth signal — with a documented **light-bleeding** failure mode. We
  deliberately did not adopt moments for a 16-light rig: the clearance
  window gives the continuous signal without a new texture format or the
  bleeding pathology.

## Mapping onto the marcher (and what M4.0.8 changes)

- `shadowVisibility()` keeps its proven structure: fixed-cadence linear
  search + interval test + horizon-style soft accumulation (components 1+3).
- M4.0.8 adds component 2 in its provably safe form: after the linear
  search, two extra fetch pairs at `t_best ± half-step` re-locate the true
  minimum clearance at half-step resolution, feeding the SAME window. It
  engages only where the window fired (`vis < 1`) — zero cost for fully lit
  or hard-blocked pixels — and a refined fetch that lands inside the
  interval returns the hard 0 the binary march would have (a crossing the
  cadence jumped over is a real crossing).
- With `uShadowPenumbra == 0` the refinement is unreachable, so the
  `--shadow-penumbra 0` md5-replay pins (byte-match M4.0.6 / M4.0.5) still
  hold. `--shadow-refine 0` reproduces the M4.0.7 soft march exactly.

## Known residual limits (documented, not hidden)

1. The refinement sharpens the graded value where the window fired; it
   cannot fire the window. A graded band narrower than the march cadence
   can be straddled without any sample landing inside it — the fix there is
   a wider window (the ledger's 0.015 / 0.06 tuning bracket) or a
   structure-aware step (cone-stepping / min-mip hierarchy, M4.1+
   candidate; the bracket-refinement shipped here is its inner loop).
2. Negative lobes narrower than the bracket-detection limit (feature width
   below ~half a step) stay leakable — the same class the baffle-thickness
   invariant (step < 0.19 m) guards for the frozen scene, and below the
   capture's 21.5 mm/texel meaningful resolution for anything thinner.
3. M4.0.9: the parallax window diverges as t -> 1; the march span stops at
   tEnd = tCap + wCap/(y1-y0) and the window caps at wCap. The capped tail
   is exact where occluders can be (t <= tCap) and saturating beyond — a
   ray that NEVER exits the occluder band (only the cbox03 under-baffle
   region) grades against the capped window (wCap ~ 35 m there), which is
   the physically correct near-black. The default window scale moves the
   ledger's tuning bracket to --shadow-penumbra 0.1625 / 0.65.
4. M4.0.9: the centroid experiment's single-ray blindness (lateral
   penumbra) was pinned in bench_tests as a contract; M4.0.9.1 CLOSED it
   with the analytic lateral half-plane (the pin was rewritten to the new
   continuous-band contract, and `--shadow-lateral 0` reproduces the old
   cliff exactly). The pin still protects the analysis: if a future change
   moves the graded band values, the model changed and the ledger must
   re-measure. Residual: block CORNERS grade slightly dark (min of two
   1D half-plane cuts is conservative at the corner), and the `int steps`
   lattice-phase seam (~7% grade steps at wall distances) is queued.

## M5.0: the area light retires the approximation entirely (the backprojection milestone)

M4.0.7→M4.0.9.1 progressively de-quantized the 16-point-grid approximation
of the area emitter: the window merged the 16-step superposition staircase,
the bracket refinement killed the march-cadence aliasing, and the lateral
half-plane closed the single-ray cliff. All of that work graded
APPROXIMATIONS of one quantity — the fraction of the emitter patch visible
from the receiver. M5.0 computes that quantity directly.

**The method (Assarsson–Akenine-Möller / Bavoil backprojection family,
adapted to the analytic occluder set instead of a shadow map).** For a
receiver R, the set of emitter points p blocked by a convex occluder is
exactly the central projection of that occluder from R onto the emitter
plane. The transport therefore: starts from the emitter rect polygon;
subtracts each occluder's projected region; and evaluates the DIFFUSE as
the form factor of the surviving polygon — not the form factor of the
rectangle times a scalar. Two structural consequences:

1. **Unions are exact.** The subtraction is the first-outside-edge
   decomposition: for blocker B with hull edges e_1..e_m,
   `piece − B = ∪_i (piece ∩ outside(e_i) ∩ inside(e_1..e_{i−1}))`, each
   term one Sutherland–Hodgman half-plane clip. SH clips are area- AND
   form-factor-exact for any simple subject (the zero-width bridge edges
   lie on the clip line and their Arvo edge contributions cancel exactly).
   The naive per-blocker product the design started from over-darkened a
   cbox03 overlap band by 18.4% — measured, then engineered away.
2. **Boxes are exact, spheres are sampled.** A box's blocked region is the
   radial sweep of its footprint between the two cap magnifications
   m(h) = (Ly−Py)/(h−Py) — the convex hull of the two projected caps —
   exact, and correct for floating geometry (the cbox03 baffle's under-pass
   band survives: the sweep of the [ylo, yhi] band, not a filled hull of
   one cap). A sphere's region is the section of its tangent cone; the
   shipped 33-gon inscribes it (one-sided under-block ≤ ~9% of the local
   fraction inside the transition band, ≤ 0.9% of K end-to-end, zero in the
   umbra and the lit region). The sampling lives in DIRECTION space
   (`d(θ) = cosα·Ĉ + sinα·ring`), which stays well-defined exactly where
   the classical tangent-circle projection collapses: the frozen rig's
   floor sphere + floor receiver sits ON the parabolic boundary, and a
   receiver ON the sphere sends α → 90° while the tangent circle shrinks to
   a point.

**What this retires.** The parallax window (there is no march whose grade
needs de-quantizing), the bracket refinement (no sampled minimum to
re-locate), the lateral half-plane (no single trace to be blind — the
projection through the receiver covers the full cone), the jitter (the
transport is noise-free by construction), and the 16-superposition penumbra
itself. Contact hardening is no longer a window artifact: a near blocker's
projected region simply covers more of the patch. The emitter's 1.30 × 1.05
anisotropy is the polygon's real shape, not a support-function ramp.

**The validation discipline.** scripts/check_area_model.py pinned every
formula against float64 brute force BEFORE the GLSL existed: Arvo's
edge-sum structure and constant (1e-9), the box sweep identity (exact; the
first hull8 implementation silently clipped wrong through a
`max(denominator, ε)` guard that destroyed negative denominators — the
brute force caught it), the direction-space sphere cone, the exact-union
piece decomposition, and the VOS solid angle (1e-5 vs Monte Carlo). The
float32 CPU mirror (AreaLight.h) is pinned in bench_tests to the same
fixtures (worst 3.1e-4), and the verify harness carries its own float64
port (`--self-test-area`).

**Known residuals (documented, ledger-measured).** (1) The sphere's
inscribed conic — one-sided, bounded, queued behind ledger evidence; the
upgrade samples the conic at its rect-edge crossings. (2) The specular is a
one-tap representative-point quadrature: energy-consistent, not
shape-exact (a mirror sphere reflects one bright point, not the patch
rectangle — the 16-grid's 16 dots were not closer to the reference's
rectangle). (3) The `int steps` lattice-phase seam of the legacy paths is
moot for the default transport but remains queued for the fallback. The
`--area-light 0` fallback keeps every M4.x instrument and pin exactly as
shipped.

---

## M5.0.1 — why the area transport cost 4x the march, and the exact-reject fix

**Field report.** 0.5.0 on the user's box: ~110 fps vs the M4 variant's
~475. The first attempt (a 0.5.1-draft inclusion-exclusion rewrite) was
built, measured, and REJECTED: it read worse (float32 parity-sum
cancellation in deep umbra) and ran worse (8-subset clip chains multiplied
ALU; the two 56-vertex scratch polygons kept the dynamic-indexing local-
memory pressure). The tree was reverted to 0.5.0 byte-for-byte before this
section's design landed. Lesson of record: reformulating pinned math is a
bet; eliminating provably-dead work is not.

**Where the 0.5.0 cost actually was.** The piece carve ran unconditionally
for every blocker x pixel: hull + one S-H chain pass per hull edge per
piece, over ~9 KB of dynamically-indexed local arrays (GLSL: scratch
memory). Two force multipliers: (1) a blocker whose region reaches nothing
still fragments pieces — hull edges are infinite lines, and a piece they
cross is "phantom-split" into area-equal parts (the union is unchanged;
the work and the piece slots are not); (2) the sphere paid 33 sin/cos cone
samples + a 33-point insertion-sort hull before the same carve. The waste
then fed the 16-piece cap, which can drop REAL tail pieces — 0.5.0
silently undercounted visible region in exactly those cases (fuzz
adjudication: brute 0.013677, 0.5.0 0.005424, 0.5.0.1 0.013694).

**The fix — five one-sided skips (GLSL only; AreaLight.h stays the plain
reference).** Per-piece AABB bookkeeping (exact; min/max add no rounding)
plus: box reject before hull+carve (8-point AABB axis-disjoint from every
piece); sphere reject before the 33 samples (conservative disk bound
`rFoot = (Cy−Py)(ca|ch.xz|+sa)/ymin`, valid exactly when no generator
clamps — `ymin > 0`); sampled-cone AABB reject before hull+carve; in-carve
copy-through of pieces whose AABB misses the blocker AABB; and the
no-carve fast exit returning `kRect` (the final loop would sum exactly one
Arvo over the rect — bit-identical by construction). Every skip is
one-sided: an AABB contains its polygon, so a rejected blocker reaches no
piece and the union cannot change.

**Equivalence contract (check_area_model.py section 8).** 4000-iteration
float64 fuzz (jittered boxes, floor + elevated spheres, high receivers):
per-fired-reject SAT soundness; the no-region-loss direction
(`K_skips ≥ K_ref − tol` — the only separating mechanism is the cap, and
the skips only restore); brute-force adjudication of deviations (skips
must be strictly closer); frozen-config agreement to 1e-12 relative (the
reference's own phantom-split regrouping makes exact float equality
unattainable BY CONSTRUCTION — 1e-15 dust). What remains on the GPU side
is float32 regrouping of equivalently-decomposed regions, ~1e-7 relative —
below display quantization by three orders.

**Cascaded shadow maps — adjudicated (M5.0.1 research ask).** CSM is a
DIRECTIONAL-sun technique: split the camera frustum into depth ranges and
give each its own shadow-map projection to fight perspective aliasing of
one depth buffer (Microsoft D3D docs, C4 Engine wiki, three.js CSM, Intel
samples). This engine has zero shadow maps — the area transport is
analytic backprojection in a frozen 5.5 m room — and no sun-shadow path to
split. The reported problem is per-pixel ALU/local-memory cost, which CSM
would only increase (more maps to render and sample). The transferable
idea is distance-partitioned LOD, which this design does NOT take: LOD by
any other name changes the pinned look. The rejects achieve the same
outcome honestly — near-blocker pixels keep the full exact machinery;
far/blocker-behind pixels pay almost nothing.
