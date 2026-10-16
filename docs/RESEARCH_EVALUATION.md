# ORCA — Research Evaluation

Version 1.0 · Phase 11 · 2026-09-03

> Every number in this document was computed from `benchmarks/results/phase11_search_comparison.csv`
> (972 rows) and `benchmarks/results/phase11_assignment_comparison.csv` (144 rows) — both real
> output of `orca_engine --bench`, generated on 2026-09-03 on the environment described in
> `FINAL_BENCHMARK_REPORT.md` §2. Analysis was done in Python (offline only, per the Phase 11
> brief's explicit "Python for analysis, never for computation" constraint) — every number
> below traces back to a real compiled-C++ run, never a Python-computed simulation result.
> This document answers a SUBSET of `BENCHMARK_PLAN.md` §1's seven questions (Q1, Q2, Q3, Q5,
> Q6, Q7) with the statistics `cpp/analytics/Statistics.hpp` actually computes (mean, median,
> min, max, population stdev) — not the fuller apparatus (Wilcoxon signed-rank tests,
> log-log regression with confidence intervals, hypervolume ratios) `BENCHMARK_PLAN.md` §7
> describes. Q4 (do Pareto-planned missions achieve better real-mission outcomes than greedy
> under constrained resources) is explicitly **not answered here** — see §7.

---

## 1. Research Questions and Hypotheses

**RQ1 (BENCHMARK_PLAN.md Q2) — Does A\* earn its place?** Does A\*'s admissible heuristic
reduce node expansions relative to Dijkstra in ORCA's mission graph, and does that reduction
translate into lower wall-clock time once heuristic-evaluation overhead is paid for?

> *Hypothesis*: A\* expands fewer nodes than Dijkstra at every tested size, and the relative
> reduction grows with graph size (more nodes for the heuristic to skip). Wall-clock savings
> should appear once the graph is large enough that heuristic-evaluation overhead is
> amortized — but may not appear, or may reverse, at very small sizes.

**RQ2 (BENCHMARK_PLAN.md Q3) — What does multi-objective planning cost?** How much slower is
Pareto (label-setting) search than single-objective search (Dijkstra/A\*), and how does that
gap grow with size?

> *Hypothesis*: Pareto search costs substantially more per run than single-objective search,
> and its cost grows super-linearly with size (label proliferation), so the *ratio* between
> Pareto and single-objective cost widens as scenarios scale up.

**RQ3 (BENCHMARK_PLAN.md Q5) — Does optimal assignment matter?** How large is
`regret = greedyCost − hungarianCost` in practice, and when is it non-zero?

> *Hypothesis*: Hungarian never does worse than greedy on a like-for-like comparison (it is
> the exact solver); greedy's real failure mode, if any, is more likely to be *serving fewer
> emergencies* than *paying more per emergency it does serve*.

**RQ4 (BENCHMARK_PLAN.md Q6/Q7) — How effective is dominance pruning, and where do the
mitigations (label cap, truncation) start to bind?**

> *Hypothesis*: dominance elimination does most of the real work of keeping a Pareto front
> small; the explicit safety-valve mitigations (pruning threshold, truncation) may not
> trigger at all at the object counts actually tested (25–100).

---

## 2. Experimental Setup

- **Generator**: `cpp/simulation/ScenarioGenerator.cpp`'s `generateScenario()`, deterministic
  given `(name, seed, config)` — the same generator `tests/simulation/scenario_generator_test.cpp`
  validates.
- **Search benchmark** (RQ1, RQ2, RQ4): `runSearchComparisonBenchmark()`
  (`cpp/analytics/BenchmarkRunner.cpp`). For each `(family, size, seed)`, builds the mission
  graph once, then runs Dijkstra, A\*, and Pareto search from the scenario's first vehicle to
  its first satellite, 1 warm-up + 5 measured repetitions each.
  - Families: `sparse`, `normal`, `dense`, `fuelConstrained`, `highDebris`, `tradeoffRich`
    (6 of the 8 defined families — `multiEmergency`/`stress` are excluded from the default
    `--bench` suite; the capability itself supports them).
  - Sizes: 25, 50, 100 objects.
  - Seeds: 1, 2, 3.
  - Total: 6 × 3 × 3 × 3 algorithms × 6 reps = 972 rows (810 after dropping the warm-up rep).
