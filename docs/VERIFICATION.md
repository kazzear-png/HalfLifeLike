# Verification

Checklist for verifying that a given build behaves correctly. Run through it
after every milestone that touches build / window / input / rendering / assets
/ math.

**Current milestone:** M3.3.1 — *Cornell Box benchmark hotfix (geometry r2 + acceptance harness)*.

---

## Build

- [ ] `cmake -B build` succeeds. First run downloads GLFW into `build/_deps/`;
      third-party CMake deprecation warnings are harmless.
- [ ] `cmake --build build` succeeds with zero errors on the configured compiler:
      - MSVC (Windows VS generator)
      - GCC / Clang (Ninja or Make)
- [ ] `ctest --test-dir build --output-on-failure` runs all four suites and passes:
      - `math_tests` — **73 checks, 0 failure(s)**
      - `obj_tests` — **24 checks, 0 failure(s)**
      - `brdf_tests` — **38 checks, 0 failure(s)** (M3.2: reference-value suite)
      - `bench_tests` — **100 checks, 0 failure(s)** (M3.3 frozen pins + M3.3.1 orientation pins)
- [ ] (Windows) Double-clicking `engine_math_tests.exe` / `engine_obj_tests.exe`
      in Explorer runs the suite and holds the console open with
      "Press Enter to close..." — results stay readable; `ctest` runs are
      unaffected (no pause when stdio is piped).

## Runtime

- [ ] Console prints `[Engine] Initialized. OpenGL: <version>` where `<version>`
      is `3.3` or newer, followed by `[Renderer] HDR active: RGBA16F, 4x MSAA, ...`.
- [ ] A `1280×720` window opens titled `Sandbox - M3 PBR Lighting + OBJ Import`.
- [ ] Console lists all four imported models with vertex/triangle counts:
      gold torus, chrome sphere, asteroid, painted hull. No `[Sandbox] ...`
      warnings about the asset directory.
- [ ] The floor and the color-vertex quad are lit: the quad's red/green/blue/
      yellow faces still read as colored (vertex-color albedo preserved), but
      now respond to light — glossy highlights from the orbiting lights.
- [ ] The gold **torus** floats behind the quad, rotating: bright sun highlight
      with metallic falloff, orange/cyan tints on the side facing each bulb.
- [ ] Two **glowing bulbs** (orange + cyan) orbit the scene; the floor picks up
      their colored light with inverse-square falloff (bright near the bulb,
      fading with distance).
- [ ] Highlights do NOT clip harshly: the ACES tonemap rolls bright light into
      white smoothly (compare with `--frames 1 --out low.ppm` after lowering
      exposure — no banding, no hard saturation).
- [ ] Resizing the window keeps the scene correct (HDR targets are recreated
      for the new size; MSAA fallback messages may appear but rendering continues).

## Lighting / HDR checks

- [ ] **F** toggles the camera flashlight: a warm, smooth-edged cone follows
      the view direction; surfaces brighten where the cone lands, with a soft
      outer falloff (no hard circle edge).
- [ ] **The flashlight pool is CLEAN**: no dark speckle dots, no concentric
      noise/banding rings inside the lit circle (M3.1 regression target — the
      original `pow`-based Fresnel produced NaN speckle exactly there).
      Aim the cone at the floor from a few units up and sweep the view: the
      falloff must stay a smooth gradient end to end.
- [ ] Smooth gradients show **dither, not bands**: in a dim flashlight pool,
      the 8-bit backbuffer carries fine noise grain (triangular PDF, tails to
      ±1.5 LSB) instead of stepped concentric rings.
- [ ] **Mouse wheel** changes exposure; each change prints
      `exposure = <value>`. Scrolling down goes toward black, up toward a
      blown-out white — motion stays visible during both (proves exposure is
      applied *after* lighting, at the tonemap stage).
- [ ] Metals behave differently from dielectrics: the torus/sphere (metalness
      1) show no white diffuse term under the sun; the rock/ship do.
- [ ] Rough vs. smooth: the chrome sphere has a tight sun highlight; the
      asteroid's facets have broad, dim responses.

## Cornell Box benchmark checks (M3.3, acceptance harness since M3.3.1)

The full standard + protocol live in `benchmarks/cornell_box/README.md`.
Quick checklist:

- [ ] `./build/bin/sandbox --scene cornell01` opens the classic box: red LEFT
      wall, green RIGHT wall, two white blocks, bright rectangular ceiling
      emitter, camera looking in through the open front. No input (ESC quits).
- [ ] `--scene cornell02` adds three spheres: mirror (left), rough gold
      (center), glossy dielectric (right). The mirror reflects the emitter and
      lights but NOT the room (no environment specular yet — that gap is the
      M5 IBL target, visible against the reference).
