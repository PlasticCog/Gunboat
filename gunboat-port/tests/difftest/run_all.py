"""Builds gb_difftest and runs every differential test (test_*.py, TESTS lists).

usage: run_all.py [--no-build] [--scale N] [--seed N] [-k SUBSTRING]
  --scale  multiplies the number of random cases (default 1)
  --seed   random seed (default 19900701; a failure prints the seed to reproduce it)
  -k       only tests whose name contains SUBSTRING
Writes gunboat-port/out/difftest-report.json. Exit code 1 if any test failed.
"""
import argparse
import importlib
import json
import pathlib
import random
import sys
import time
import traceback

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gbdiff  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--no-build', action='store_true')
    ap.add_argument('--scale', type=int, default=1)
    ap.add_argument('--seed', type=int, default=19900701)
    ap.add_argument('-k', default='')
    ap.add_argument('--dll')
    a = ap.parse_args()
    if not a.no_build and not a.dll:
        gbdiff.build()
    harnesses = {}  # one per video card (a test's `machine` attribute, default 'vga')
    results, failed = [], 0
    for path in sorted(HERE.glob('test_*.py')):
        module = importlib.import_module(path.stem)
        for test in module.TESTS:
            name = '%s.%s' % (path.stem, test.__name__)
            if a.k not in name:
                continue
            machine = getattr(test, 'machine', 'vga')
            if machine not in harnesses:
                harnesses[machine] = gbdiff.Harness(a.dll, machine)
            h = harnesses[machine]
            rng = random.Random('%d:%s' % (a.seed, name))
            t0 = time.time()
            try:
                cases = test(h, rng, a.scale)
                status, detail = 'pass', ''
            except gbdiff.Mismatch as e:
                cases, status, detail = None, 'FAIL', str(e)
            except Exception:
                cases, status, detail = None, 'ERROR', traceback.format_exc()
            dt = time.time() - t0
            print('%-40s %-5s %8s cases  %6.1f s' % (name, status, cases if cases is not None else '-', dt), flush=True)
            if detail:
                failed += 1
                print('    ' + detail.replace('\n', '\n    '))
            results.append(dict(test=name, status=status, cases=cases, seconds=round(dt, 1), detail=detail))
    report = dict(seed=a.seed, scale=a.scale, failed=failed, tests=results,
                  compared='all memory (1 MB + 64 KB) except the original\'s stack below the test SP; '
                           'return registers as listed per test',
                  oracle='unmodified GB.EXE code in Unicorn; the port built with GCC')
    out = gbdiff.PORT / 'out'
    out.mkdir(exist_ok=True)
    (out / 'difftest-report.json').write_text(json.dumps(report, indent=2) + '\n')
    total = sum(r['cases'] or 0 for r in results)
    print('%d tests, %d failed, %d cases compared (seed %d)' % (len(results), failed, total, a.seed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