- **Assignment benchmark** (RQ3): `runAssignmentBenchmark()`
  (`cpp/analytics/AssignmentBenchmark.cpp`). For each `(matrixSize, seed)`, builds ONE
  candidate-cost matrix, then runs Greedy and Hungarian on the **identical matrix** (same
  `fronts`/`scored`/`matrix` objects passed to both solvers — verified by reading the
  benchmark's own source, not assumed), 1 warm-up + 5 measured repetitions each.
  - Matrix sizes (vehicles = emergencies, square): 2, 5, 10, 20.
  - Seeds: 1, 2, 3.
  - Total: 4 × 3 × 2 algorithms × 6 reps = 144 rows (120 after dropping warm-up).

## 3. Variables

| | Search benchmark | Assignment benchmark |
|---|---|---|
| Independent | `objectCount` (25/50/100), `algorithm` | `matrixSize` (2/5/10/20), `algorithm` |
| Dependent (RQ1/RQ2) | `nodesExpanded`, `heuristicEvaluations`, `executionTimeMs`, `labelsGenerated/Pruned/Dominated`, `truncated` | — |
| Dependent (RQ3) | — | `totalAssignmentCost`, `assignedPairs` |
| Controlled | seed (paired — every algorithm sees the identical graph/matrix per seed), warm-up excluded, same build (`-O2 -DNDEBUG`), same machine | same |

## 4. Baselines

Dijkstra is the baseline for A\* (RQ1); Dijkstra/A\* are jointly the baseline for Pareto
(RQ2); Greedy is the baseline for Hungarian (RQ3) — all pre-existing, already-tested
algorithms from Phases 4–6, not built for this benchmark.

## 5. Results

### 5.1 RQ1 — A\* vs. Dijkstra (nodes expanded, mean ± population stdev, n=90/cell)

| Size | Dijkstra nodesExpanded | A\* nodesExpanded | Expansion reduction | Dijkstra time (ms) | A\* time (ms) |
|---|---|---|---|---|---|
| 25 | 11.00 ± 6.54 | 9.00 ± 6.84 | 18.2% | 0.01330 | 0.01334 |
| 50 | 30.83 ± 15.35 | 19.67 ± 15.21 | 36.2% | 0.06482 | 0.05973 |
| 100 | 53.00 ± 31.33 | 29.89 ± 21.41 | **43.6%** | 0.19105 | 0.15336 |

A\*'s expansion advantage over Dijkstra **grows monotonically with size** (18.2% → 36.2% →
43.6%). Wall-clock time is essentially tied at size 25 (A\* is marginally *slower*, 0.01334
vs 0.01330ms — heuristic-evaluation overhead cancels the small node-count saving at this
scale) but A\* is faster at 50 and 100 objects (7.9% and 19.7% faster respectively). This
matches the hypothesis precisely, including the predicted "no win yet" case at the smallest
size — not hidden or averaged away.

### 5.2 RQ2 — Pareto vs. single-objective cost (mean execution time, ms)

| Size | Dijkstra | A\* | Pareto | Pareto ÷ Dijkstra |
|---|---|---|---|---|
| 25 | 0.01330 | 0.01334 | 0.12637 | 9.5× |
| 50 | 0.06482 | 0.05973 | 0.87109 | 13.4× |
| 100 | 0.19105 | 0.15336 | 5.80208 | **30.4×** |

Scaling ratio (time at 100 objects ÷ time at 25 objects, a 4× object-count increase):

| Algorithm | Scaling ratio |
|---|---|
| Dijkstra | 14.4× |
| A\* | 11.5× |
| Pareto | **45.9×** |

Pareto search is not just more expensive in absolute terms (9.5×–30.4× slower than Dijkstra
at the tested sizes) — its *scaling exponent* is worse: a 4× increase in object count costs
Dijkstra/A\* ~12-14× more time (consistent with graph size driving a super-linear but
sub-quadratic cost, plausible for `(V+E)log V`-family algorithms), but costs Pareto search
**46×** more time. This is the single clearest quantitative confirmation in this dataset of
`ALGORITHMS.md`'s stated expectation that label-setting is asymptotically the most expensive
algorithm in the codebase.

