# Shortest-Path Algorithms Competition

Your job: take the provided Dijkstra implementation and make it **faster** on
the released graph instances, without changing its output.

This README contains everything you need.

## What's in this repo

```
dijkstra_foundation.cpp   the baseline you must build on
Makefile                  builds  `foundation`  and  `solver`
grade.py                  scoring script (correctness + speedup)
instances.txt             the instance list grade.py reads
samples/
    tiny.graph            6-node hand-built graph
    tiny.queries          7 queries
    tiny.expected         expected output (for sanity)
instances/
    *_dev.{graph,queries,answers}    dev tier, committed here
    *_medium.*                       download, unscored, for iteration
    *_large.*                        download, THIS IS THE SCORED SET
checksums/                SHA-256 of every distributed file
scripts/download_large.sh fetch the medium and large tiers
tools/gen/                the dataset generator (see "Practice instances")
tools/ref/                reference solver used to produce the answer keys
```

## Tiers

| Tier | Where | Scored | Purpose |
|---|---|:--:|---|
| `_dev` | committed here | no | correctness suite; small enough to run the foundation on directly |
| `_medium` | `scripts/download_large.sh medium` | no | iteration at realistic size |
| `_large` | `scripts/download_large.sh` | **yes** | the graded run |

```sh
scripts/download_large.sh          # large tier
scripts/download_large.sh all      # medium + large
```

Downloads are gzipped and checksum-verified on arrival, so a truncated
download fails loudly instead of turning into a mysterious wrong answer.

## 1. Build and sanity-check the foundation

```sh
make foundation
./foundation samples/tiny.graph samples/tiny.queries /tmp/out.txt
diff /tmp/out.txt samples/tiny.expected   # must be empty
```

If `diff` prints anything, something is wrong with your environment — stop and ask.

Then check it against every committed instance's answer key:

```sh
make check-data
```

That runs the unmodified foundation over each `_dev` instance and compares it
with the shipped `.answers`. The keys were produced by a completely separate
implementation (`tools/ref/refsolve`), so agreement is a real cross-check
rather than a tautology.

## 2. File formats

0-based vertex indices throughout.

```
<graph_file>
  V E FLAGS
  u_1 v_1 w_1
  ...
  u_E v_E w_E
  [ if FLAGS bit 0 is set:
    x_0 y_0
    ...
    x_{V-1} y_{V-1} ]

<query_file>
  Q
  s_1 t_1
  ...
  s_Q t_Q

<output_file>
  d_1
  ...
  d_Q          (-1 if t_i is not reachable from s_i)
```

`FLAGS` is a bitmask:

| bit | value | meaning |
|---|---|---|
| 0 | 1 | the coordinate block is present |
| 1 | 2 | **the edge list is directed**: `u v w` is the arc `u -> v` only |

When bit 1 is clear the graph is undirected and each line contributes both
directions. A `FLAGS` of `0` or `1` therefore means what `HAS_COORDS` used to.

Coordinates are **integers**. Weights are positive and fit in `int32`;
**distances do not necessarily fit in 32 bits** — check each instance's
`.meta.json` for `resolve.fits_uint32` and `resolve.max_finite_distance`.

Each instance ships a `<name>.meta.json` recording V, E, flags, component
count, weight range, query mix, unreachable count and maximum distance. It is
there so you can reason about the data instead of guessing at it.

## 3. Fork the foundation into `solver.cpp`

```sh
cp dijkstra_foundation.cpp solver.cpp
make solver
```

Now improve `solver.cpp`. Your submission must:

- Keep the same command-line interface: `./solver <graph> <queries> <output>`.
- Read the same file formats as the foundation.
- Produce output identical to the foundation's on every instance
  (modulo trailing whitespace).
- Be a derivative of the foundation, not a rewrite. Submit a
  `diff_from_foundation.patch`.

## 4. Score yourself

```sh
python3 grade.py --solver ./solver --instances instances.txt --json result.json
```

