#!/usr/bin/env python3
# M5.0 hotfix discipline: REAL GLSL compile gate.
# glsl_balance.py is a regex lint (balance/invariants) and CANNOT catch type
# errors -- the C1102 array-size mismatch shipped in M5.0 proved that. This
# script extracts every R"GLSL(...)GLSL" shader string from the sources below
# and compiles it with the standalone Khronos glslangValidator (no GPU needed).
#
# Usage: python3 tools/glsl_validate.py [repo_root]
# Exit 0 = all shaders compile; 1 = any compile error (print verbatim).
import os, re, shutil, subprocess, sys, tempfile


def _find_glslang() -> str:
    # 1) explicit override; 2) PATH (glslangValidator from a real install /
    #    SDK); 3) the local standalone binary used by the offline gate.
    env = os.environ.get("GLSLANG")
    if env and os.path.isfile(env):
        return env
    for name in ("glslangValidator", "glslang"):
        p = shutil.which(name)
        if p:
            return p
    local = "/home/z/my-project/tools-bin/bin/glslang"
    return local if os.path.isfile(local) else "glslangValidator"


GLSLANG = _find_glslang()

SOURCES = [
    "sandbox/src/shaders.h",
    "engine/src/rendering/Renderer.cpp",
    "engine/src/rendering/ShadowHeightfield.cpp",
    "engine/src/rendering/DiffuseGI.cpp",
]

BLOCK_RE = re.compile(r'const char\*\s+(\w+)\s*=\s*R"GLSL\((.*?)\)GLSL"', re.S)


def stage_of(name: str, body: str) -> str:
    n = name.lower()
    if n.endswith("vertex") or n.endswith("vs"):
        return "vert"
    if n.endswith("fragment") or n.endswith("fs"):
        return "frag"
    # content heuristics
    if "gl_Position" in body:
        return "vert"
    if "FragColor" in body or "out vec4" in body:
        return "frag"
    return "comp"


def main() -> int:
    root = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
    if not shutil.which(GLSLANG):
        print("[FAIL] glslangValidator is unavailable; install it or set GLSLANG.", file=sys.stderr)
        return 1
    failures = 0
    checked = 0
    gi_path = os.path.join(root, "engine/src/rendering/DiffuseGI.cpp")
    with open(gi_path, encoding="utf-8") as gi_file:
        gi_blocks = dict(BLOCK_RE.findall(gi_file.read()))
    common = gi_blocks["kGiCommon"]
    with tempfile.TemporaryDirectory() as td:
        for rel in SOURCES:
            path = os.path.join(root, rel)
            if not os.path.isfile(path):
                print(f"[FAIL] missing {rel}")
                failures += 1
                continue
            text = open(path, "r", encoding="utf-8").read()
            for m in BLOCK_RE.finditer(text):
                name, body = m.group(1), m.group(2)
                if name == "kGiCommon":
                    continue
                # GLSL requires #version on the FIRST line of the string after
                # the raw-string opener; files store it on the next line, so
                # trim a single leading newline to match the shipped semantics
                # AND keep line numbers identical to the driver's view.
                body = body[1:] if body.startswith("\n") else body
                stage = stage_of(name, body)
                # The application specializes PBR at runtime. Compile BOTH
                # branches: the default macro alone misses the exact-area path.
                variants = [(name, body)]
                if name == "kPbrFragment":
                    first, rest = body.split("\n", 1)
                    variants = [(f"{name}_area{mode}_gi{enabled}",
                                 first + f"\n#define AREA_FAST_BUILD {mode}\n#define SURFACE_GI_BUILD {enabled}\n"
                                 + (common if enabled else "") + rest)
                                for mode in (0, 1) for enabled in (0, 1)]
                elif name == "kGiUpdateFragment":
                    first, rest = body.split("\n", 1)
                    variants = [(name, first + "\n" + common + rest)]
                for label, source in variants:
                    checked += 1
                    sp = os.path.join(td, f"{label}.{stage}")
                    with open(sp, "w", encoding="utf-8") as shader_file:
                        shader_file.write(source)
                    r = subprocess.run([GLSLANG, sp], capture_output=True, text=True)
                    ok = r.returncode == 0
                    status = "PASS" if ok else "FAIL"
                    print(f"[{status}] {rel} :: {label} ({stage})")
                    if not ok:
                        failures += 1
                        for line in (r.stdout + r.stderr).strip().splitlines():
                            print("      " + line)
    if checked == 0:
        failures += 1
        print("[FAIL] no shaders found")
    print(f"--- {checked} shaders checked, {failures} failed ---")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
