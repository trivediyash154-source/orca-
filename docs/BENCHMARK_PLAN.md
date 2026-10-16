# ORCA — Benchmark Plan

Version 1.2 · Phase 0, addenda Phase 4-5 · 2026-09-03

> **No results appear in this document.** It defines a measurement protocol. Every number here
> is a *parameter of the experiment*, never an outcome. Results land in `benchmarks/` when the
> engine exists and the suite has actually run.

> **Phase 11 as-built correction**: the engine now exists and the suite has run — real
> results are in `benchmarks/results/*.csv`, analyzed in `docs/RESEARCH_EVALUATION.md` and
> `docs/FINAL_BENCHMARK_REPORT.md`. What was actually built is narrower than this plan: no
> JSON metadata export (§6.2), no memory profiling (§3.4), no Wilcoxon/CI statistical testing
> or log-log regression (§7), and Q4 (`§1`, "the research question that matters
> academically") was not answered — full-mission outcome comparison across strategies was
> judged out of scope for this phase's time budget. `cpp/analytics/Statistics.hpp` computes
> mean/median/min/max/stdev only. Q1, Q2, Q3, Q5, Q6, Q7 (§1) all have real, measured answers
> in `RESEARCH_EVALUATION.md`. This plan's remaining scope is listed there under "Future Work."

---

## 1. Questions the Benchmarks Must Answer

Benchmarks exist to answer stated questions, not to generate charts.

**Q1 — Scaling.** How do graph construction, Dijkstra, A*, label-setting, and assignment scale
with object count?

**Q2 — Does A\* earn its place?** How many fewer nodes does it expand than Dijkstra, and does
that translate into lower wall time once heuristic evaluation is paid for?

**Q3 — What does multi-objective planning cost?** How much slower is label-setting than
single-objective search, and how does that gap grow with size?

**Q4 — What does it buy?** Do Pareto-planned missions achieve better outcomes — success rate,
fuel, risk — than greedy and single-objective strategies under constrained resources?
*This is the research question in `PRD.md` §15.*

**Q5 — Does optimal assignment matter?** How large is `regret = greedy − optimal` in practice,
and when is it non-zero?

**Q6 — How effective is dominance pruning?** What fraction of generated labels is pruned, and
how does the front size grow?

**Q7 — Where do the mitigations start to bind?** At what size does ε-dominance or the label cap
begin to truncate?

Q4 is the one that matters academically. Q1–Q3 and Q6–Q7 are algorithmic characterisation;
Q5 is a subsidiary question about the assignment layer.

---

## 2. Protocol

### 2.1 Sizes

```
25 · 50 · 100 · 250 · 500 · 1000        (total tracked objects)
```

Composition scales proportionally: 80% satellites, 8% rescue vehicles, 12% debris. At
`N = 1000`: 800 satellites, 80 vehicles, 120 debris.

Roughly geometric spacing (×2, ×2, ×2.5, ×2, ×2) so log-log plots reveal the exponent
directly. Six points is enough to fit a scaling curve and see curvature.

### 2.2 Seeds

```
30 seeds per configuration, fixed and committed:
  928371, 104729, 224737, 350377, 479909, 611953, 746773, 882377, 1020379, 1159523,
  1300537, 1442968, 1587791, 1734479, 1882277, 2032079, 2183507, 2336497, 2490529, 2646011,
  2802181, 2960287, 3119459, 3280343, 3442369, 3605537, 3769669, 3934819, 4100977, 4268047
```

30 gives usable confidence intervals without dominating runtime. The list is committed so
anyone can reproduce the exact experiment (`configs/benchmark-seeds.json`).

### 2.3 Repetitions and warm-up

| Setting | Value | Why |
|---|---|---|
| Warm-up iterations | 3, **discarded** | Cache/branch-predictor warm-up; first-run allocation |
| Measured repetitions | 10 per seed per config | Damps OS scheduling noise |
| Reported statistic | **median** | Robust to outliers from scheduling |
| Also reported | mean, stddev, p95, min, max | Distribution shape matters |
| Timer | `std::chrono::steady_clock` | Monotonic; unaffected by clock adjustment |

**The median is the headline statistic, not the mean.** A single scheduling hiccup can shift a
mean by 30% and tells you nothing about the algorithm.

### 2.4 Environment control

Recorded in every export, because timings are meaningless without them:

```json
{
  "cpu": "Apple M-series arm64", "cores": 0, "ramGb": 0,
  "os": "Darwin 24.6.0", "compiler": "Apple clang 17.0.0",
  "buildType": "Release", "flags": "-O2 -DNDEBUG",
  "engineVersion": "0.0.0", "gitCommit": "unknown",
  "timestampUtc": "not-yet-run"
}
```

Zeros and `not-yet-run` are placeholders — **deliberately not filled with plausible-looking
values**, so an un-run benchmark can never be mistaken for a completed one.

Requirements: `Release` build (`-O2 -DNDEBUG`), no debugger, no profiler, machine otherwise
idle, power connected, thermal throttling checked and noted.

---

## 3. Measured Metrics

### 3.1 Timing (ms)

`graphBuildMs` · `dijkstraMs` · `astarMs` · `paretoSearchMs` · `dominanceFilterMs` ·
`greedyAssignMs` · `hungarianAssignMs` · `replanMs` · `totalPlanningMs` · `snapshotMs`

### 3.2 Algorithmic counters

These are more valuable than timings: they are **hardware-independent** and reproducible
exactly.

`nodesExpanded` · `edgesRelaxed` · `pqPushes` · `pqPops` ·
`labelsGenerated` · `labelsPruned` · `labelsRetained` · `frontSize` ·
`verticesBuilt` · `edgesBuilt` · `edgesRejectedFuel` · `edgesRejectedDeadline` ·
`edgesRejectedRisk` · `truncated`

A reviewer who cannot reproduce our wall times **can** reproduce our node counts exactly. That
is the stronger evidence, and the report leads with it.

### 3.3 Outcome metrics — the Q4 evidence

`totalEmergencies` · `successfulRescues` · `failedRescues` · `expiredUnreached` ·
`successRate` · `avgRescueTimeSec` · `avgFuelFraction` · `avgRisk` ·
`totalDeltaVSpent` · `replanCount` · `fleetUtilisation` · `assignmentRegret`

### 3.4 Memory

`peakResidentMb` · `graphMemoryBytes` · `labelPoolPeakBytes`

---

## 4. Experiment Matrix

```
Scenario × Strategy × Size × Seed
```

### 4.1 Scenarios

| Scenario | Purpose |
|---|---|
| `uniform-leo` | Baseline: single band, spread inclinations |
| `multi-band` | LEO+MEO+GEO — exercises large altitude differences |
| `high-inclination-spread` | Wide inclination range — where plane-change cost dominates (`SIMULATION_MODEL.md` §4.3) |
| `debris-dense` | Heavy debris — exercises risk and the spatial index |
| `resource-constrained` | Few vehicles, low Δv budgets — **where multi-objective planning should matter most** |
| `cascading-failures` | Multiple near-simultaneous emergencies — exercises assignment |
| `dynamic-environment` | Frequent debris alerts — exercises replanning |

`resource-constrained` is the scenario Q4 hinges on. Under abundant resources, every strategy
succeeds and the comparison is uninformative; scarcity is what separates them.

### 4.2 Strategies

| Strategy | Description |
|---|---|
| `greedy-fastest` | Nearest feasible vehicle, minimise time |
| `greedy-fuel` | Minimise fuel only |
| `greedy-safety` | Minimise risk only |
| `weighted-single` | Scalarise **first**, then Dijkstra — *the key comparator* |
| `dijkstra-time` | Single-objective shortest time |
| `astar-time` | Same with heuristic (algorithmic comparison) |
| `orca-pareto` | Full front + profile selection |

**`weighted-single` is the honest comparator for Q4.** It uses the same weights as
`orca-pareto`'s profile but scalarises before searching. Any difference isolates the value of
computing the front *first* — which is precisely `OPTIMIZATION_MODEL.md` §1.1's claim, tested
rather than asserted.

### 4.3 Volume

```
7 scenarios × 7 strategies × 6 sizes × 30 seeds × 10 reps = 88 200 runs
```

Too many for routine use. Three tiers:

| Tier | Configuration | Runs | Use |
|---|---|---|---|
| **Smoke** | 1 scenario × 3 strategies × 3 sizes × 3 seeds × 3 reps | 243 | Every CI run |
| **Standard** | 3 scenarios × 5 strategies × 6 sizes × 10 seeds × 5 reps | 4 500 | Weekly / pre-report |
| **Full** | everything | 88 200 | Final report only |

Runtime for Full is unknown until Standard has been measured. **No estimate is given here**,
because guessing it would be inventing a number.

---

## 5. Running

```bash
orca_engine bench --tier smoke --out benchmarks/
orca_engine bench --tier standard --out benchmarks/ --seeds configs/benchmark-seeds.json
orca_engine bench --scenario resource-constrained \
                  --strategies orca-pareto,weighted-single,greedy-fastest \
                  --sizes 25,50,100,250,500,1000 --seeds 30 --reps 10 \
                  --out benchmarks/q4/
```

Headless. No Node, no browser (`ARCHITECTURE.md` P3). The benchmark suite is the strongest
demonstration that the engine stands alone.

---

## 6. Output Format

### 6.1 CSV — one row per run

```csv
runId,scenario,strategy,size,seed,rep,graphBuildMs,dijkstraMs,astarMs,paretoSearchMs,
totalPlanningMs,nodesExpanded,edgesRelaxed,pqPushes,pqPops,labelsGenerated,labelsPruned,
labelsRetained,frontSize,verticesBuilt,edgesBuilt,edgesRejectedFuel,edgesRejectedDeadline,
truncated,totalEmergencies,successfulRescues,failedRescues,successRate,avgRescueTimeSec,
avgFuelFraction,avgRisk,replanCount,assignmentRegret,peakResidentMb
```

Long format, one row per run — no pre-aggregation. Aggregation is a presentation choice and
must remain reversible; a reviewer should be able to recompute every summary statistic from
the raw file.

### 6.2 JSON — run metadata + aggregates

```json
{
  "meta": { "engineVersion": "...", "gitCommit": "...", "environment": { }, "protocol": { } },
  "runs": [ ],
  "aggregates": [
    { "scenario": "...", "strategy": "...", "size": 0,
      "n": 0, "median": { }, "mean": { }, "stddev": { }, "p95": { },
      "ci95": { "lower": 0, "upper": 0 } }
  ]
}
```

Empty structures are the schema, not results.

---

## 7. Analysis

### 7.1 Scaling (Q1)

Log-log plot of time vs `N`; fit `t = a·N^b`; report `b` with its confidence interval.
Expectations to be **tested, not assumed**:

| Component | Predicted | Verified by |
|---|---|---|
| Graph build | `b ≈ 2` (all-pairs candidates) | fitted exponent |
| Dijkstra | `b ≈ 1`–`1.2` (sparse, `(V+E)log V`) | fitted exponent |
| A* | same order, lower constant | fitted exponent + constant |
| Label-setting | `b > 2`, possibly super-polynomial | fitted exponent, curvature |

If a fitted exponent disagrees with `ALGORITHMS.md`, **the documentation is wrong or the
implementation is wrong**, and the discrepancy is reported either way. A benchmark that only
ever confirms expectations is not measuring anything.

### 7.2 A* effectiveness (Q2)

Two separate ratios, reported separately because they can disagree:

```
expansionRatio = nodesExpanded_astar / nodesExpanded_dijkstra
wallTimeRatio  = astarMs / dijkstraMs
```

`expansionRatio < 1` with `wallTimeRatio ≥ 1` is a real and interesting outcome — it means the
heuristic is informative but too expensive to evaluate at this scale. That would be reported
plainly, not hidden by quoting only the favourable ratio.

### 7.3 Pruning effectiveness (Q6)

```
pruneRate  = labelsPruned / labelsGenerated
frontRatio = frontSize / labelsGenerated
```
Plotted against `N` and against objective count `k` (a small `k = 2, 3, 4` sweep), since
dominance weakens as `k` grows (`ALGORITHMS.md` §5.8).

### 7.4 Outcome quality (Q4) — the research result

For each (scenario, size), compare strategies on `successRate`, `avgFuelFraction`, `avgRisk`.

- Paired by seed — every strategy sees the **identical** scenario, which is exactly what the
  determinism guarantee buys us. Pairing removes scenario variance from the comparison.
- Wilcoxon signed-rank test (paired, non-parametric — no normality assumption).
- Report effect size, not just p-values.
- Report 95% CIs on all differences.

**Pre-registered honesty condition:** if `orca-pareto` does **not** outperform
`weighted-single`, that is the finding and it gets reported. A plausible outcome is that they
tie on outcome quality while Pareto costs more compute — in which case the defensible claim
narrows to *"the front provides operator insight and post-hoc preference flexibility at a
measured compute cost"*, which is still true and still worth having. The claim is adjusted to
the evidence; the evidence is not adjusted to the claim.

### 7.5 Assignment regret (Q5)

Distribution of `regret` across seeds; fraction of runs with `regret > 0`; correlation with
contention (emergencies per available vehicle). If regret is near-zero everywhere, the honest
conclusion is that greedy suffices at ORCA's scale — and the Hungarian implementation remains
justified as the *verification* that establishes it.

---

## 8. Reproducibility

Every export carries: engine version, git commit, full environment block, complete protocol
(sizes, seeds, reps, warm-up), scenario file hashes, and config file hashes.

```bash
git checkout <commit>
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/orca_engine bench --tier standard --seeds configs/benchmark-seeds.json --out repro/
diff <(cut -d, -f1-24 repro/results.csv) <(cut -d, -f1-24 benchmarks/results.csv)
```

**Algorithmic counters must match exactly** — they are deterministic. Timings will differ by
hardware; that is expected and is why counters are the primary evidence.

---

## 9. Threats to Validity

Stated up front, because a benchmark section without one is not credible.

| Threat | Mitigation |
|---|---|
| Synthetic scenarios may not represent real orbital traffic | Claims are scoped to *the algorithms under this model*, never to real operations. `SIMULATION_MODEL.md` §8 |
| Scenario generation could favour ORCA | Generation is strategy-agnostic and seeded before any strategy runs; identical scenarios given to every strategy |
| Wall time is hardware-dependent | Counters are primary; timings secondary; environment recorded |
| Thermal throttling on laptops | Runs interleaved by strategy, not blocked; throttling checked and noted |
| Cherry-picked seeds | Seed list committed **before** running |
| Selective reporting | All 7 strategies reported for every scenario, including unfavourable ones |
| `p`-hacking via many comparisons | Q1–Q7 pre-registered here; multiple-comparison correction applied where applicable |
| Small `n` | 30 seeds with reported CIs; no claims from single runs |
| ε-dominance makes "exact" fronts approximate | `ε = 0` for all correctness comparisons; `truncated` reported per run |

### 9.1 The strongest threat

**We designed the model, the scenarios, and the algorithm.** A model built by people who
expect multi-objective planning to win can produce a model where it wins.

Partial mitigations: the trade-off comes from an *independently validated* physical quantity
(the 25.4-hour synodic period, `REPOSITORY_AUDIT.md` §4.2) rather than from a tuned parameter;
`weighted-single` uses identical weights, identical graph and identical cost model, so the
comparison isolates exactly one variable; and §7.4's honesty condition is stated in advance.

It is a partial mitigation, not a solution. The limitation is real and is repeated in the
final report.

---

## 10. Deliverables

```
benchmarks/
├── results.csv                 raw, one row per run
├── results.json                metadata + aggregates
├── environment.json            machine and build details
├── protocol.json               exact parameters used
└── figures/
    ├── scaling-loglog.svg
    ├── astar-vs-dijkstra.svg
    ├── pruning-effectiveness.svg
    ├── strategy-outcomes.svg
    └── assignment-regret.svg
```

Figures are generated **from `results.csv`** by a script in `scripts/`, so every figure is
traceable to raw data and regenerable. No figure is hand-made.

**Note on tooling.** Plotting is the one place where `ARCHITECTURE.md` §3.1's "no Python" rule
is worth re-examining — matplotlib is genuinely good at this. The current decision is to
generate figures from the frontend's existing charting stack, keeping the toolchain to one
language. If that proves clumsy in practice, an *offline* plotting script is an acceptable
exception, because it consumes exported CSV and is not part of the product. That judgement is
deferred until there is real data to plot.

---

## 11. Phase 4 Addendum: Dijkstra vs A* Micro-Benchmark

**This is a separate, narrower tool from everything above, and it has already run.**
Everything in §1–§10 is the protocol for the *full* system — Pareto search, Hungarian
assignment, outcome metrics — none of which exist yet. Building any part of the `orca_engine
bench` CLI those sections describe ahead of the subsystems its own options name would be
exactly the kind of premature interface this project's phases avoid elsewhere (`cpp/app/
main.cpp` documents that verb as arriving in Phase 13). Phase 4 needed a much smaller
question answered now: does A* earn its evaluation cost against Dijkstra, on the two
algorithms that actually exist.

### 11.1 Tool

`benchmarks/dijkstra_vs_astar_bench.cpp`, built and run via
`scripts/run_astar_benchmark.sh` (`-O2 -DNDEBUG`, per §2.4's requirement, unchanged here).
Four deterministic scenarios (10/25/50/150 nodes — named tiny/small/medium/larger),
generated from a seeded `RandomGenerator` (portable, not `std::uniform_real_distribution`
— `RandomGenerator.hpp`), each with its own fixed, documented seed distinct from
`configs/benchmark-seeds.json` (that list is committed for the full protocol above; this
tool is Phase 4's own, reproducible on its own terms). Each scenario is an altitude-sorted
backbone (guarantees connectivity end to end) plus a handful of skip edges per node
(guarantees genuine route choices exist, not a single forced chain). Source is the
lowest-altitude node, target the highest.

Protocol, scaled down from §2.3 but not loosened: 3 warm-up runs (discarded) + 10 measured
runs per (scenario, algorithm) pair; median is the headline timing statistic; the
algorithmic counters (§3.2) are verified internally to be byte-identical across all 10
repetitions before being reported once, and separately re-confirmed by running the whole
binary twice end-to-end and diffing (only the timing columns differed, as expected system
noise — `benchmarks/phase4_dijkstra_vs_astar_results.txt` records this check).

### 11.2 Results

Full table, protocol notes, and the reading below: `benchmarks/
phase4_dijkstra_vs_astar_results.txt` — this document states the findings, not the raw
numbers, matching this file's own opening rule.

- **A* expanded the same number or fewer nodes than Dijkstra at every size tested**
  (strictly fewer at two of the four sizes) — real, if modest, evidence the heuristic
  guides the search, and enough on its own to satisfy `PRD.md`'s FR-A2 acceptance bar
  ("A* expands strictly fewer nodes... on a documented case").
- **A* was slower in median wall-clock time than Dijkstra at every size tested.** Each
  heuristic evaluation costs two closed-form Hohmann computations, and on these particular
  graphs that fixed cost was not repaid by the reduction in nodes and edges touched.
- **The heuristic-quality ratio `h(source,target)/trueCost` fell from 0.167 to 0.003** as
  scenario size grew from 10 to 150 nodes — explained by, not merely correlated with, the
  heuristic's own design (`HEURISTIC_DESIGN.md` §4, Step 1: it bounds coast time only,
  never wait, and this benchmark's larger graphs accumulate more phasing wait along the
  optimal route). This is the same underlying reason A* did not earn back its overhead:
  a heuristic that is a loose bound on this benchmark's true cost cannot guide the search
  aggressively.

### 11.3 What this does and does not establish

**Does:** A* is correct here (Dijkstra and A* agreed on found status, arrival time and
every total objective on every scenario — the benchmark's own internal check would have
printed a warning and exited non-zero otherwise, and did not). A* is not a universal
improvement over Dijkstra for ORCA's graphs, at least not on the class of topology tested.

**Does not:** rule out A* winning decisively on a different topology. The chosen shape (a
backbone climb from lowest to highest altitude) was picked for guaranteed connectivity and
reproducibility, not because it is where a heuristic's pruning would show up most — a graph
with substantial dead-end branching the heuristic can see is hopeless (dense fan-out around
the target, for instance) would plausibly favour A* far more clearly. That scenario was not
built in Phase 4. Building and measuring it, rather than assuming the answer either way, is
future work (`HEURISTIC_DESIGN.md` §10), not a claim made here.

---

## 12. Phase 5 Addendum: Pareto Search Label Growth and ε-Dominance

Same scope discipline as §11: a separate, narrower tool
(`benchmarks/pareto_search_bench.cpp`, run via `scripts/run_pareto_benchmark.sh`), not any
part of the unbuilt full protocol in §1–§10.

### 12.1 Tool

Four deterministic "fan" scenarios (10–32 nodes: several structurally distinct candidate
orbits per layer, fully connected between consecutive layers), 3 warm-up + 10 measured runs
each, `-O2 -DNDEBUG`. The layered-fan topology is deliberately different from Phase 4's
altitude backbone — label growth is a *multi-objective* phenomenon, and a single backbone
rarely retains more than one or two labels per node (`PARETO_SEARCH.md` §17's finding).
Counters verified byte-identical across all 10 repetitions per scenario, and re-confirmed
across two independent full runs of the binary.

### 12.2 Results

Full table and reading: `benchmarks/phase5_pareto_search_results.txt`.

- Label growth across the three reachable scenarios (tiny/small/medium) was real but not
  explosive at these sizes (19 → 35 → 154 labels inserted), with dominance comparisons
  growing faster than labels (72 → 112 → 2401) — consistent with each insertion being
  checked against every existing label at that node (`O(n)` per insertion,
  `PARETO_SEARCH.md` §8.1).
- The fourth ('larger', 5 layers) scenario was **fully unreachable** under a realistic 2.5
  km/s vehicle budget — every route's cumulative delta-v exceeded it. This is the
  cumulative delta-v hard constraint (`PARETO_SEARCH.md` §9) correctly firing on a scenario
  deep enough for accumulated cost to matter, not a benchmark defect, and it is reported
  rather than swapped out for a scenario that produces a nicer-looking number.
- **ε-dominance collapses the frontier sharply and monotonically**: on the 'medium'
  scenario's 19-route exact frontier, `epsilon=0.02` cut it to 3 routes and dominance
  comparisons by 95% (2401 → 117); `epsilon=0.20` collapsed it to 1 route. Wall-clock time
  tracked the same trend (roughly a 5–6× speedup at `epsilon=0.20`).

### 12.3 What this does and does not establish

**Does:** demonstrate, with real measurements, that (a) dominance-comparison cost is the
dominant term as label counts grow, not raw label generation, and (b) ε-dominance is a
substantial, usable lever when exactness is not required — both expected from the
algorithm's design (`ALGORITHMS.md` §5.7) and now backed by numbers rather than assumed.

**Does not:** measure whether ε-dominance changes the *recommended* plan under a specific
`OptimizationProfile` — this benchmark exercises `algorithms::findParetoRoutes()` only, not
the `optimization/` scoring layer on top of it. Scoring the exact front and each ε-front
under a fixed profile and comparing the winning plan is a reasonable next measurement, not
one taken here.

---

## 13. Phase 6 Addendum: Assignment (Hungarian) Scaling and Integrated Pipeline Breakdown

Same scope discipline as §11–12: a separate, narrower tool
(`benchmarks/assignment_bench.cpp`, run via `scripts/run_assignment_benchmark.sh`).

### 13.1 Tool

Two parts, both `-O2 -DNDEBUG`, 2 warm-up + 7 measured runs per point (fewer measured runs
than §11–12's 10 since the largest raw case, `n=100`, is already `O(n^3)` and the pipeline
part times four sub-stages per run):

1. **Raw `solveSquareAssignment()` scaling** on random `n x n` cost matrices (deterministic
   seeded), `n in {2, 5, 10, 20, 50, 100}` — isolated from all domain code, matching
   `ASSIGNMENT_MODEL.md` §15's complexity claim against measurement.
2. **Integrated pipeline, timed stage by stage**: candidate generation (the Pareto search
   stage — the only one calling `findParetoRoutes()`), scoring, cost-matrix construction,
   and the Hungarian solve, for synthetically generated but real-orbital-mechanics scenarios
   from `2x2` through `10x10` vehicles x emergencies (independently randomised orbits per
   entity, fully cross-connected, deterministic seed per size).

### 13.2 Results

Full table: `benchmarks/phase6_assignment_results.txt`.

- Raw Hungarian scaling matched the `O(n^3)` operation-count claim directly:
  `operationCount` grew from 4 (`n=2`) to 50506 (`n=100`), and wall-clock grew from
  ~0.001ms to ~0.15–0.37ms across runs — comfortably sub-millisecond at every size Phase 6
  brief §15 asked for, and still sub-millisecond well past ORCA's realistic scale
  (`ALGORITHMS.md` §8.4's `n <= ~20` estimate).
- **Candidate generation dominates the integrated pipeline at every scenario size
  measured, by a wide and growing margin** — at `10x10`, candidate generation took
  ~0.4–0.7ms against Hungarian's own solve at ~0.01–0.02ms, a 30–40x gap; at `2x2` the gap
  was already ~5–8x. This is the honest answer to Phase 6 brief §25's question of whether
  Hungarian is the bottleneck: **it is not, anywhere in the range measured.** The dominant
  cost is running one full Pareto search per `(vehicle, emergency)` pair
  (`O(V*E)` independent searches, each itself `O(graph size)` per `PARETO_SEARCH.md` §14) —
  an expected result given §12's own finding that Pareto search's dominance-comparison cost
  already dominates at modest label counts, now compounded by needing one search per pair
  rather than one search total.
- Scoring and cost-matrix construction were consistently the two cheapest stages at every
  size, each roughly an order of magnitude below candidate generation — expected, since
  neither calls `findParetoRoutes()` and both operate on already-computed fronts
  (`ASSIGNMENT_MODEL.md` §6).

### 13.3 What this does and does not establish

**Does:** confirm Hungarian's own complexity claim against real measurement, and answer
honestly (not by assumption) that candidate generation, not assignment solving, is where a
future performance-optimisation pass should look first if `V*E` grows large.

**Does not:** measure candidate generation's cost as `V` and `E` grow independently beyond
`10x10`, or with graphs denser than the fully-cross-connected synthetic scenarios used here
— a real scenario's graph structure (`GraphBuilder.hpp`'s Group 1–3 feasibility pruning) may
prune many pairs before search even runs, which this benchmark's fully-connected generation
does not model.