- [ ] `--scene cornell03` shows the baffle under the emitter; the floor is
      lit ONLY by leaked direct light in the rasterizer (no shadows/GI yet),
      while the reference shows bounce-only light — the largest ledger gap.
- [ ] `--benchmark 300` prints a telemetry report (fps, frame/CPU/GPU ms,
      draw calls, triangles, lights = 16) and writes the final frame PPM;
      with `--report file` the same lines land in a file.
- [ ] **Acceptance harness (M3.3.1)**: `python3 tools/verify_cornell_shot.py
      <shot.ppm>` judges the M3.3 acceptance checklist from the deterministic
      screenshot — camera framing, box-silhouette black ratio, interior mean
      luminance, per-probe expected values (emitter/back wall/red wall/green
      wall/floor/block tops) derived in float64 from the frozen light grid +
      display transform, red/green dominance, and a ceiling "GI gap" pin that
      fails if fake ambient ever appears. Prints the shot md5 for the ledger;
      exit 0 iff `verdict: ACCEPTED`. Expect on hardware at 720p: global black
      ~30-35% (the open-front void border is correct, not a bug), interior
      black < 15%, red wall ≈ (130,22,17), green wall ≈ (42,104,28), back
      wall ≈ 140, short-block top ≈ 160 — bands in the harness absorb MSAA /
      specular / dither deltas.
- [ ] `tools/benchmark_compare.py <frame.ppm> <reference.ppm>` prints RMSE,
      SSIM and a "Cornell similarity" score; identical images score 100.00.
      Run AFTER the acceptance harness passes.
- [ ] (Headless container/CI, no display) the sandbox still exits gracefully
      with the standard GLFW failure messages — for cornell scenes too.

### M3.3.1 regression: the black-room failure class

The first on-hardware Cornell render was 96.5% black: r1 geometry authored
every room quad normal AWAY from the room interior (and 3 of 5 quads had
winding contradicting their own vn). The rasterizer shades one-sided; the
reference path tracer shades two-sided, so offline reference checks could
never catch it. Now unshippable by construction:

- `bench_tests` "frozen normal orientation" section (100 checks total):
  room-quad normals pinned exactly (floor +Y, ceiling −Y, left +X, right
  −X, back +Z, emitter −Y), winding·vn > 0 for every triangle of every
  variant mesh, solids face outward from their own center.
- `tools/generate_cornell.py` refuses to write geometry that fails the same
  orientation validation (fail-loud at the source).
- If a Cornell render is ever again majority-black, FIRST check the
  orientation pins (`ctest --test-dir build -R bench_tests`), THEN suspect
  the renderer — in that order.

## PBR correctness checks (M3.2)

- [ ] **Metals receive no fake ambient diffuse**: under ambient-only light
      (sun off via angle, flashlight off) the chrome sphere goes nearly black
      except for the gradient reflection and highlights — it does NOT glow
      with the ambient sky color the way pre-M3.2 metals did.
- [ ] **The chrome sphere reads as chrome**: its reflection follows the
      sky/ground gradient (brighter facing up, darker facing down) plus the
      direct highlights — structure, not a flat tint. That structure is the
      placeholder environment specular; M5 IBL replaces it wholesale.
- [ ] The **ship** (key 4) now shades as a dielectric (metalness 0) — its
      diffuse responds to light color like the rock, with a glossy sheen.
- [ ] Overall scene brightness is close to M3.1 with small shifts: dielectric
      ambient dims a few percent (Fresnel split), metal ambient changes
      character (structured reflection instead of flat tint). Exposure
      compensates if desired — that is what the scroll wheel is for.
- [ ] **V cycles six material views** (each switch prints to stdout):
      1. shaded — the normal image;
      2. normals — world normals as RGB (floor greenish, model multicolor);
      3. albedo — flat material colors, no lighting;
      4. metal-rough — metalness in red, roughness in green;
      5. F0 — dielectrics show the 4% gray, metals show their tinted F0;
      6. validation — every bundled preset renders green (a luminance-scaled
         map); violations render saturated red.
- [ ] Validation actually flags: temporarily set the ship's metalness to
      `0.5f` in `kModelAssets` and re-run — key 4 turns red in the validation
      view (intermediate metalness). Revert afterwards.
- [ ] Debug views bail before the light loop: in views 1–5 the scene renders
      identically with the flashlight on or off (no light evaluation).

## Model import

- [ ] **1 / 2 / 3 / 4** swap the hero model instantly (torus / sphere / rock /
      ship); each keeps its own material preset; the swap is printed to stdout.
- [ ] Model discovery works from ANY launch style (M3.1): running the exe by
      double-click from `build/bin/Debug`, from VS (F5), and from the project
      root all find the bundled assets. With models intentionally missing,
      the console lists every probed path (see Diagnostics).
