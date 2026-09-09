# Realtime diffuse transport update

The Cornell solver now adds **one realtime diffuse bounce**. Colored walls can illuminate the boxes and floor. The direct-light emitter-angle defect is corrected. This is an OpenGL 3.3 implementation using GPU shader ray queries over the existing analytic Cornell geometry; it does not use RTX hardware traversal or a baked lighting file.

## Design: shared work, measured cost

The new `engine::DiffuseGI` computes incoming indirect irradiance in a small atlas attached to world-space surfaces. The scene renderer samples that atlas instead of launching secondary rays independently for every screen pixel. Geometry, normals, and materials come from the same loaded Cornell meshes used to draw the scene.

A single intersection routine handles both nearest-surface bounce rays and light-visibility rays. It supports axis-aligned boxes, bounded room planes, and analytic spheres. The estimator samples one diffuse surface interaction followed by the rectangular emitter, with the correct material factors and light-sampling PDF. It mixes emitter-area and cosine-hemisphere sampling near large lights to reduce variance without clipping radiance. Direct emitter hits are excluded from the indirect cache, so the existing direct-light term is not counted twice.

The atlas updates **every rendered frame**. Two GPU textures hold the current estimate and its history. Initial frames use a running average; after the configured history length, updates use an exponential average with weight `1/history`. `setScene()` automatically resets history when geometry or materials change; `setLight()` resets it when the source changes. Camera movement does not require reprojection because the cache lives on surfaces. No cache is saved or loaded from disk.

At the defaults (16×16 interior texels per face, 8 paths per update), Cornell01 has 17 active faces including its two boxes. Including the filtering border, that launches 44,064 bounce paths per frame, each with at most one additional light-visibility ray. Inactive atlas faces exit immediately. Both RGBA16F atlas textures together occupy 497,664 bytes (486 KiB). These are work/memory counts, **not measured RTX 3060 timings**.

The existing PPC direct-light solver remains responsible for detailed direct shadows. Its rect-emitter approximation now includes `max(L.y, 0)` and rejects receivers above the one-sided emitter. The existing exact-area variant remains available. Replacing those direct-shadow algorithms wholesale with the coarse cache would sacrifice detail; this update shares diffuse transport at the surface level while preserving the established direct-shadow path.

## Run and compare

Build the project normally in Release mode. On a single-configuration build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The executable is `build/bin/sandbox` on single-configuration builds, or typically `build/bin/Release/sandbox.exe` with Visual Studio. Example Windows commands from the project root:

```powershell
# Corrected direct light only
.\build\bin\Release\sandbox.exe --scene cornell01 --benchmark 1000 --gi 0 --out direct.ppm --report direct.txt

# Realtime color bounce, default quality
.\build\bin\Release\sandbox.exe --scene cornell01 --benchmark 1000 --gi 1 --out bounce.ppm --report bounce.txt

# Same GI workload with timer queries disabled: checks measurement overhead
.\build\bin\Release\sandbox.exe --scene cornell01 --benchmark 1000 --gi 1 --gpu-timing 0 --out bounce-no-timer.ppm --report bounce-no-timer.txt

# Higher sampling and surface detail, at additional GPU cost
.\build\bin\Release\sandbox.exe --scene cornell01 --benchmark 1000 --gi 1 --gi-rays 32 --gi-res 32 --gi-history 64 --out bounce-quality.ppm --report bounce-quality.txt
```

Repeat for cornell02 and cornell03. Hold scene, resolution, MSAA, exposure, frame count, and compiler settings constant. Use at least 64 frames for ordinary screenshots so startup noise has settled. GI is stochastic but its sequence is deterministic for a fixed execution path/frame count; screenshots from different frame numbers need not be identical.

