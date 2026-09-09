# Verification results

- All 423 existing CPU checks passed (math 87, frustum 32, OBJ 34, BRDF 38, benchmark 232).
- The CPU suites also passed with UndefinedBehaviorSanitizer, including linkage of the new GI module.
- The edited sandbox translation unit and GI implementation compiled with GCC C++17, `-O2 -Wall -Wextra -Wpedantic`, without warnings.
- All Python tool files passed syntax checks.
- All 15 assembled shader variants compiled on a headless Mesa OpenGL context: PBR with both area-light solvers and GI off/on, existing utility shaders, and the new GI shaders.
- Real engine meshes/HDR targets and the actual GI update/sampling shaders rendered all three Cornell scenes at 640×360, with GI off and on. Indirect-only renders used 48 updates, 8 paths per texel, and the default 16×16 cache faces/history 32.
- The selected previously unlit tall-box face increased from mean display value 0.13 to 16.72 in the indirect-only test.
- A light-off edit cleared the indirect lighting on the very next update. The resulting image contained only the tonemap's near-black dither (maximum <= 2).
- Reference tests confirmed ray advancement, preservation of caller input arrays, no repeated lighting on an escaping path, emitter termination, and mixture-PDF energy integration. The image-metric tool's 16 existing self-checks passed.

The included PNGs show **only indirect lighting**, not the final direct+indirect image. Their black emitter and dark overall exposure are intentional. Noise and coarse spatial detail remain visible at this budget, particularly in cornell03.

## Full-renderer and hardware limits

The complete PPC + GI render was attempted with two software-driver paths. The first took excessively long on the initial full-scene draw and was stopped. The second (Mesa softpipe) crashed while linking the PBR shader. A separate test reproduced the same softpipe crash when linking the original, unmodified PBR shader from the uploaded ZIP. These attempts do not establish the final full-renderer result on the target GPU.

The GLFW application was not linked or interactively exercised here. The available compiler checks, assembled GLSL compilation, and real GI integration are narrower than running the complete application on Windows/NVIDIA. RTX 3060 frame times, full direct+GI screenshots, resize/minimize interactions, and quality comparisons against the corrected multibounce reference remain target-machine checks. No FPS improvement or no-regression guarantee is claimed.