- [ ] All four display at a consistent size (loader normalizes to radius 0.6)
      and rotate around their own center (loader centers to origin).
- [ ] Dropping any additional `.obj` into `sandbox/assets/models/` and wiring
      it into `kModelAssets` (one line) loads and displays it — including
      exports without normals (flat shading + warning).

## Input / controls

- [ ] **Left click** locks the cursor; mouse motion rotates the camera.
- [ ] **ESC** when locked unlocks; when unlocked, requests window close
      (exit code 0).
- [ ] **W / A / S / D** fly (W follows full view direction); **E / Q** up/down.
- [ ] **SPACE** freezes the quad's spin at its current angle (not a reset to 0).
- [ ] Closing the window via the close button exits cleanly with code 0.

## Frame timing / robustness

- [ ] Pausing in a debugger for >0.1 s does not explode simulation state
      (proves `dt` is clamped to `Application::kMaxDeltaTime`).
- [ ] Losing window focus mid-press does not leave a key stuck down.

## Headless / CI verification

- [ ] `./build/bin/sandbox --frames 60 --out shot.ppm` renders 60 frames on a
      deterministic 30 Hz timeline, writes a binary PPM (1280×720), and exits 0.
      Under a headless X server (e.g. `xvfb-run -s "-screen 0 1280x720x24"
      ./build/bin/sandbox --frames 60 --out shot.ppm` with Mesa llvmpipe), the
      PPM must contain the fully lit scene — usable as a pixel-diff regression
      artifact.
- [ ] In an environment with no display at all, the sandbox exits gracefully:
      `[GLFW] ...`, `[Window] glfwInit() failed`,
      `Engine initialization failed` — no crash.

## Diagnostics

| Symptom                                          | Likely cause                                                                                       |
|--------------------------------------------------|----------------------------------------------------------------------------------------------------|
| `glfwCreateWindow failed`                        | No GL 3.3+ driver (common over Remote Desktop; run on the physical GPU).                           |
| `[GLLoader] Missing entry point: glXxx`          | Broken driver install. If persistent, switch the loader to glad2 (see `ARCHITECTURE.md` → Decisions). |
| `[Renderer] HDR target incomplete`               | Driver cannot render to RGBA16F at the requested MSAA level; the renderer retries without MSAA automatically. |
| `[Renderer] Tonemap program failed`              | GLSL compiler issue; file a bug with the log (shaders are bundled, should never fail).             |
| `[Renderer] HDR targets unavailable; continuing in direct mode` | Very old driver: scene still renders, but expects a real HDR target for correct tonemapping — colors will look dark. Update the driver. |
| `[Sandbox] Model assets not found ... Searched:` | The zip layout was modified (e.g. `sandbox/assets/models/` moved or renamed). The log lists every probed path — restore the layout or launch from the project root. |
| Test exe window closes instantly (Windows)       | Pre-M3.1 builds only; current builds hold the console open on double-click. Run tests via `ctest` for automation. |
| `The syntax of the command is incorrect.` + `vector subscript out of range` | Pre-M3.1 `obj_tests` bug (`system("mkdir -p")` on cmd.exe); fixed in M3.1 — rebuild. |
| `[Sandbox] ...: no normals in file; flat shading generated` | Informational: the imported OBJ lacked normals and got flat-shaded. Re-export with normals to smooth it. |
| `[Sandbox] ...: skipped N invalid face(s)`       | The OBJ contained degenerate polygons; they were dropped safely.                                    |
| `[Shader] ... compile error`                     | Should be unreachable with the bundled shaders; file a bug with the log.                          |
| `brdf_tests` failure after editing shader math   | A shader equation changed without re-deriving the reference anchors. Run `tools/brdf_reference.py`, update `engine/tests/brdf_tests.cpp`, and read the derivation again before trusting the change. |
| Validation view shows red on a material          | Working as intended (Dota 2 rules): intermediate metalness, emitter-bright dielectric, implausible metal F0, or accidental mirror roughness. Fix the material, not the view. |
| Scene slightly darker / metals look different than M3.1 | Expected: the M3.2 ambient split removed fake diffuse from metals and Fresnel-weights dielectric ambient. |
| `[Sandbox] cornell geometry not found ... Searched:` | The zip layout was modified (`benchmarks/cornell_box/geometry/` moved). The log lists every probed path — restore the layout or launch from the project root. |
| `[Renderer] GPU timer query failed; timing disabled.` | Driver rejected the TIME_ELAPSED query; the report shows `gpu ms: n/a` and everything else keeps working. |
| `bench_tests` failure after touching `tools/generate_cornell.py` | The FROZEN standard changed — forbidden without an explicit ledger re-baseline. Restore the constants or run the re-baselining discussion first. |
| `[Renderer] GL error 0x%04x raised during drawIndexed` | Unexpected GL state. Should not appear during a clean run.                                    |
| `Skipping engine_math_tests: ... not found`      | The CMakeLists expected `engine/tests/math_tests.cpp` (plural); the file was renamed or never renamed. |

