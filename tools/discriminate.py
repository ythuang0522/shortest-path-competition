#!/usr/bin/env python3
"""Does the dataset actually separate different levels of solution?

    make -C tools/ref solvers
    python3 tools/discriminate.py --instances instances.txt

Runs the three reference solvers in tools/ref/solvers through grade.py and
tabulates the result. They share an I/O layer and a heap implementation on
purpose, so any spread here comes from the algorithm rather than from who
wrote a better parser.

This is the check that decides whether the redesign worked. Nice-looking data
is worth nothing if a plain bidirectional search, a landmark-guided one and a
hierarchy all land within a factor of two of each other -- that is a
leaderboard nobody can be ranked on, which is the problem the redesign exists
to fix. If they bunch up, raise Q and try again.
"""

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOLVERS = [
    ("bidij", "CSR + bidirectional Dijkstra, no preprocessing"),
    ("alt", "ALT, 16 landmarks"),
    ("ch", "contraction hierarchy with a core"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--instances", default="instances.txt")
    ap.add_argument("--timeout", type=float, default=1800)
    ap.add_argument("--json", type=Path)
    args = ap.parse_args()

    results = {}
    with tempfile.TemporaryDirectory() as tmp:
        for name, _desc in SOLVERS:
            binary = ROOT / "tools/ref/solvers" / name
            if not binary.exists():
                sys.exit(f"{binary} not built -- run: make -C tools/ref solvers")
            out = Path(tmp) / f"{name}.json"
            print(f"running {name} ...", flush=True)
            r = subprocess.run(
                [sys.executable, str(ROOT / "grade.py"), "--solver", str(binary),
                 "--instances", args.instances, "--json", str(out),
                 "--timeout", str(args.timeout)],
                cwd=ROOT, capture_output=True, text=True)
            if not out.exists():
                print(r.stdout, r.stderr)
                sys.exit(f"grade.py failed for {name}")
            results[name] = json.loads(out.read_text())

    names = [n for n, _ in SOLVERS]
    instances = [Path(i["queries"]).name.replace(".queries", "")
                 for i in results[names[0]]["instances"]]

    print()
    print(f"{'instance':<18}" + "".join(f"{n:>14}" for n in names))
    print("-" * (18 + 14 * len(names)))
    for k, inst in enumerate(instances):
        cells = []
        for n in names:
            rec = results[n]["instances"][k]
            cells.append(f"{rec['speedup']:.2f}" if rec["note"] == "ok"
                         else rec["note"])
        print(f"{inst:<18}" + "".join(f"{c:>14}" for c in cells))

    print("-" * (18 + 14 * len(names)))
    for key, label in (("overall_geomean", "OVERALL geomean"),
                       ("global_track_geomean", "  global track"),
                       ("local_track_geomean", "  local track")):
        print(f"{label:<18}" + "".join(f"{results[n][key]:>14.2f}" for n in names))

    overall = [results[n]["overall_geomean"] for n in names]
    print()
    for a, b in zip(names, names[1:]):
        ra, rb = results[a]["overall_geomean"], results[b]["overall_geomean"]
        print(f"  {b} vs {a}: {rb / ra:.1f}x")
    spread = max(overall) / min(overall) if min(overall) > 0 else float("inf")
    print(f"  best vs worst: {spread:.1f}x")

    clipped = [n for n in names
               if any(i.get("raw_speedup", 0) >= 1e6 for i in results[n]["instances"])]
    if clipped:
        print(f"\n  WARNING: {clipped} hit the score cap")

    if args.json:
        args.json.write_text(json.dumps(results, indent=2) + "\n")
        print(f"\nwrote {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