### 5.3 RQ3 — Assignment regret (Greedy vs. Hungarian)

**A naive comparison is misleading and was caught before being reported as a finding.** A
first pass compared `totalAssignmentCost` (sum over ASSIGNED pairs only) directly and found
greedy *appearing* cheaper in 75% of paired runs (mean regret = −0.54, i.e. negative). Cross-
checking `assignedPairs` showed why: in 45 of 60 paired runs, greedy and Hungarian assigned a
**different number of pairs** (e.g. greedy assigned 1, Hungarian assigned 2, at
`matrixSize=2`) — greedy was leaving emergencies unserved rather than finding a cheaper
overall assignment. Summing cost over a *smaller* served set is not a fair comparison.

**Corrected comparison, restricted to the 15/60 paired runs where both algorithms assigned
the identical number of pairs:**

| | n | mean regret (greedy − hungarian) | regret > 0 | regret = 0 | regret < 0 |
|---|---|---|---|---|---|
| Equal-assignedPairs subset | 15 | **+0.1388** | 5 (33%) | 10 (67%) | **0 (0%)** |

Hungarian is never worse than greedy when they serve the same number of emergencies (regret
is never negative) — confirming Hungarian's optimality, as expected of an exact solver. In
33% of these cases Hungarian found a strictly better assignment; in the rest, greedy happened
to find the same optimum (small matrices have few enough feasible pairs that greedy's
first-fit strategy sometimes lands on the true optimum by construction).

**The more operationally significant finding**: greedy's `assignedPairs` histogram skews
consistently lower than Hungarian's at every size (e.g. at the largest tested matrix,
greedy assigns 18–19 pairs across seeds while Hungarian assigns 19–20) — greedy more often
leaves an emergency completely unserved. Mean cost **per assigned pair** is actually *lower*
for greedy (0.386) than Hungarian (0.455, n=60 each) — consistent with a classic greedy
failure mode: it grabs the cheapest, easiest pairs first and leaves the harder, more
expensive ones unassigned, while Hungarian (forced by the padded square-matrix formulation
to complete a full assignment) takes on some genuinely expensive pairs that greedy simply
skips. For a rescue-dispatch system, "leaves an emergency unserved" is a materially worse
failure mode than "serves it at a somewhat higher cost" — this dataset's real evidence
favors Hungarian for a reason the raw cost sum alone would have hidden.

### 5.4 RQ4 — Pruning effectiveness and mitigation binding point

| Size | labelsGenerated (sum) | labelsDominated | Dominance rate | labelsPruned | truncated=true |
|---|---|---|---|---|---|
| 25 | 87,865 | 82,100 | 93.4% | 0 | 0/90 |
| 50 | 612,895 | 590,720 | 96.4% | 0 | 0/90 |
| 100 | 4,279,230 | 4,192,030 | 98.0% | 0 | 0/90 |

Ordinary Pareto dominance does essentially all the work of keeping the front small, and its
effectiveness *increases* with size (93.4% → 98.0% of generated labels are eliminated by
dominance). The separate safety-valve mitigations (an explicit pruning threshold beyond
dominance, and truncation once a label cap is hit) **never triggered once** across all 270
Pareto runs in this dataset (`labelsPruned` is always exactly 0; `truncated` is always
`false`). At the object counts this benchmark actually tests (25–100), dominance alone is
sufficient — the mitigations exist for a scale this dataset does not reach. `BENCHMARK_PLAN.md`
Q7's question ("where do the mitigations start to bind?") is answered honestly as: **not yet
observed** — the binding point is somewhere above 100 objects and remains untested.

## 6. Observations

- The clearest, cleanest result in this dataset is RQ2: Pareto search's scaling exponent is
  visibly and substantially worse than single-objective search's, which is exactly the
  quantitative form of the "candidate generation dominates" finding `docs/FINAL_BENCHMARK_REPORT.md`
  reports from a different angle (candidate generation runs one Pareto search per
  vehicle-emergency pair, so a per-search cost that itself scales at 46× compounds badly
  across an O(vehicles×emergencies) generation loop).
- RQ1 shows heuristic search earning its keep, but only once the graph is large enough —
  worth stating precisely rather than as a blanket "A\* is faster" claim.
