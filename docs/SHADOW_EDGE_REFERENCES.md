# Shadow-edge quality: how shipped engines solve what our marcher hit

**Status:** research verdict, M4.0.8; extended M4.0.9 (parallax window +
the centroid rig-march experiment).
**Question (user):** the blocky penumbra after the first lit march — no other
engine shows that. Is it a bug, and how do other codebases avoid it?

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
   penumbra) is pinned in bench_tests as a contract; see the M4.0.9
   section above. The pin protects the analysis: if a future change makes
   the "blind" fixtures grade, the model changed and the ledger must
   re-measure.
