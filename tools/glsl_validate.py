#!/usr/bin/env python3
# M5.0 hotfix discipline: REAL GLSL compile gate.
# glsl_balance.py is a regex lint (balance/invariants) and CANNOT catch type
# errors -- the C1102 array-size mismatch shipped in M5.0 proved that. This
# script extracts every R"GLSL(...)GLSL" shader string from the sources below
# and compiles it with the standalone Khronos glslangValidator (no GPU needed).
#
# Usage: python3 scripts/glsl_validate.py [repo_root]
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
    failures = 0
    checked = 0
    with tempfile.TemporaryDirectory() as td:
        for rel in SOURCES:
            path = os.path.join(root, rel)
            if not os.path.isfile(path):
                print(f"[SKIP] missing {rel}")
                continue
            text = open(path, "r", encoding="utf-8").read()
            for m in BLOCK_RE.finditer(text):
                checked += 1
                name, body = m.group(1), m.group(2)
                # GLSL requires #version on the FIRST line of the string after
                # the raw-string opener; files store it on the next line, so
                # trim a single leading newline to match the shipped semantics
                # AND keep line numbers identical to the driver's view.
                body = body[1:] if body.startswith("\n") else body
                stage = stage_of(name, body)
                sp = os.path.join(td, f"{name}.{stage}")
                open(sp, "w", encoding="utf-8").write(body)
                r = subprocess.run([GLSLANG, sp], capture_output=True, text=True)
                ok = r.returncode == 0
                status = "PASS" if ok else "FAIL"
                print(f"[{status}] {rel} :: {name} ({stage})")
                if not ok:
                    failures += 1
                    err = (r.stderr or r.stdout or "").strip()
                    for line in err.splitlines():
                        print("      " + line)
    print(f"--- {checked} shaders checked, {failures} failed ---")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