Point `--instances` at a file with one `<category> <graph> <queries>` line per
instance; `instances.txt` ships wired to the dev tier, and you extend it with
the medium and large instances once you have downloaded them. Categories are
`GLOBAL` (long-range queries, throughput-bound) or `LOCAL` (short-range
queries, where per-query fixed costs dominate).

`note=WRONG` means correctness failed — fix that first; speed is worthless if
the answers are wrong.

### How the score is computed

For each instance, `speedup = T_base / T_solver`, then geometric means per
category, per track and overall. Four things about that are worth knowing:

**The score is not capped.** If your solver is 400x faster, you score 400.

**`T_base` is estimated, not measured in full.** The scored instances carry up
to 5,000,000 queries; running the unmodified foundation over all of them takes
hours. So `grade.py` times the foundation on two short prefixes of the query
file and fits `T(q) = a + b·q` — `a` is parsing, `b` is per-query cost.

Measured over 5 repetitions on the dev tier, the systematic error of a
2,000-query probe is +3.2% on `lattice3d_dev` and −0.1% on `local2d_dev`.
That part is a per-instance constant — the same queries for everyone — so it
scales every submission on that instance equally and cancels out of the
ranking. What does not cancel is machine noise, measured at 3.9% run-to-run on
a 75-second instance and 7.2% on a 5-second one. Use `--baseline-full` on a dev
instance to see the extrapolation checked against a real full run.

That last pair of numbers is also why the dev tier is not scored: a few-second
run cannot be timed tightly enough to rank anyone.

**Correctness is checked against the shipped `<name>.answers` key**, not
against your copy of the foundation.

**`grade.py` compiles the foundation itself**, from `dijkstra_foundation.cpp`
with pinned `-O2 -std=c++17`, so `T_base` does not depend on your build
settings. Your own `Makefile` governs only your solver.

### Limits are enforced, not just stated

`grade.py` applies them and scores the instance at the floor if you exceed one:

| Limit | How it is enforced |
|---|---|
| 4 GB memory | `RLIMIT_AS` on the child, plus peak RSS from `getrusage` |
| single-threaded | CPU time may not exceed 1.4x wall time |
| time limit | `--timeout`, default 3600 s per run |
| determinism | the three timing repeats must produce identical output |

## 5. Practice instances

`tools/gen/` is the real generator, and you have it. Instances are a pure
function of `(seed, salt, parameters)`, so you can produce as many fresh ones
as you like:

```sh
make -C tools/ref                                  # build the reference solver

python3 -m tools.gen --manifest tools/manifest/public.json \
                     --instance road2d_dev --seed 12345 --salt mine --out mydata
tools/ref/refsolve plan mydata/road2d_dev.graph mydata/road2d_dev.qplan \
                        mydata/road2d_dev.queries mydata/road2d_dev.answers
```

or in one step, with the same validation the released instances went through:

```sh
python3 tools/pipeline.py --manifest tools/manifest/public.json \
                          --instance road2d_dev --out mydata
```

**The graded run uses instances generated from the same code and the same
parameters with a seed you do not have.** Tuning to the released bytes will not
transfer; tuning to the *structure* will.

## Make targets

| Target | What it does |
|---|---|
| `make foundation` | build the baseline |
| `make solver` | build your `solver.cpp` |
| `make check-data` | verify every committed answer key against the foundation |
| `make tools` | build `tools/ref/refsolve` (needed to make your own instances) |
| `make test` | run the `grade.py` regression suite |

## Rules

C++17 only, standard library only, single-threaded final binary, 4 GB memory
cap, no precomputed answers in the binary, output must match the foundation
exactly, and the solver must be derived from `dijkstra_foundation.cpp` rather
than being a parallel rewrite.

`tools/` is instructor tooling and reference material. It is not part of your
submission and its code is not subject to these rules — in particular
`tools/ref/refsolve` is multi-threaded on purpose, because producing the answer
keys single-threaded would take days.

## What to submit

- `solver.cpp` (+ any headers)
- `Makefile`
- `result.json` from `grade.py --json`
- `diff_from_foundation.patch`
- `report.pdf` (one page)
- `machine.txt`
