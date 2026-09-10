#!/usr/bin/env python3
"""Fuzz the reference solvers against refsolve on small random graphs.

    make -C tools/ref && make -C tools/ref solvers
    python3 tools/fuzz_solvers.py

Graph shapes matter more than graph size here. The first version of this
fuzzer used only sparse random graphs and passed everything, while the CH
solver was in fact wrong on two of the six dev instances. The bug needed a
node with many in- and out-neighbours to show up: contracting such a node
inserts several shortcuts, and inserting them *during* the scan let a later
witness search travel along one -- proving an alternative route that in truth
went through the node being contracted, and so suppressing a shortcut that was
needed.

So the shapes below deliberately include a 3D lattice (degree 6, large
separators) and a hub graph (a few nodes of very high degree). Both reproduce
that class of bug at V = 200 in under a second.
"""

import os
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REF = ROOT / "tools/ref/refsolve"
SOLVERS = ["bidij", "alt", "ch"]
TMP = Path("/tmp/fuzz_solvers")


def write(path, V, edges, directed, queries):
    with open(f"{path}.graph", "w") as f:
        f.write(f"{V} {len(edges)} {2 if directed else 0}\n")
        for u, v, w in edges:
            f.write(f"{u} {v} {w}\n")
    with open(f"{path}.queries", "w") as f:
        f.write(f"{len(queries)}\n")
        for s, t in queries:
            f.write(f"{s} {t}\n")


def g_random(rng, V):
    e = {(rng.randrange(v), v) for v in range(1, V)}       # keep it connected
    while len(e) < V * 3:
        a, b = rng.randrange(V), rng.randrange(V)
        if a != b:
            e.add((a, b))
    return V, e


def g_lattice3d(rng, V):
    n = max(3, round(V ** (1 / 3)))
    V = n ** 3
    e = set()
    for i in range(V):
        x, y, z = i % n, (i // n) % n, i // (n * n)
        for dx, dy, dz in ((1, 0, 0), (0, 1, 0), (0, 0, 1)):
            j = ((x + dx) % n) + ((y + dy) % n) * n + ((z + dz) % n) * n * n
            if i != j:
                e.add((min(i, j), max(i, j)))
    return V, e


def g_hub(rng, V):
    """A few very high-degree nodes -- the shape that broke CH."""
    hubs = list(range(4))
    e = set()
    for v in range(4, V):
        for h in rng.sample(hubs, 2):
            e.add((v, h) if rng.random() < 0.5 else (h, v))
        u = rng.randrange(v) if v else 0
        if u != v:
            e.add((u, v))
    return V, e


SHAPES = [("random", g_random), ("lattice3d", g_lattice3d), ("hub", g_hub)]


def main():
    if not REF.exists():
        sys.exit("build first: make -C tools/ref && make -C tools/ref solvers")
    TMP.mkdir(exist_ok=True)
    budgets = sys.argv[1:] or ["0.0", "0.5", "8.0"]

    failures = []
    cases = 0
    for shape_name, shape in SHAPES:
        for seed in range(12):
            rng = random.Random(seed * 7919 + hash(shape_name) % 1000)
            directed = seed % 2 == 0
            V, pairs = shape(rng, rng.choice([60, 130, 220]))
            edges = [(u, v, rng.randint(1, 10_000)) for u, v in sorted(pairs)]
            queries = [(rng.randrange(V), rng.randrange(V)) for _ in range(300)]
            base = TMP / f"{shape_name}_{seed}"
            write(base, V, edges, directed, queries)

            subprocess.run([str(REF), "answers", f"{base}.graph", f"{base}.queries",
                            f"{base}.ref"], capture_output=True, check=True)
            expect = open(f"{base}.ref").read().split()

            for solver in SOLVERS:
                for budget in (budgets if solver == "ch" else ["8.0"]):
                    cases += 1
                    env = dict(os.environ, CH_SHORTCUT_BUDGET=budget)
                    subprocess.run([str(ROOT / "tools/ref/solvers" / solver),
                                    f"{base}.graph", f"{base}.queries", f"{base}.out"],
                                   capture_output=True, check=True, env=env)
                    got = open(f"{base}.out").read().split()
                    if got != expect:
                        bad = [(k, a, b) for k, (a, b) in enumerate(zip(expect, got)) if a != b]
                        too_big = sum(1 for _, a, b in bad if int(b) > int(a))
                        failures.append(
                            f"{solver} budget={budget} shape={shape_name} seed={seed} "
                            f"V={V} directed={directed}: {len(bad)}/{len(expect)} wrong "
                            f"({too_big} too large), first q{bad[0][0]} "
                            f"expected={bad[0][1]} got={bad[0][2]}")

    print(f"{cases} cases across {len(SHAPES)} shapes")
    for f in failures[:10]:
        print("  FAIL " + f)
    print(f"\n{len(failures)} failures" if failures else "\nall cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