| Option | Default | Range / meaning |
|---|---:|---|
| `--gi` | 1 | 0/1; active for Cornell scenes with shadows and area lighting enabled |
| `--gi-rays` | 8 | 1–64 paths per active cache texel per frame |
| `--gi-res` | 16 | 4–64 interior texels per face dimension; cost grows approximately quadratically |
| `--gi-history` | 32 | 1–128; averaging time constant, with explicit invalidation on edits |
| `--gpu-timing` | 1 | 0/1; disables only timing queries, preserving benchmark pacing/workload |
| `--area-fast` | 1 | Existing PPC / exact-area direct-light variant selection |

The benchmark's GPU query encloses the GI update as well as scene drawing and tonemapping. The report lists the extra GI pass separately from mesh draw calls. The old “GPU wait” label has been replaced by “timer-section”: that interval includes polling, result retrieval, error checking and query begin, so it does not isolate a driver wait. The report no longer claims that CPU optimization is exhausted.

## Scope and limitations

- This release handles the analytic Cornell scenes, not arbitrary triangle-mesh GI in the demo. Imported demo models do not silently use their bounding boxes as GI geometry.
- It adds diffuse-to-diffuse indirect transport. Metals receive no diffuse bounce; indirect glossy reflections, caustics, transmission, and additional bounces are not implemented.
- The low-resolution cache and finite sample budget can produce visible noise, coarse shading, filtering artifacts near occluder edges, and sphere-face seams. GI-only diagnostics make this particularly visible. Increase rays/history for lower variance and resolution for finer detail, while measuring the extra cost.
- Analytic spheres approximate the tessellated Cornell sphere meshes. Planes and axis-aligned boxes use their loaded bounds.
- The frozen scene still has no moving-object editor. The GI API supports invalidating edited scene/light data; a future dynamic scene system must call those setters and also update the direct renderer's corresponding data.
- No frame-rate guarantee has been established for an RTX 3060. The default controls are a starting point for measurement.

## Reference tracer corrections

The previous multibounce reference loop sampled new directions but never wrote new ray origins/directions back for the next bounce. It repeatedly revisited the initial camera intersection. It also divided full-BSDF contributions by only the selected lobe's PDF instead of the complete mixture PDF, and allowed primary emitter hits to continue shading.

These are corrected in `tools/reference_pathtracer.py`. The tracer now owns mutable path state, advances rays after each bounce, uses the mixture density, and terminates emitter hits after counting primary emission. Existing reference PPMs are preserved as historical assets; they have **not** been silently regenerated and should not be treated as validated multibounce truth for this release. The old `optimization_regression.py` is a historical pre-GI comparator and will intentionally disagree with the corrected multibounce tracer.

## Verification

```sh
python3 tools/run_cpu_tests.py
python3 tools/run_cpu_tests.py --sanitize undefined
python3 tools/reference_transport_tests.py
python3 tools/benchmark_compare.py --self-test
python3 tools/glsl_validate.py .
```

For Linux with installed EGL/OpenGL libraries, a C++17 compiler, NumPy, and Pillow:

```sh
python3 tools/verify_realtime_gi.py --out gi-validation
# Optional full PPC + GI test; software-driver execution may be slow:
python3 tools/verify_realtime_gi.py --full-lighting --width 128 --frames 2 --out full-validation
```

The default EGL test renders **indirect lighting only** through real engine meshes, HDR targets, and the actual GI update/sampling shaders, so failures can be isolated from the older PPC shader. A black emitter in these diagnostic images is intentional. It compiles all 15 shader variants, renders the three Cornell variants with GI off/on, checks indirect light reaches an otherwise dark box face, and checks a light-off edit clears the previous lighting immediately.

Completed checks: 423 CPU checks (also under UndefinedBehaviorSanitizer), 15 real GLSL shader compilations, all three Cornell indirect-only GL renders, and immediate light-off history reset. Full PPC rendering encountered software-driver limitations, including a link crash also reproduced with the original shader. RTX 3060 performance and full application rendering remain unverified. Validation logs and selected diagnostic images are in `docs/realtime-validation/`. Historical CPU-optimization notes remain in `OPTIMIZATION_REVIEW.md`; their unchanged-pixel claims apply only to that earlier revision, not to the intentional lighting changes here.
