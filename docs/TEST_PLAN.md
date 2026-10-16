# ORCA — Test Plan

Version 1.1 · updated after Phase 3 · 2026-09-02

---

## 1. Strategy

ORCA's tests answer one question: **is a result correct, or merely plausible?** A
multi-objective planner that returns a wrong front returns something that *looks* entirely
reasonable. Nothing crashes, nothing looks odd, and the answer is wrong. That failure mode
drives the whole approach:

1. **Verify against independently-known answers**, not against the implementation's own output.
2. **Property-test the invariants** that correctness arguments depend on.
3. **Cross-validate algorithms** against each other (A* vs Dijkstra, Hungarian vs brute force,
   incremental patch vs full rebuild).
4. **Assert determinism** as a first-class requirement.
5. **Test the layering**, so the architecture cannot silently rot.

### 1.1 Toolchain

| Layer | Tool | Notes |
|---|---|---|
| C++ | GoogleTest via CMake `FetchContent`, pinned release tag | No system install (audit §3.2) |
| TypeScript | Vitest | Ships with Vite |
| Contract | Vitest over recorded NDJSON fixtures | Both sides validated |
| E2E | Playwright | Tier 3; only after the UI stabilises |

### 1.2 Pyramid

```
        ╱ E2E ╲              few — expensive, brittle
      ╱ contract ╲           moderate — the cross-language seam
    ╱ integration  ╲         moderate — full scenario runs
  ╱   unit + property ╲      many — fast, precise, the foundation
```

---

## 2. Coverage Targets

| Area | Target | Rationale |
|---|---|---|
| `cpp/algorithms/` | **≥ 95% line, 100% branch on dominance** | Correctness core |
| `cpp/optimization/` | ≥ 90% | Front construction and selection |
| `cpp/graph/` | ≥ 90% | |
| `cpp/simulation/` | ≥ 85% | Some paths need long runs |
| `cpp/planning/` | ≥ 85% | |
| `cpp/io/` | ≥ 80% | Built in Phase 9: `JsonValue`/`JsonParser`/`CommandProtocol`/`ViewProjection` — 43 tests in `tests/io/` |
| `frontend/` | ≥ 60% | Presentational; E2E covers the rest |

Coverage is a floor, not a goal. A 100%-covered dominance predicate with no property tests
would still be inadequate.

---

## 3. Unit Tests — Models

Verified against **closed-form values computed independently**, using the figures validated in
`REPOSITORY_AUDIT.md` §4.

### 3.1 Orbital propagation — `tests/simulation/propagator_test.cpp`

| Test | Assertion |
|---|---|
| `PeriodAt500km` | `T(6878.137) ≈ 5677.5 s (94.62 min)`, tol 0.01% |
| `PeriodAt800km` | `T(7178.137) ≈ 6052.2 s (100.87 min)`, tol 0.01% |
| `CircularVelocityAt500km` | `v ≈ 7.6126 km/s`, tol 0.01% |
| `PhaseWrapsAtTwoPi` | `θ` stays in `[0, 2π)` over 10⁶ steps |
| `FullPeriodReturnsToStart` | After exactly `T`, `θ` returns to initial within 1e-9 |
| `PositionMagnitudeEqualsRadius` | `‖pos‖ == a` for all `t`, all inclinations |
| `EquatorialOrbitHasZeroZ` | `i = 0 ⇒ pos.z == 0` |
| `PolarOrbitReachesMaxZ` | `i = π/2 ⇒ max‖pos.z‖ == a` |
| `NoDriftOverLongRun` | `θ(t)` computed directly == stepped 100 000× within 1e-9 |

`NoDriftOverLongRun` is the one that protects determinism: it fails if anyone replaces the
analytic form with incremental integration.

### 3.2 Transfer model — `tests/simulation/transfer_test.cpp`

| Test | Assertion |
|---|---|
| `HohmannLeoToGeo` | `Δv ≈ 3.8926 km/s`, tol 0.1% |
| `HohmannLeoToGeoTime` | `t ≈ 18990 s (316.5 min)`, tol 0.1% |
| `Hohmann500to800` | `Δv ≈ 0.1608 km/s`, tol 0.1% |
| `PlaneChangeOneDegAt500km` | `Δv ≈ 0.1329 km/s`, tol 0.1% |
| `PlaneChangeIsSymmetric` | `Δv(+Δi) == Δv(−Δi)` |
| `InclinationDominatesAltitude` | `Δv_plane(1°) > 0.8 × Δv_hohmann(500→800)` — **guards the corrected §4.3 finding** |
| `CombinedCheaperThanSeparate` | `Δv_combined ≤ Δv_hohmann + Δv_plane` for all sampled `Δi > 0` |
| `SynodicPeriod500to800` | `T_syn ≈ 91 518 s (1525.3 min)`, tol 0.1% |
| `ZeroTransferIsFree` | `r₁ == r₂`, `Δi == 0 ⇒ Δv == 0` |

`InclinationDominatesAltitude` exists specifically because the inherited audit got this
relationship wrong (`REPOSITORY_AUDIT.md` §4.1). The test pins the corrected fact.

### 3.3 Risk model — `tests/simulation/risk_test.cpp`