## Math + asset pipeline

Verified two ways:

1. **Transitively at runtime** — broken lighting shows up immediately: wrong
   normal matrices shear highlights off surfaces, wrong falloff kills the
   bulb pools, wrong tonemap order (sRGB before tonemap) looks washed out.
2. **Directly via CTest:**
   - `math_tests` (73 checks) — `Vec3` basics, identity laws, translation,
     rotation (incl. length preservation), perspective depth mapping,
     `lookAt` mapping, camera clamping, plus M3: transpose laws
     (`(AB)^T = B^T A^T`), inverse round-trip (`M·M⁻¹ = I` on TRS matrices),
     rotation-inverse-is-transpose, normal-matrix behavior (strips
     scale/translation, preserves perpendicularity), singular-matrix safety.
   - `obj_tests` (24 checks) — `v//vn` triangles, `v/vt/vn` quads,
     texcoord-ignored warnings, pentagon fan triangulation, negative indices,
     flat-normal fallback, degenerate-face rejection, missing-file failure
     with error string, center+radius normalization; fixture setup is
     portable (`std::filesystem`, no shell) and indexed inspections are
     guarded (a failed load reports FAILs instead of crashing).
   - `brdf_tests` (38 checks, M3.2) — reference-value suite for the shader
     math: the Karis/Filament visibility form is verified against an
     INDEPENDENT Lambda-based derivation of Heitz's exact height-correlated
     Smith (agreement to ~3e-7 float32 across a NoV/NoL/alpha grid);
     closed-form and float64 point anchors for D/V/F; ambient-split energy
     rules (metalness kills fake diffuse); deterministic white-furnace
     integrals pinning the exact D·V pairing (single-scattering deficit =
     documented multi-scattering slot, not a bug); and a regression check
     proving the removed M3.1 squared-term visibility was measurably wrong
     (15% at the grazing corner).

   Anchors are re-derivable with `tools/brdf_reference.py` (float64,
   independent algebra + quadrature). Rule: change a shader equation →
   re-derive anchors → update the test → only then judge the image.
   - `bench_tests` (100 checks) — pins the FROZEN cornell-box/1.0
     standard: standard string, exposure, camera, emitter dimensions/radiance,
     canonical reflectances, variant mesh lists; the point-light grid flux
     model (I = A·L_e/N, positions inside the emitter footprint); and — since
     M3.3.1 — the on-disk OBJs (triangle counts + exact bounds vs the
     generated header, zero degenerate faces — the pole-cap regression from
     generation is guarded here) plus FROZEN normal orientation: room-quad
     normals pinned exactly (floor +Y, ceiling −Y, left +X, right −X, back
     +Z, emitter −Y), winding·vn agreement for every triangle of every
     variant mesh, and solids facing outward from their own center.

   Rules for the benchmark live in `benchmarks/cornell_box/README.md`; the
   two-axis ledger in `benchmarks/cornell_box/BASELINE.md`.

Expected output:

```
[math] Vec3 basics
[math] identity laws
[math] translation
[math] rotation
[math] perspective
[math] lookAt
[math] camera
[math] transpose
[math] inverse
[math] 73 checks, 0 failure(s)
[obj] basic triangle (v//vn)
[obj] quad with texcoords (v/vt/vn)
[obj] pentagon, negative indices, no normals
[obj] degenerate face skipped
[obj] missing file + normalization option
[obj] 24 checks, 0 failure(s)
=== engine BRDF reference tests (M3.2) ===
[brdf] V: simplified form vs independent Lambda derivation
  alpha 0.05: max relative deviation ~3e-07
  ...
  alpha 1.00: max relative deviation ~2e-07
[brdf] closed-form anchors
[brdf] Fresnel anchors
[brdf] ambient split rules
[brdf] white-furnace reference integrals (F = 1)
  NoV 0.30 alpha 0.15: 0.893143 (ref 0.893292)
  ...
[brdf] M3.1 regression reference (old squared-term form)
  old form deviates 15.0% from exact Smith at NoV=NoL=0.975, alpha=0.7
[brdf] 38 checks, 0 failure(s)
=== engine benchmark tests (M3.3, cornell-box/1.0) ===
[bench] frozen standard pins (cornell-box/1.0)
[bench] point-light grid flux model
[bench] geometry on disk matches the generated pins
[bench] 100 checks, 0 failure(s)
```
