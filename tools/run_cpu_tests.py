#!/usr/bin/env python3
"""Build and run the five CPU suites without downloading GLFW or needing a GPU.

Usage: python3 tools/run_cpu_tests.py [--cxx g++] [--sanitize]
Normal CMake/CTest builds remain supported. Temporary build files are removed.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--sanitize', nargs='?', const='address,undefined',
                        choices=('address,undefined', 'undefined'),
                        help='enable GCC/Clang sanitizers (default: address,undefined)')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sources = ['math/Mat4.cpp', 'math/Frustum.cpp', 'rendering/Camera.cpp',
               'assets/OBJ.cpp', 'rendering/ShadowHeightfield.cpp',
               'rendering/Shader.cpp', 'rendering/GL.cpp', 'rendering/DiffuseGI.cpp']
    flags = ['-std=c++17', '-O1' if args.sanitize else '-O2',
             '-I' + str(root / 'engine/src'), '-I' + str(root / 'sandbox/src')]
    if args.sanitize:
        flags += ['-g', '-fno-omit-frame-pointer', '-fsanitize=' + args.sanitize]
    try:
        with tempfile.TemporaryDirectory(prefix='engine-tests-') as build:
            objects = []
            for index, source in enumerate(sources):
                obj = str(Path(build) / f'{index}.o')
                subprocess.run([args.cxx, *flags, '-c',
                                str(root / 'engine/src' / source), '-o', obj], check=True)
                objects.append(obj)
            for suite in ('math', 'frustum', 'obj', 'brdf', 'bench'):
                exe = str(Path(build) / (suite + ('.exe' if os.name == 'nt' else '')))
                subprocess.run([args.cxx, *flags,
                                '-DCORNELL_GEOMETRY_DIR="' +
                                str(root / 'benchmarks/cornell_box/geometry') + '"',
                                str(root / 'engine/tests' / f'{suite}_tests.cpp'),
                                *objects, '-o', exe], check=True)
                subprocess.run([exe], cwd=root / 'engine/tests', check=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f'CPU verification failed: {exc}\n')
    print('All five CPU suites passed.')


if __name__ == '__main__':
    main()