| Test | Assertion |
|---|---|
| `RiskInUnitInterval` | `risk ∈ [0,1]` over 10 000 randomised inputs |
| `NoDebrisGivesBaselineRisk` | zero debris ⇒ risk from complexity terms only |
| `RiskIncreasesAsDebrisNears` | monotonic decreasing in distance |
| `RiskZeroBeyondHazardRadius` | `d ≥ R_hazard ⇒ contribution == 0` |
| `PathCompositionIsBounded` | 100 legs at 0.5 each ⇒ risk < 1.0 |
| `PathCompositionIsMonotonic` | adding any leg never decreases path risk |
| `PathCompositionIsOrderIndependent` | permuting legs gives identical risk within 1e-12 |
| `TwoTenPercentLegsGiveNineteen` | `1−(0.9×0.9) == 0.19` exactly |

The monotonicity test is not cosmetic — `ALGORITHMS.md` §5.6's pruning-soundness argument
requires it. If it fails, the Pareto search is unsound.

### 3.4 Fuel and priority

`FuelFractionInUnitInterval` · `DeltaVMonotonicallyDecreases` ·
`InfeasibleWhenExceedsBudget` · `PriorityInUnitInterval` ·
`UrgencyRisesAsDeadlineNears` · `ExpiredEmergencyHasMaxUrgency` ·
`ModerateNearExpiryCanOutrankFreshCritical` (pins the intended triage behaviour of
`OPTIMIZATION_MODEL.md` §7.1).

---

## 4. Unit Tests — Algorithms

**These are the tests that matter most.** All use hand-built toy graphs with
independently-derived expected answers.

### 4.1 Graph — `tests/graph/` — **implemented in Phase 3, revised from the plan below**

> This section originally planned constant-weight edges (`A→B(1)`) and a determinism test
> keyed on scenario seed. Neither survived contact with the actual design: ORCA has no
> constant edge weight to assert (`GRAPH_MODEL.md` §3), and `MissionGraph` construction is
> a pure function of `(Scenario, TransferModel, Options)` with no seeded randomness of its
> own — determinism is inherited from `Scenario` and `TransferModel`, both already covered
> by Phase 1/2's own determinism tests, so re-testing it here would test the same property
> a second time under a different name. What actually ships, in
> `graph_structure_test.cpp` and `graph_builder_test.cpp`:

**Structure** (`graph_structure_test.cpp`, 22 tests): node/edge insertion and lookup ·
`AddNodeIgnoresCallerSuppliedId` · `EdgeIsDirectedNotBidirectional` ·
`RejectsSelfLoop` · `RejectsEdgeToUnknownNode` · `DetectsDuplicateEdges` (via
`isStructurallyValid()`) · a 12-node densely-connected sanity check.

**Builder** (`graph_builder_test.cpp`, 12 tests): `ExcludesTerminalSatellitesAndDisabledVehicles` ·
`CandidatePairsIsNTimesNMinusOne` ·
**`PhasingUnavailableIsItsOwnCategoryNeverGenericInfeasible`** ·
`RejectionAccountingIsExhaustive` (candidatePairs == feasible + Σrejections) ·
`CapabilityAndDeadlineAreNeverRejectedAtBuildTime` (Group 2/3 reasons must be zero at
build time — GRAPH_MODEL.md §3) · **`EdgeSetIsInvariantToReferenceTime`** (rebuilding at two
different reference times produces the identical edge set) · tight risk/distance ceiling
rejection · empty-scenario and single-node edge cases.

Every numeric expectation (`candidatePairs=12`, `feasibleEdges=10`,
`rejectedPhasingUnavailable=2`, …) was verified by compiling and running the exact fixture
scenario against the real `GraphBuilder` before being written into an assertion.

### 4.2 Dijkstra — `tests/algorithms/dijkstra_test.cpp`, `dijkstra_time_dependence_test.cpp`, `dijkstra_oracle_test.cpp`

> **Implemented in Phase 3.** The plan below anticipated constant-weight test graphs
> (`A→B(1)→D(1)`); ORCA has no such edges, so every test graph here is built from real
> `Orbit`s and real `TransferModel::evaluate()` calls, verified against the actual
> implementation before being trusted as an expected value (`GRAPH_MODEL.md` §8.2).
> `LinearChainOf1000` was not built — a 1000-node stress case is a `BENCHMARK_PLAN.md`
> concern, not a correctness one, and asserting a specific `nodesExpanded` for it here
> would test heap tie-breaking, not the algorithm.

**Basic correctness** (`dijkstra_test.cpp`, 9 tests): `TrivialPathWhenSourceEqualsTarget` ·
`DirectSingleEdge` · `UnreachableTargetNotFound` · `DisconnectedComponentIsUnreachable` ·
`EdgeDirectionIsRespected` (directed, not bidirectional) · `RejectsUnknownSourceOrTarget` ·
`EqualCostParallelPathsBothReachTarget` · two `SearchMetrics` consistency tests.

**Time-dependence — the headline property** (`dijkstra_time_dependence_test.cpp`, 5 tests):

- **`IndirectLegUsesArrivalTimeAtIntermediateNodeNotGlobalStart`** — the mandated
  "critical rule" test. Does not hardcode an expected wait/arrival number; it compares
  Dijkstra's actual second leg against two independent, freshly-called `evaluate()`
  invocations (one at the correct predecessor-arrival time, one at the search's global
  start) and asserts which one it matches, and by how much it differs from the other
  (verified >1000 s apart on the committed fixture).