- RQ3's naive-vs-corrected analysis is itself a finding worth keeping in this document: it is
  a concrete, real example of exactly the failure mode `BENCHMARK_PLAN.md` §7.4 was written
  to guard against ("the evidence is not adjusted to the claim") — catching it changed the
  conclusion from "greedy is cheaper" to "greedy under-serves emergencies," a materially
  different and more actionable finding.

## 7. What This Evaluation Does NOT Answer (honestly deferred)

- **Q4, the question `BENCHMARK_PLAN.md` calls "the one that matters academically"**: do
  Pareto-planned missions achieve measurably better real-mission outcomes (success rate,
  fuel, risk) than greedy/single-objective planning under constrained resources? Answering
  this needs full `SimulationEngine` runs comparing strategies end-to-end across many seeded
  scenarios, not isolated search/assignment benchmarks — out of scope for the time available
  this phase. Listed explicitly in the Phase 11 final report's FUTURE RESEARCH triage.
- No statistical hypothesis testing (Wilcoxon signed-rank, confidence intervals) was
  performed — `cpp/analytics/Statistics.hpp` computes mean/median/min/max/stdev only, and
  this document reports exactly that, not more.
- No curve-fitting (`t = a·N^b`) was performed for RQ1/RQ2's scaling claims; the scaling
  ratios reported (§5.1, §5.2) are simple ratios between the smallest and largest tested
  sizes, not a fitted exponent with a confidence interval.

## 8. Threats to Validity

- **Simplified orbital/risk model**: every number here reflects ORCA's own two-body,
  circular-orbit, heuristic-risk model (`SIMULATION_MODEL.md` §8's own documented
  simplifications) — not real orbital mechanics. Graph structure, and therefore search cost,
  would differ under a higher-fidelity model.
- **Synthetic scenarios, not real orbital data**: `ScenarioGenerator`'s object placement is
  randomized within configured bands, not drawn from any real satellite catalogue.
- **Small size range (25–100 objects)**: RQ4's "mitigations never bind" finding is scoped
  explicitly to this range; extrapolating it to 500+ objects is not supported by this data
  (`docs/FINAL_BENCHMARK_REPORT.md`'s separate 500-object timing probes did not re-check
  `labelsPruned`/`truncated` at that scale).
- **Single machine, single build**: all numbers are from one Apple M4 machine, one `-O2`
  build (`FINAL_BENCHMARK_REPORT.md` §2) — absolute timings are not portable across hardware;
  the *relative* comparisons (ratios, percentages) are more likely to generalize than the
  absolute millisecond figures.
- **Small paired-sample count for RQ3**: only 15 of 60 assignment runs had equal
  `assignedPairs`, so the "regret is never negative" claim rests on n=15, not n=60 — stated
  explicitly in §5.3 rather than implied to be a larger sample.
- **Assignment benchmark's synthetic emergencies**: `buildSyntheticEmergencies()`
  (`AssignmentBenchmark.cpp`) creates one `PropulsionFailure`/`High`-severity emergency per
  generated satellite — a fixed, uniform emergency profile, not the mixed severities/kinds a
  real dispatch run would see.

## 9. Future Work

- Answer Q4 properly: run full `SimulationEngine` sessions comparing Pareto-driven dispatch
  against greedy/weighted-single-objective dispatch on the same seeded scenarios, measuring
  success rate / fuel / risk outcomes (the seam already exists — `EventDispatcher.cpp`'s
  `runGlobalReplanAndDispatchMissions()` already supports swapping the solver).
  `RESEARCH_EXTENSION.md` §7 (Monte Carlo outcome analysis) is the natural next step.
- Extend RQ1/RQ2's size range past 100 to find where `labelsPruned`/`truncated` actually
  start to bind (RQ4's open question).
  `docs/FINAL_BENCHMARK_REPORT.md` §4's own O(N²) candidate-generation finding is the reason
  this was not simply run further within this phase — assignment benchmarking beyond
  `matrixSize=20` already costs minutes per point.
- Add the statistical rigor `BENCHMARK_PLAN.md` §7 specifies (Wilcoxon signed-rank tests,
  confidence intervals, log-log regression) once a larger sample size justifies it.
