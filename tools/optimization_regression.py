#!/usr/bin/env python3
"""Historical comparator for the pre-GI CPU optimization revision.

The current branch intentionally corrects multibounce transport and will differ
from older baselines. Use reference_transport_tests.py for the current checks.

Usage: python3 tools/optimization_regression.py --baseline /path/to/original
Requires NumPy. Reports local CPU timings, not renderer FPS.
"""
import argparse
import importlib.util
from pathlib import Path
import statistics
import time
import numpy as np


def load(root, name):
    spec = importlib.util.spec_from_file_location(name, root / 'tools/reference_pathtracer.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--width', type=int, default=128)
    parser.add_argument('--repeats', type=int, default=3)
    args = parser.parse_args()
    if args.width < 16 or args.repeats < 1:
        parser.error('width must be at least 16 and repeats must be positive')
    height = args.width * 9 // 16
    old = load(args.baseline.resolve(), 'baseline_tracer')
    new = load(Path(__file__).resolve().parents[1], 'optimized_tracer')
    # Include parallel, near-parallel, inside-solid, miss, and empty-ray cases.
    rng = np.random.RandomState(19)
    ro = rng.uniform(-8, 8, (4096, 3))
    rd = rng.normal(size=ro.shape)
    rd /= np.linalg.norm(rd, axis=1)[:, None]
    rd[:3] = np.eye(3)
    rd[3] = [0, 0, 0]
    rd[4] = [1e-14, 1, 0]
    checks = 0
    with np.errstate(invalid='ignore'):
        for variant in ('cornell01', 'cornell02', 'cornell03'):
            before, after = old.build_scene(variant), new.build_scene(variant)
            for a, b in zip(before, after):
                for count in (0, len(ro)):
                    ta, na = a.intersect(ro[:count], rd[:count])
                    tb, nb = b.intersect(ro[:count], rd[:count])
                    ts, ns = b.intersect(ro[:count], rd[:count], normals=False)
                    np.testing.assert_array_equal(ta, tb)
                    np.testing.assert_array_equal(na, nb)
                    np.testing.assert_array_equal(ta, ts)
                    assert ns is None
                    checks += 1
            e0 = next(p for p in before if p.is_emitter)
            e1 = next(p for p in after if p.is_emitter)
            for direct in (False, True):
                timing = [[], []]
                for trial in range(args.repeats):
                    seed = (7, 31, 97)[trial % 3]
                    jitter = np.random.RandomState(seed).random((args.width * height, 2)) - 0.5
                    o, d = old.camera_rays(args.width, height, old.gc.CAMERA, jitter)
                    outputs = [None, None]
                    # Alternate order to reduce timing bias. Copy ray arrays
                    # because tracing may mutate them in future implementations.
                    for index in ((0, 1) if trial % 2 == 0 else (1, 0)):
                        module, prims, emitter = ((old, before, e0), (new, after, e1))[index]
                        start = time.process_time()
                        outputs[index] = module.trace(prims, emitter, o.copy(), d.copy(),
                                                      np.random.RandomState(seed), 6, direct)
                        timing[index].append(time.process_time() - start)
                    np.testing.assert_array_equal(*outputs)
                    checks += 1
                a, b = (statistics.median(t) for t in timing)
                print(f'{variant} {"direct" if direct else "6 bounces"}: exact match; '
                      f'CPU median {a*1000:.2f} -> {b*1000:.2f} ms ({a/b:.2f}x)')
    print(f'{checks} exact comparison cases passed.')


if __name__ == '__main__':
    main()
