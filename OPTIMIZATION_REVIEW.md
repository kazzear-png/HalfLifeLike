> Historical report for the preceding CPU-optimization revision. Its unchanged-shader/reference claims describe that revision only. This branch adds lighting and reference-transport corrections; see [REALTIME_GI.md](REALTIME_GI.md).

# Optimization review

This update removes redundant CPU work and improves capture/report reliability. It preserves the existing shader algorithms, quality settings, scene assets, reference images, and rendering order. It does not claim that every possible optimization is exhausted or that GPU performance has been verified.

## Changes

- **OBJ loading:** reuse the line stream, tag/token strings, and triangle scratch vector across the file. Replace the unused UV array with a presence flag. Use one hash-table insertion/lookup for vertex deduplication instead of a lookup followed by a second insertion lookup. Strict parsing, triangulation, normals, normalization, and warnings retain their existing behavior.
- **Camera work:** rebuild projection only after a framebuffer-size change. Reuse the frozen Cornell view-projection matrix and frustum until resize. The interactive demo still updates its view/frustum each frame.
- **Materials:** resolve Cornell material names at scene setup. Adjacent visible meshes sharing the same material reuse the uploaded values. Tracking resets each frame, and no mesh is reordered.
- **Demo correctness:** when the floor is culled but the spinning quad is visible, explicitly restore the quad's shared material fields. Previously the quad could inherit the preceding frame's hero material.
- **Screenshot output:** convert bottom-up RGBA to top-down RGB one row at a time. A 1920×1080 capture now uses 1,080 pixel-data `fwrite` calls instead of 2,073,600. Additional scratch memory is 3×width bytes (5,760 bytes at 1920 pixels). Header, write, and close failures are checked.
- **Capture/report reliability:** failed screenshots and report writes return a nonzero exit status. Benchmark descriptions have enough space for their full mode details. Culling statistics now exclude warmup frames, matching their denominator.
- **Reference tracer:** shadow intersections skip constructing normals, AABB normal-axis/sign arrays, and sphere hit-point normals that visibility never consumes. Nearest-distance updates reuse their output array; boolean-indexed normals no longer receive a redundant second copy. Random streams and transport equations are unchanged.
- **Verification tools:** add a standalone CPU-test runner requiring no GLFW download and an exact baseline comparison tool for the tracer. The GLSL validator checks both `AREA_FAST_BUILD=0` and `1`, reports a missing compiler clearly, and fails when required sources are missing or no shaders were found.

## Verification completed

| Check | Result |
|---|---|
| Original CPU baseline | 423 checks passed across math, frustum, OBJ, BRDF, and benchmark suites |
| Updated CPU suites | Same 423 checks passed |
| UndefinedBehaviorSanitizer | All five suites passed; no runtime diagnostics |
| Edited sandbox translation unit | Compiled with GCC, C++17, `-O2 -Wall -Wextra -Wpedantic`; no warnings |
| OBJ output comparison | All 16 bundled assets plus 6 generated test fixtures: byte-identical vertices, indices, and warnings, both raw and normalized |
| Screenshot serialization | Exact original/updated PPM byte equality at 1×1, 7×3, 960×540, and 1920×1080 |
| Screenshot write failure | `/dev/full` correctly returns failure |
| Reference tracer | 110 exact comparison cases at 256×144 across all three scenes, direct and six-bounce modes, fixed seeds; primitive comparisons include empty batches and parallel rays |
| Image metric self-tests | All 16 checks passed |
| Python syntax | All tool files parse |
| Asset preservation | All bundled OBJ assets, reference PPMs, scene JSON, and shader source bytes preserved |

Raw CPU and comparison logs are in `docs/optimization-validation/`.

## Performance interpretation and remaining validation

The reduction in work is concrete: fewer allocations, material string searches, repeated matrix calculations, uniform uploads, and file-write calls. Local timings are microbenchmarks on a shared execution host, not renderer FPS. Scheduling and load produced substantial variation in early timing runs; do not treat individual timing ratios as universal speed guarantees. The larger tracer run (nine paired samples per mode at 256×144) measured 1.05–1.25× improvements in five cases and a 0.98× result in cornell03 direct mode (about 2% slower in that run). That small-case result is retained transparently in the log; performance needs confirmation on the target machine.

The full GLFW application was not linked or run here. CMake, GLFW development files, and glslangValidator were unavailable. Therefore GPU timings, real-driver GLSL compilation, visual screenshots from the renderer, resizing/minimize interactions, and the demo culling fix still require a graphics-capable machine. Shader source was deliberately preserved. Address/LeakSanitizer could not complete because the sandbox prevents its process inspection; the successful sanitizer run used UndefinedBehaviorSanitizer only.

Existing PPC box-overlap approximations, sphere visibility sampling, polygon capacities, shadow bias, framebuffer formats, MSAA, precision, and synchronization behavior were left intact. Changing these without GPU image/performance comparisons could trade correctness or quality for speed.

## Reproduce

From the project root, with a GCC/Clang-compatible C++ compiler and Python:

```sh
python3 tools/run_cpu_tests.py
python3 tools/run_cpu_tests.py --sanitize undefined
python3 tools/benchmark_compare.py --self-test
python3 tools/optimization_regression.py --baseline /path/to/original-project
```

The metric and tracer tools require NumPy. For a larger tracer comparison, add `--width 256 --repeats 9`. Timing output measures process CPU time.

On a graphics-capable development machine:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python3 tools/glsl_validate.py .
```

Run original and updated builds with identical compiler settings, resolution, MSAA, scene, and shadow options. Compare captured images for cornell01/02/03 and both area-light variants, then compare warmed-up frame, CPU, and GPU times using the existing benchmark commands in README.md. Also exercise the moving demo camera, floor culling, window resize, and output failures. No GPU/FPS improvement is claimed in this package.