- **`DijkstraChoosesIndirectRouteWhenGenuinelyFasterOnArrivalTime`** — A and C five
  kilometres apart in altitude (huge synodic period, direct route usually has to wait most
  of it); B a well-separated staging point. Direct edge present and feasible; Dijkstra
  chooses the two-leg route anyway because it arrives roughly 2.39 million seconds sooner.
- **`CumulativeBudgetIsNotEnforcedByDesign`** — two individually-affordable legs whose sum
  exceeds the vehicle's budget; the search still reports the route as found. This is a
  **documented limitation**, not an oversight — full argument in `GRAPH_MODEL.md` §6.3.
- `SingleLegExceedingTotalBudgetIsRejected` — the per-edge check the design *does* perform.

**Independent brute-force oracle** (`dijkstra_oracle_test.cpp`, 4 tests):
`AgreesWithBruteForceOnDirectedGraphWithACycle` (7 nodes, a real cycle, verified
edge-for-edge before being trusted) · `AgreesOnUnreachableTarget` ·
`AgreesOnIndirectRouteBeatingDirect` · `AgreesOnRandomisedSmallGraphs` (4 structurally
distinct small graphs: chain, diamond, star, back-edge cycle).

The oracle enumerates every *simple* path (bounded by a visited-set, so real cycles do not
cause non-termination) and evaluates each sequentially through `TransferModel` directly —
sharing no code with `Dijkstra.cpp` beyond the transfer model both are required to use.
Restricting to simple paths is sound, not just convenient: every objective composed along a
path is non-negative (`ObjectiveVector`'s invariant), so a repeated-node detour can never
beat the best simple path already found.

### 4.3 A* and its heuristic

**This section was rewritten in Phase 4.** The table this section previously showed
specified tests for a multi-objective heuristic (`h_fuel`, a 500-random-graph sweep,
`ExpandsNoMoreThanDijkstra` as an assertion) that does not match what Phase 4 actually
built or what turned out to matter. What is below is what exists, across six files rather
than one, split by concern the same way the Dijkstra suite is (§4.1–4.2).

**Heuristic structural properties** (`astar_heuristic_test.cpp`, 11 tests): zero at equal
radius regardless of phase/inclination, symmetry, independence from inclination and from
phase (including right at the `2π` wrap seam), finite and non-negative across the
realistic range and at an extreme (5×10⁷ km) separation, and agreement between
`estimateRemainingTimeSec` and `computeBreakdown().chosenSec`.

**Heuristic admissibility — three independent angles, never validated against A* itself**
(`heuristic_admissibility_test.cpp`, 3 tests):

- `AdmissibleAgainstDirectTransferAcrossRealisticGrid` — broad, cheap sweep: `h(n) ≤` one
  real direct `TransferModel` transfer's own time, across a 10×10 realistic-altitude grid.
- **`AdmissibleAgainstBruteForceMultiHopGraphOracle`** — the deep check: `h(n) ≤` the true
  minimum over *every simple path* in real multi-node graphs, including nodes placed
  deliberately near Earth's surface to stress the floor-detour term where it matters. This
  is what a direct-transfer-only check (above) could not catch, since a multi-hop detour
  is exactly the failure mode the old heuristic missed (`ALGORITHMS.md` §4.3).
- `FloorDetourMonotonicityLemma` — re-derives, against the real `hohmann::compute()`
  rather than the heuristic's own code, the specific claim the floor-detour term's
  admissibility rests on: `coast(ra,x) + coast(x,rb)` is minimised at the smallest
  physically valid `x`.

**Heuristic consistency — sampled, and honest about a real finding** (`heuristic_consist
ency_test.cpp`, 4 tests): `SampledTriplesAcrossRealisticRangeSatisfyConsistency` and a
wide-range variant (5000 samples each) check the *combined* heuristic; zero violations.
**`BothComponentsIndividuallyAcrossRealisticRange`** decomposes via `computeBreakdown` and
checks each component separately — `floorDetour` passes (0 violations, matching its
unconditional proof), and `direct` is asserted to **fail** (`directViolations > 0` is the
expected, correct outcome, not a bug) — this is the test that found the gap in an earlier
draft's "minimum of two consistent heuristics" justification, which assumed both
components were individually consistent when one is not
(`AStarHeuristic.hpp` §5, `HEURISTIC_DESIGN.md` §5).
`CombinedHeuristicSurvivesAdversarialNearGeoToLeoRegime` (20 000 samples, deliberately
concentrated on the source/goal-near-GEO, intermediate-near-LEO shape that breaks `direct`
alone) confirms the *combined* function still holds there.

**A* correctness** (`astar_test.cpp`, 11 tests): the same trivial/direct/unreachable/
disconnected/directed/unknown-node/tied-parallel-path/metrics scenarios `dijkstra_test.cpp`
uses, re-run through `findEarliestArrivalAStar` — deliberately identical scenarios where
possible, since the point is that A* is a drop-in alternative for this problem, not a
different one. Adds `heuristicEvaluations`-specific checks (`SearchMetrics.hpp`'s one field
with no Dijkstra analogue) and `CumulativeBudgetIsNotEnforcedByDesign` (§4.2's limitation
applies unchanged, since both algorithms build the identical per-edge
`TransferConstraints`).

**Time-dependence, the h=0 ablation, and reopening** (`astar_time_dependence_test.cpp`, 5
tests):

- The critical-rule test and the indirect-route test, re-run through A* on
  `dijkstra_time_dependence_test.cpp`'s exact fixture orbits — agreement, not merely
  similarity, is the point.
- **`ZeroHeuristicMatchesDijkstraOn{Direct,Indirect}Route`** — the mandated h=0 ablation
  (Phase 4 brief §45–46): a heuristic that always returns `0` must make A* match Dijkstra's
  found status, arrival time, total objectives and node sequence exactly. `heuristicEvalua
  tions` is checked to still differ (Dijkstra's is always `0`; A* still *calls* h even
  when h always returns `0`) — documented rather than left as an unexplained asymmetry.
- **`ReopeningCorrectsWronglyExpandedNodeUnderInconsistentButAdmissibleHeuristic`** — the
  test that exists because every other one leaves reopening unexercised: the real
  heuristic is consistent throughout ORCA's realistic range, so it never needs to reopen
  anything in a realistic scenario. Uses a custom, deliberately admissible-but-inconsistent
  heuristic (via `findEarliestArrivalAStar`'s override hook) on a 5-node graph where every
  g/h value was found by compiling and running the real code, not hand-derived. Confirms,
  via the trace, that a node is genuinely expanded twice with an improving `g`, and that
  the final answer still matches independent Dijkstra ground truth exactly.

**Cross-validation** (`astar_oracle_test.cpp`, 5 tests): the same independent brute-force
oracle pattern `dijkstra_oracle_test.cpp` uses (rewritten fresh here, not shared, for the
same independence reasons that file gives), re-run against A*, plus
**`DijkstraAndAStarAgreeOnRandomisedSmallGraphs`** — both algorithms must agree on
found/not-found, arrival time and every total objective on five structurally distinct
graphs, but are *not* required to return the same path when multiple routes tie
(`PathResult.hpp` specifies no canonical choice among ties).

**What justifies A\* existing is a measured, not asserted, question** — see
`BENCHMARK_PLAN.md`'s Phase 4 addendum. The honest answer on the graphs actually measured:
fewer-or-equal node expansions at every size, but slower wall-clock time at every size,
because heuristic evaluation cost was not repaid on those particular topologies.
`ALGORITHMS.md` §4.6–4.7 has the full reading.

### 4.4 FIFO property — `tests/algorithms/fifo_test.cpp`

**Guards Dijkstra's optimality under time-dependent weights** (`ALGORITHMS.md` §3.4).

```
FifoPropertyHoldsForAllEdges:
    for each edge e in a representative graph:
        for t₁ in [0, 10000] step 50:
            for t₂ in (t₁, 10000] step 50:
                assert  t₁ + w_time(e,t₁)  ≤  t₂ + w_time(e,t₂)
```

Also: `WaitingNeverArrivesEarlier`, `ArrivalTimeMonotonicInDeparture`.

**If this test fails, Dijkstra's and A*'s optimality claims are void.** The correct response
is to fix the transfer model, never to relax the test. This is stated here because a failing
FIFO test is easy to mistake for a tolerance problem.

### 4.5 Dominance — `tests/algorithms/dominance_test.cpp`

**Rewritten in Phase 5** to match the real implementation (`Dominance.{hpp,cpp}`,
`algorithms/`, not `optimization/` — `ALGORITHMS.md` §6.2). 19 tests, not the 9 originally
sketched: the core matrix (A dominates B, B dominates A, neither/genuine trade-off, equal
vectors, one-strictly-better, floating-point near-equality on both sides of the epsilon
threshold, all four objectives individually, non-finite values), equivalence as a concept
distinct from incomparability, the four properties (irreflexive/asymmetric/transitive/
partial) checked directly rather than by random sampling, and a dedicated block for
`dominatesWithEpsilon` (zero-epsilon-matches-exact, a case where a genuine trade-off
collapses only past a specific epsilon threshold — checked both sides, irreflexivity for
any epsilon, and negative-epsilon rejection).

`NeitherDominatesGenuineTradeOff` (the Phase 5 brief's own worked example: A faster/thirst
ier, B slower/leaner) is the test that proves ORCA is genuinely multi-objective — if it
ever passed with "dominates", the front would collapse to a single point.

### 4.6 Pareto search — `tests/algorithms/pareto_search_test.cpp`, `pareto_search_oracle_test.cpp`

**Rewritten in Phase 5**, split by concern the same way Dijkstra's suite is: basic
correctness and trade-off shapes in one file, independent oracle verification in another.

**Correctness** (`pareto_search_test.cpp`, 12 tests): trivial source-equals-target,
single-route (no alternatives), unreachable target, unknown node rejection, a genuine
two-route trade-off (real orbital data, exact numbers verified by compiling and running —
neither route dominates), a genuine **four**-route mutually non-dominated frontier (found
via a documented parameter sweep, not hand-picked — see the file's own comment on how much
search real trade-offs took to find), a dominated-candidate-never-survives check on that
same four-route graph, duplicate-route handling, a cyclic-graph termination check
cross-checked against independently-verified Dijkstra, the critical time-dependence rule
(identical requirement to Dijkstra/A*), and **two dedicated cumulative-delta-v tests**: one
reproducing exactly the scenario `Dijkstra.CumulativeBudgetIsNotEnforcedByDesign`
documents as unfixed and confirming Pareto search correctly rejects it where Dijkstra
wrongly accepts it, and one confirming rejection is scoped to the offending route — a
cheaper alternate route to the same target still survives.

**Independent oracle** (`pareto_search_oracle_test.cpp`, 6 tests): a brute-force
multi-objective enumerator sharing nothing with `Dominance.hpp`, `ParetoSearch.cpp` or
`ParetoFront` — dominance is reimplemented from scratch in the test file, with its own
epsilon constant, so a shared bug cannot validate itself
(`PARETO_SEARCH.md` §13 has the full reasoning). Checked: the two-route and four-route
trade-off scenarios above, the same 7-node cycle graph `dijkstra_oracle_test.cpp` uses, an
unreachable target, the cumulative-delta-v rejection scenario, and **five** structurally
distinct randomised graphs (chain, diamond, star, back-edge cycle, wider fan-in) — not just
hand-picked cases. Comparison is by *distinct cost vector*, not raw count, since production
search deliberately collapses exact duplicates to one representative while a naive oracle
retains every one (§4.5's `isEquivalent` is what makes that policy well-defined).

This oracle pairing is, per the Phase 5 brief's own framing, the single most valuable test
in this phase: brute force is obviously correct and obviously too slow; label-setting is
fast and subtle. Agreement between them on graphs small enough for brute force to be
exhaustive is the strongest evidence available.

### 4.6.1 Optimization layer — `tests/optimization/*_test.cpp`

Not originally scoped in this section at all (Phase 0 did not anticipate the
search/selection split being tested this granularly). Four files, 32 tests:
`objective_normalizer_test.cpp` (front-relative normalisation formula, the degenerate
constant-objective case rendering as `0` not `NaN`, ideal/nadir componentwise extrema,
inconsistent-bounds rejection), `optimization_profile_test.cpp` (every preset's weights
checked against `configs/profiles.json`'s literal values, weight validation — all-zero and
negative rejected, a single zero weight accepted), `pareto_front_test.cpp` (the
`algorithms::ParetoSearchResult` → `MissionPlan`-bearing `ParetoFront` conversion,
including the trivial-search-produces-empty-front boundary `MissionPlan::makeFeasible`'s
empty-route rejection creates), and `mission_scorer_test.cpp` (hand-computed
`scorePlan()` arithmetic, weight normalisation at point of use, and the full deterministic
tie-break chain — score, then risk, then time, then fuel, then `PlanId` — exercised at
every step of the chain, not just the final one).

### 4.7 Assignment — `tests/algorithms/assignment_test.cpp`

| Test | Assertion |
|---|---|
| **`HungarianMatchesBruteForce`** | `n ≤ 8`: compare against all `n!` permutations |
| `HungarianOnIdentityMatrix` | picks the diagonal |
| `HungarianHandlesRectangular` | `m ≠ n` via padding |
| `HungarianHandlesInfeasiblePairs` | `LARGE` entries reported unassigned |
| **`GreedyIsSuboptimalOnCounterexample`** | the §7.5 matrix: greedy 109, optimal 23 |
| `GreedyNeverBetterThanHungarian` | property, 1000 random matrices |
| `RegretIsNonNegative` | `greedy − optimal ≥ 0` always |
| `BothHandleEmptyInput` | no crash, empty assignment |

### 4.8 Priority queue and spatial index

`PopsInPriorityOrder` · `StableUnderEqualKeys` (deterministic tie-break) ·
`LazyDeletionSkipsStaleEntries` · `EventQueueChronological` ·
`EventQueueTieBreaksOnId` · `RebuildAfterPriorityChangeReorders`.

`SpatialIndexFindsAllInRadius` (vs brute force, 1000 random queries) ·
`SpatialIndexFindsNoFalsePositives` · `RebuildIsCheap` · `EmptyIndexReturnsNothing`.

---

## 5. Determinism Tests

`tests/integration/determinism_test.cpp` — FR-N5.

| Test | Method |
|---|---|
| `SameSeedIdenticalRun` | Run seed 928371 ten times; hash full state every 100 ticks; **all hashes identical** |
| `SpeedMultiplierDoesNotAffectResult` | Same seed at 1× and 16× ⇒ identical state history |
| `StepwiseEqualsContinuous` | 1000 single steps == one 1000-step run |
| `SeedSplittingIsIndependent` | Adding a debris object does **not** change the failure schedule |
| `NoRandomDeviceInBinary` | `strings`/symbol scan asserts `random_device` is absent |
| `PlanningIsDeterministic` | Same graph + profile ⇒ identical `planId` chosen, 100 runs |
| `TieBreakingIsStable` | Constructed exact-tie scenario ⇒ same winner every run |
| `NoStdDistributionUsed` | Grep test: `std::uniform_real_distribution` etc. absent from `cpp/` |

The last two matter more than they look. `std::*_distribution` output is
**implementation-defined**, so using it would make results differ between libstdc++ and
libc++ — determinism on one machine, not across machines (`SIMULATION_MODEL.md` §7.3).

---

## 6. Integration Tests

`tests/integration/`

| Test | Assertion |
|---|---|
| `EndToEndSingleRescue` | Load fixture → run → exactly 1 emergency, 1 success, 0 failures |
| `EndToEndMultiEmergency` | 3 emergencies, 2 vehicles; assignment used; all outcomes accounted |
| `DynamicReplanScenario` | Constructed debris event ⇒ `replanCount == 1`, new route has strictly lower risk |
| `NoFeasiblePlanScenario` | Casualty out of every Δv budget ⇒ `feasible:false` with `bindingConstraint.kind == DELTA_V_BUDGET` |
| `SurvivalWindowExpiry` | Window shorter than any transfer ⇒ `LOST`, no crash |
| `IncrementalPatchEqualsFullRebuild` | After a debris event, patched graph **identical** to full rebuild — edge-for-edge |
| `ReplanHysteresisPreventsOscillation` | Two near-equal routes ⇒ `replanCount ≤ 1` over 1000 ticks |
| `AllMissionsReachTerminalState` | No mission is left non-terminal at run end |
| `MetricsAccountingBalances` | `successes + failures + expired == totalEmergencies` |

`IncrementalPatchEqualsFullRebuild` is what makes the incremental optimisation safe to keep.
Without it, the fast path could silently diverge from the correct path.

> **Status as of Phase 8: §5 and §6 above are this project's original, Phase-0-era
> aspiration; the toolchain (GoogleTest/CMake FetchContent) and several named tests never
> materialised, and the actual coverage differs in real, documented ways.**
>
> - **Toolchain**: every test in this repository uses the minimal `tests/TestFramework.hpp`
>   harness via `scripts/run_tests.sh` (a direct-compile path), not GoogleTest — CMake is not
>   installed on the development machine (`docs/REPOSITORY_AUDIT.md` §3). `tests/CMakeLists.txt`
>   is kept up to date as the unverified target, per its own header comment.
> - **`IncrementalPatchEqualsFullRebuild` does not apply** — no incremental graph patch was
>   ever built (`ALGORITHMS.md` §11.2-11.3, `IMPLEMENTATION_ROADMAP.md` P12). Debris
>   invalidation is route-filtering against the existing, unmodified graph, not a patch, so
>   there is nothing for this test to compare against a full rebuild.
> - **What Phase 8 actually built and tested, in the equivalent spirit of §5-6**:
>   `tests/simulation/simulation_engine_test.cpp`'s
>   `SameSeedAndScriptProduceIdenticalOutcomes` (replay determinism, the real analogue of
>   `SameSeedIdenticalRun`), `ResetThenRerunProducesTheSameOutcomeAsAFreshEngine`,
>   `LargeAdvanceProcessesEveryEventNoneSkipped` (the real analogue of
>   `StepwiseEqualsContinuous`), `EventBurstAtTheSameTimestampAllProcessExactlyOnce`,
>   `LongRunProducesNoInvalidState`, and
>   `RepeatedIdenticalTriggersDoNotCauseUnboundedReplanning` (the real analogue of
>   `ReplanHysteresisPreventsOscillation`); plus `end_to_end_demo_test.cpp`'s single
>   hand-verified full-chain scenario (the real analogue of `EndToEndSingleRescue` /
>   `DynamicReplanScenario`). `SIMULATION_ENGINE.md` §12-14 has the full description of what
>   each of these actually verifies. `NoStdDistributionUsed`-equivalent coverage exists
>   structurally: `RandomGenerator` (Phase 1) never uses `std::*_distribution`, confirmed by
>   code inspection at every phase that has touched randomness since, including Phase 8's
>   `EmergencyGenerator`.
> - **Not built in Phase 8**: `SpeedMultiplierDoesNotAffectResult` as a named test (though the
>   underlying property holds by construction — `SimulationClock` never reads
>   `speedMultiplier()` internally, `SIMULATION_ENGINE.md` §3); `SeedSplittingIsIndependent`;
>   `NoRandomDeviceInBinary`; `SurvivalWindowExpiry`; `MetricsAccountingBalances` as a
>   standalone test (though `SimulationMetrics` fields are individually asserted across the
>   engine test suite).

---

## 7. Contract Tests

`frontend/tests/contract/` — the cross-language seam.

| Test | Assertion |
|---|---|
| `FixturesParseAgainstTypes` | Every fixture validates against the TypeScript types |
| `AllMessageTypesHandled` | Exhaustive `switch` over `EngineMessage` compiles (compile-time) |
| `NoNaNOrInfinityInFixtures` | All numerics finite |
| `UnitsMatchContract` | Angles in degrees, times in seconds, fractions in `[0,1]` |
| `SnapshotFieldsReadonly` | Type-level assertion |
| `UnknownFieldsIgnored` | Extra field doesn't break parsing (forward compatibility) |
| **`DisplayedValuesMatchFixtureExactly`** | Render components with fixture data; assert displayed numbers equal fixture numbers |

The last is the mechanical enforcement of the single-source-of-truth rule
(`ARCHITECTURE.md` §10). If a React component starts deriving a value instead of displaying
it, and its derivation drifts from the engine's, this test fails.

Fixtures are **recorded from real engine runs**, never hand-written (`API_CONTRACT.md` §9).

---

## 8. Frontend Tests

Vitest + React Testing Library. Presentational focus:

- Formatting: seconds → `mm:ss`, fraction → `13.6%`, km → thousands separators.
- Empty, loading and error states render for every panel.
- Store: snapshot replaces cleanly; events append in order; stale `reqId` responses discarded.
- Slider debounce fires once per settled drag.
- Accessibility: axe-core on every screen; keyboard navigation reaches every control; focus is
  visible; colour-blind simulation confirms status remains distinguishable by shape.

**Explicitly not tested here:** any mission calculation — because there should be none.

> **Status as of Phase 9: §7-8 above are the Phase 0 aspirational sketch; real coverage
> differs in the ways `INTEGRATION_ARCHITECTURE.md` §10 documents in full.** No
> `frontend/tests/contract/` fixture-replay suite or axe-core accessibility pass was built
> — out of this phase's explicit scope (§65: "no final UI polish," and no scenario-file
> fixtures exist to record from, since `--serve` always runs one fixed demo scenario). What
> was built instead, achieving the same actual goals these sections describe:
> `frontend/test/engineService.test.ts` (mocked-fetch contract coverage: success/error
> shapes, request bodies), `frontend/test/EngineContext.test.tsx` (connection-state
> transitions, snapshot-replaces-not-merges — the real analogue of "store: snapshot
> replaces cleanly"), and `frontend/test/{StatusBar,ControlPanel,ObjectInspector,
> EngineOfflineOverlay}.test.tsx` (component rendering against a real-values fixture,
> disabled/enabled control states, empty/offline states — the real analogue of
> `DisplayedValuesMatchFixtureExactly` and "empty, loading and error states render"). 25
> frontend tests total. The backend's own 23 tests (`backend/test/`) cover the NDJSON
> parser and the Node↔real-engine seam directly — closer to this section's "contract" intent
> than a frontend-only fixture suite would have been, since they run against the actual
> engine binary rather than a recording of one.

---

## 9. Architecture Tests

Mechanical enforcement of the layering that makes P3/P5 real.

| Test | Method |
|---|---|
| **`NoUpwardIncludes`** | Script over `cpp/`: `algorithms/` must not include `planning/`, `simulation/`, `io/`, `app/`; `graph/` must not include `algorithms/`; etc. |
| **`EngineBuildsWithoutNode`** | CI job builds and runs all C++ tests in a container with **no Node installed** |
| `CoreHasNoIODependency` | `core/`, `domain/`, `graph/`, `algorithms/` must not include `<iostream>`/`<fstream>` |
| `NoGlobalMutableState` | Scan for non-const namespace-scope variables |
| `NoRandomDevice` | Symbol scan |
| `NoMagicNumbersInAlgorithms` | Heuristic lint: bare float literals outside `constants` and tests |
| `FileSizeLimit` | Warn > 300 lines, fail > 600 without an `// LARGE-FILE:` justification comment |

`EngineBuildsWithoutNode` is the one that keeps `ARCHITECTURE.md` P3 honest. Everything else
is documentation until this job passes.

---

## 10. Performance Tests

Guard rails against regression, distinct from benchmarks (`BENCHMARK_PLAN.md` measures;
these fail the build).

| Test | Threshold | Source |
|---|---|---|
| `PlanLatency100Objects` | < 100 ms | NFR-1 |
| `GraphBuild500Objects` | < 250 ms | NFR-4 |
| `SnapshotRate` | ≥ 10 Hz sustained | NFR-2 |
| `ParetoTerminatesAt1000` | < 5 s with default caps | — |
| `NoQuadraticMemoryGrowth` | memory at `V=1000` < 4× memory at `V=500` | — |

Thresholds are set with generous headroom and are **regression detectors**, not targets. They
run on a labelled CI machine, since absolute timings are hardware-dependent.

---

## 11. E2E Tests

Playwright, Tier 3, after the UI stabilises.

`LoadScenarioAndRun` · `EmergencyTriggersRecommendation` ·
`ProfileSwitchChangesRecommendationWithoutResearch` (asserts **no new `plan` request** is
issued — only `reselect`) · `DispatchAnimatesTrajectory` · `ReplanSwitchesTrajectory` ·
`AlgorithmLabSteps` · `EngineCrashShowsError` (kill the child process, assert the banner) ·
`ExportDownloadsCsv`.

---

## 12. Edge Cases

Every one is a test.

**Scenario:** empty scenario · 1 satellite, 0 vehicles · 1000 satellites · zero-duration ·
malformed JSON · missing required field · negative altitude · altitude below `R⊕` ·
inclination > 180° · duplicate IDs.

**Graph:** empty · single node · complete graph · all edges infeasible · casualty unreachable ·
casualty is the vehicle's own node.

**Planning:** front of size 1 · all plans identical cost · deadline already passed at planning
time · vehicle with exactly enough Δv (boundary) · vehicle with Δv one epsilon short.

**Simulation:** two emergencies at the same tick · emergency during an active mission ·
vehicle disabled mid-transfer · debris alert on the final leg · replan with no alternative ·
`Δt` larger than a leg's duration.

**Numeric:** zero weights (rejected) · all weights equal · `ε` larger than the front's spread ·
identical objective values across the front · very large and very small altitudes.

---

## 13. CI

```yaml
on: [push, pull_request]
jobs:
  cpp:            # macOS + Linux × Debug/Release
    - configure, build with -Wall -Wextra -Werror
    - ctest (unit, property, integration, determinism, architecture)
    - coverage gate per §2
  cpp-no-node:    # Linux container WITHOUT Node installed
    - build + full C++ test suite        ← enforces ARCHITECTURE.md P3
  frontend:
    - tsc --noEmit, eslint, vitest, axe
  contract:
    - validate recorded fixtures against TypeScript types
  perf:           # labelled runner only
    - §10 thresholds
```

Merge gate: **all jobs green, no exceptions.** A skipped test is a failed test.

---

## 14. Test Data

```
tests/fixtures/
├── graphs/        toy graphs with hand-computed answers
│   ├── diamond.json                  known shortest path
│   ├── pareto-3path.json             hand-computed 3-member front
│   └── disconnected.json
├── scenarios/
│   ├── single-rescue.json            deterministic, 1 emergency
│   ├── multi-emergency.json          3 emergencies, 2 vehicles
│   ├── replan-trigger.json           scripted debris event
│   ├── infeasible.json               out of every Δv budget
│   └── greedy-suboptimal.json        ALGORITHMS.md §7.5 counterexample
├── matrices/
│   └── hungarian-cases.json          with brute-force optimal answers
└── ndjson/                           recorded from real engine runs
    ├── demo-seed-928371.ndjson
    ├── planresult-feasible.json
    └── planresult-infeasible.json
```

**Rule.** Graph and matrix fixtures carry hand-derived or brute-force-derived expected answers
committed alongside them. NDJSON fixtures are recorded from real runs. **No expected value is
ever generated by the implementation it tests** — that would only assert that the code does
what it currently does.

---

## 15. What Would Falsify ORCA's Claims

The tests that, if they failed, would mean a headline claim is wrong. Worth listing explicitly:

| Claim | Falsifying test |
|---|---|
| "The Pareto front is exact" (in exact mode -- never claimed under ε-dominance) | `ParetoSearchOracle.AgreesOnRandomisedSmallGraphs` (+ the other 5 oracle cases) |
| "Pareto search correctly enforces cumulative delta-v where Dijkstra/A* cannot" | `ParetoSearch.CumulativeDeltaVHardConstraintRejectsWhatDijkstraWronglyAccepts` |
| "A* is optimal" | `AStarOracle.AgreesWithBruteForceOnDirectedGraphWithACycle` (+ `AgreesOnRandomisedSmallGraphs`) |
| "A* agrees with Dijkstra on every graph tested" | `AStarOracle.DijkstraAndAStarAgreeOnRandomisedSmallGraphs` |
| "The A* heuristic never overestimates" | `HeuristicAdmissibility.AdmissibleAgainstBruteForceMultiHopGraphOracle` |
| "Dijkstra is optimal here" | `FifoPropertyHoldsForAllEdges` |
| "Dominance pruning is sound" | `Dominance.IsTransitive` + `Dominance.IsPartialNotTotal` + `ParetoSearchOracle`'s agreement across all six cases (soundness would show up as an oracle disagreement, not a standalone unit test) |
| "Hungarian is optimal" | `HungarianMatchesBruteForce` |
| "Greedy is worth comparing against" | `GreedyIsSuboptimalOnCounterexample` |
| "Runs are reproducible" | `SameSeedIdenticalRun` |
| "The engine is independent of the frontend" | `EngineBuildsWithoutNode` |
| "The frontend computes nothing" | `DisplayedValuesMatchFixtureExactly` |
| "A mandatory replan never applies the switching-margin hysteresis" | `DynamicReplanner.MandatoryReplanIgnoresSwitchMarginEvenWhenAlternativeIsOnlySlightlyBetter` |
| "The replanner does not oscillate between two near-equal plans" | `DynamicReplanner.DoesNotOscillateBetweenTwoNearEqualPlans` |
| "Replanning uses the vehicle's remaining resources, not its original budget" | `DynamicReplanner.RemainingDeltaVNotOriginalBudgetGovernsReplanning` |
| "The simulation behaves identically regardless of render speed" | `SimulationClock` never reads `speedMultiplier()` internally (structural, `SIMULATION_ENGINE.md` §3) |
| "A large `advance()` call never silently skips a due event" | `SimulationEngine.LargeAdvanceProcessesEveryEventNoneSkipped` |
| "Same seed + same script produces an identical run" | `SimulationEngine.SameSeedAndScriptProduceIdenticalOutcomes` |
| "Resource consumption is cumulative and a replan never resets it" | `MissionExecutor.ResourceConsumptionIsCumulativeNeverReset` |
| "A long run never produces NaN, negative time, or unbounded queue growth" | `SimulationEngine.LongRunProducesNoInvalidState` |

If any of these fails, the correct action is to fix the system or **retract the claim** — not
to weaken the test.

**Deliberately absent: "A\* is worth having."** An earlier version of this table listed a
test for that claim. Phase 4's benchmark (`ALGORITHMS.md` §4.6) found A* expanded the same
or fewer nodes than Dijkstra at every size tested, but was slower in wall-clock time at
every size tested — so "worth having" is not, in fact, an unconditional claim this project
makes. It is not in this table because it is not true as a general statement, not because
it was untested. The row above it — "A* agrees with Dijkstra" — is the claim that *is*
true unconditionally and belongs here.
