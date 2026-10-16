# ORCA — Assignment Model

Version 1.0 · Phase 6 · 2026-09-03

How ORCA goes from *one vehicle, one emergency, many candidate routes* (Phases 4–5) to
*many vehicles, many emergencies, one globally-coordinated set of missions* — Hungarian
optimal assignment built from scratch on top of the real Pareto search, not a substitute
metric.

---

## 1. Problem

Given `N` rescue vehicles and `M` active emergencies, decide which vehicle serves which
emergency (if any), via which mission plan, minimising total assignment cost while
respecting orbital-mechanics feasibility, delta-v budgets, survival-window deadlines, and
emergency priority. Unlike Phases 4–5, which answer "what is the best route for *this*
vehicle to *this* emergency," Phase 6 answers "what is the best *simultaneous* set of
vehicle-emergency pairings across the whole fleet" — a distinct combinatorial problem
(bipartite minimum-cost matching) layered on top of, not replacing, the routing problem.

## 2. Why This Is Not Just "Run Pareto Search Many Times"

Running `findParetoRoutes()` once per `(vehicle, emergency)` pair produces `N * M`
independent fronts — but a front tells you the best *route*, not whether *this* vehicle
should be the one serving *this* emergency once every other vehicle's options are also on
the table. Two vehicles might both be individually best-suited to the same emergency; only
one can go. A vehicle might be the fleet's best all-around performer, yet the fleet-optimal
outcome requires it to take its *second*-best emergency so a weaker vehicle can take the one
job it is not terrible at (§11's real example). This is the classical assignment problem —
minimum-cost perfect (or partial) bipartite matching — and Phase 6 brief §16's point stands:
a cheapest-first, per-vehicle-independent choice is not the same as a jointly optimal one,
and the gap between them is measurable, not hypothetical.

## 3. Architecture: Three Layers

```
Candidate Generation          Assignment Optimization        Validation / Reporting
--------------------------    --------------------------     --------------------------
Vehicle x Emergency -> Pareto  Cost matrix -> Hungarian /      AssignmentResult ->
search (real, from Phase 5)    Greedy solver (domain-          validated, materialised
-> CandidateFront              agnostic)                       missions + metrics
-> MissionScorer -> Scored-
Candidate
```

Strict separation, checked by what each layer's code is allowed to import, not merely by
convention:

- **`algorithms/Hungarian.hpp` / `algorithms/GreedyAssignment.hpp`** know nothing about
  vehicles, emergencies, or orbital mechanics — they solve "minimise total cost over an
  `n x n` matrix," full stop. Same directory-placement discipline as `Dominance.hpp`
  (`ALGORITHMS.md` §6): a domain-agnostic algorithm lives in `algorithms/`, not
  `optimization/`.
- **`transfer/TransferModel.hpp`** knows nothing about Hungarian, greedy, or assignment at
  all — it is unchanged by Phase 6.
- **`optimization/MissionScorer.hpp`** (Phase 5) knows nothing about assignment
  optimisation — it scores one candidate's route, not a fleet-wide matrix.
- **`optimization/CandidateGeneration.hpp` / `AssignmentCost.hpp` / `AssignmentMatrix.hpp`
  / `AssignmentResult.hpp`** are the only Phase 6 code that knows about *both* orbital
  mechanics and assignment — deliberately, since connecting the two is exactly what this
  layer is for.

## 4. Candidate Generation

`CandidateGeneration.hpp`'s `generateAllCandidateFronts()` runs `findParetoRoutes()` exactly
once per `(vehicle, emergency)` pair, in `vehicles`-major order — a fixed, deterministic
order, not incidental (needed for the entries-array indexing `AssignmentMatrix` relies on).

**Vehicle -> `NodeId`, emergency -> `NodeId`.** `MissionGraph` has no entity-to-`NodeId`
lookup (checked directly by reading the full header, not assumed absent) — building one is
this layer's responsibility alone, since only assignment needs "which node is vehicle V."
`EntityNodeIndex` (`buildEntityNodeIndex()`) is a one-time `O(V)` scan producing two hash
maps, built once per planning pass and reused across every candidate pair.

**Constraints, precisely.** `SearchConstraints::latestArrivalSec` is
`emergency.deadlineSec()`, reusing Group 3's existing per-relaxation deadline check
(`GraphBuilder.hpp`) rather than building a second one.
`SearchConstraints::vehicleDeltaVBudgetKms` is `vehicle.deltaVRemainingKms()`, not
`deltaVBudgetKms()` — the *currently affordable* amount, matching
`RescueVehicle::canAfford()`'s own semantics, correct even for a partially-spent vehicle
(every scenario Phase 6 built starts with fresh vehicles, so this distinction changes no
test's numbers, but it is a deliberate choice, not an unexamined one).

**Infeasibility diagnosis — honestly bounded, not exhaustive.** An empty front does not say
*why*. `ConstraintKind::NoAsset` is assigned before any search runs, from
`RescueVehicle::isAvailable()`. Otherwise, up to two additional searches run *only on the
already-confirmed-infeasible path* (never the common case): relaxing the deadline alone,
then relaxing the delta-v budget alone. If exactly one relaxation succeeds, that is the
reported binding constraint. If *neither* alone succeeds, the reason is
`ConstraintKind::Unreachable` — an honest simplification: it does not check whether relaxing
*both together* would help, which is not the same claim as "genuinely unreachable under any
combination." `ConstraintKind` is the existing enum from `core/Types.hpp`
(`{DeltaVBudget, SurvivalWindow, RiskCeiling, NoAsset, Unreachable}`) — reused rather than
inventing a parallel one, per the project's established discipline.

## 5. The Search/Score Split, and Why It Structurally Guarantees "No Re-Search"

`MissionCandidate.hpp` defines two distinct structs for the same reason
`ParetoSearch`/`MissionScorer` are separate functions (Phase 5): re-scoring under a
different `OptimizationProfile` must never re-run search.

- **`CandidateFront`** — search-stage output. `generateCandidateFront()` /
  `generateAllCandidateFronts()` are the *only* functions in Phase 6 that call
  `findParetoRoutes()`.
- **`ScoredCandidate`** — scoring-stage output, built *from* a `CandidateFront` by
  `scoreCandidates()`, which reads only already-computed front data and never touches the
  graph, the transfer model, or search.

This is not merely documented; it is checked directly by
`pareto_assignment_integration_test.cpp`'s
`RescoringUnderADifferentProfileDoesNotAlterFrontData` — the same `fronts` vector, scored
twice under `balanced()` and `fastest()`, produces byte-identical front data (same PlanIds,
same solution counts) both times, while the resulting costs genuinely differ. If
`scoreCandidates()` had re-run search, PlanIds and/or solution sets could legitimately
differ between the two calls (a fresh search is not guaranteed to reproduce identical PlanId
numbering); observing zero change is what "no re-search" means in practice, not merely a
type-signature argument.

## 6. Emergency Priority

`EmergencyPriority.hpp` — which casualty to serve, distinct from `MissionScorer`'s route
selection (`OPTIMIZATION_MODEL.md` §7 vs §6), and distinct from `OptimizationProfile`'s
route-selection weights (`configs/priority.json` vs `configs/profiles.json`). Two
deliberately un-mergeable weight systems: folding them into one vector would conflate "how
should this vehicle fly" with "who gets a vehicle at all."

```
priority = (w_severity * severityScore + w_urgency * urgencyAt(now) + w_feasibility * feasibilityScore)
           / (w_severity + w_urgency + w_feasibility)
```

Defaults `{0.4, 0.4, 0.2}` (`configs/priority.json`, `defaultPriorityWeights()`), specified
in Phase 0 but with no implementation or config file until Phase 6. `severityScore()` is the
existing `constexpr` function in `core/Types.hpp` — reused, not duplicated (an early draft
of `EmergencyPriority.cpp` did duplicate it; the compiler caught the redefinition
immediately, and the duplicate was removed in favour of the existing one).
`feasibilityScore(bool)` is `1.0` if any feasible candidate exists for that emergency across
*any* vehicle, else `0.2` — damping, not excluding
(`OPTIMIZATION_MODEL.md` §7.1: "a vehicle completing another mission may bring it into
range").

## 7. Assignment Cost: Global Normalisation, and the Doc Inaccuracy It Corrects

`AssignmentCost.hpp`'s central conceptual work — full context in that file's own header
comment, restated here because it is one of the most important design decisions in Phase 6.

**The problem.** `MissionScorer::scorePlan()` (Phase 5) normalises a route's cost against
*that specific `(vehicle, emergency)` pair's own* `ideal`/`nadir`
(`OPTIMIZATION_MODEL.md` §5.1's explicit "scores are not comparable across different
planning problems"). Every `(vehicle, emergency)` pair is, precisely, a different planning
problem — each has its own front. `OPTIMIZATION_MODEL.md` §8.1 claimed, since Phase 0, that
assignment-matrix entries were "comparable within a row (same emergency, same front)." That
was never true for any architecture Phase 5 could have built consistently with the rest of
this project — Phase 5 gives every `(vehicle, emergency)` pair its *own* front, never one
shared across vehicles for the same emergency — so "same front" was never a real property to
rely on. It was written before Phase 5 existed and never checked against the real
architecture until Phase 6 needed to actually compare matrix cells.

**Checked directly, not just argued.**
`assignment_cost_test.cpp`'s `FrontRelativeScoresCollideButGlobalAssignmentCostDoesNot`
constructs two vehicles with genuinely different fronts to the same emergency: V1 (close,
cheap neighbourhood) and V2 (far, expensive neighbourhood), each with a real two-point
front. Their **front-relative** scores land at the *identical* value — `0.300000` for both
— despite V1's selected route costing `fuel=0.0293` against V2's `fuel=0.8627`. Two
genuinely different missions, scored identically, because each was normalised against its
own front's `[ideal, nadir]` rather than a shared one.

**The fix.** `computeGlobalIdeal()` / `computeGlobalNadir()` compute `ideal`/`nadir` across
**every feasible candidate's selected-plan cost in the entire matrix**, not per pair. Every
candidate is then scored against those *shared* bounds via the exact same
`normalize()`/`scorePlan()` machinery Phase 5 already built — no new scoring mathematics,
only new (global, not per-pair) bounds fed into it. Re-run under the global bounds, the same
two vehicles above score `0.45` (V1) and `1.05` (V2) — no longer colliding, and correctly
reflecting that V2's plan is the worse one.

**The formula**, `OPTIMIZATION_MODEL.md` §8's shape reused exactly, with the corrected
normalisation basis substituted for `score(...)`:

```
cost(V, E) = globalScore(V, E) * (2 - priority(E))
```

`priority(E)` is computed once per emergency (§6), not once per pair — a property of the
emergency, not of any one vehicle's attempt at it (checked directly by
`assignment_cost_test.cpp`'s `PriorityIsIdenticalAcrossEveryVehicleForTheSameEmergency`).
`(2 - priority)` ranges over `[1,2]`, biasing the minimiser toward higher-priority
emergencies without letting priority alone overwhelm the mission-quality score.

**Infeasible entries.** `kInfeasibleAssignmentCost = 1.0e9` — a large, finite, documented
constant, never `std::numeric_limits<double>::infinity()`. The brief's own warning: Hungarian's
row/column reduction performs subtraction on matrix entries, and `inf - inf = NaN` would
silently corrupt the matrix. Bounded well above every real feasible cost (`[0,2]` by the
formula above), so no real assignment can ever be numerically confused with an infeasible
one.

## 8. The Assignment Matrix: `(N+M) x (N+M)` Block Padding

`AssignmentMatrix.hpp` — full derivation in that file's own header comment; summarised here.

**Why not `max(N,M)`.** A naive `max(N,M)`-square padding leaves the equal-count case
(`N == M`) with *no padding at all*. If some vehicle has zero feasible emergencies in that
case, Hungarian's perfect-matching requirement has no legal escape and is forced to select
an infeasible real pairing anyway — exactly the "fabricated solution" Phase 6 brief §13
forbids. This was caught during design review, before any test was written, not after a
failure.

**The fix** — the standard technique for optional (not-necessarily-total) bipartite matching
via a solver requiring a *perfect* matching:

```
                   M real emergency cols     N dummy-emergency cols
  N real vehicle    [ real costs, or           [ 0 -- vehicle idle,
  rows                kInfeasibleAssignment-      any of the N slots,
                       Cost if infeasible ]        cost is the same ]

  M dummy-vehicle    [ unassignedPenalty(         [ 0 -- never a real
  rows                 priority(col)) --            outcome, balances
                       identical across all          the block matrix
                       M dummy rows          ]        to square       ]
```

Every real vehicle row always has `N` zero-cost idle columns; every real emergency column
always has `M` dummy-vehicle rows offering it at a bounded penalty. Since
`kInfeasibleAssignmentCost` dwarfs both escape hatches, an optimal solver never voluntarily
selects an infeasible real cell while any escape hatch remains — checked, not just argued,
by `AssignmentResult.cpp`'s post-solve validation (§10) and by
`assignment_matrix_test.cpp`'s block-by-block tests.

**Cost.** `(N+M) x (N+M)`, so Hungarian's `O(n^3)` becomes `O((N+M)^3)` here — larger than
the unsound `O(max(N,M)^3)` alternative, in exchange for the correctness guarantee above.

**The unassigned-emergency penalty — a bug found and fixed while building the priority
integration test (§17.2).** `unassignedPenalty(priority) = kUnassignedEmergencyBasePenalty *
(1.0 + priority)`, base `10.0`, ranging over `[10, 20]` — deliberately the mirror image of
§7's real-pair multiplier `(2 - priority)`, which *decreases* with priority (making a
high-priority emergency cheaper, hence more attractive, to actually serve). Abandoning a
high-priority emergency must cost *more*, not less. An earlier version of this function used
`(2.0 - priority)` here too — the same expression as the real-pair multiplier, copied
without re-deriving the correct direction for a penalty rather than a reward. That made it
strictly *cheaper* to leave a Critical-severity, near-deadline emergency unserved than a
Low-severity, distant one: the opposite of what "priority" is supposed to mean. §17.2 has
the full account of how this was found.

## 9. Greedy Baseline: Rectangular, Not Padded

`GreedyAssignment.hpp`'s `solveGreedyRectangularAssignment()` — repeatedly pick the
cheapest still-available, still-feasible `(vehicle, emergency)` pair until one side runs
out, exactly Phase 6 brief §16's description.

**Why this operates on the natural rectangular matrix, not Hungarian's padded square one —
a design correction, not the first attempt.** Running greedy over the *same* padded matrix
built for Hungarian was tried first (an appealing idea: identical input, maximally fair
comparison) and found badly wrong by end-to-end smoke testing, before it became a permanent
test that would have enshrined the bug as expected behaviour. The padded matrix's
"vehicle idle" cells cost exactly `0`. A cheapest-pair-first greedy strategy, given a matrix
containing `0`-cost cells, picks every one of them before ever considering a real cell with
positive cost — because they are, individually, the cheapest thing available. The result:
greedy assigned every vehicle to an idle slot and every emergency to "unassigned" before
ever serving anyone, reporting a trivial `totalCost = 0` with **zero real assignments**.
Hungarian does not fall into this trap because it minimises the *global* sum, where leaving
emergencies unassigned costs far more in aggregate (§8's penalty) — but greedy, by
construction, never looks at the global picture. The fix: operate directly on the
rectangular `(vehicles x emergencies)` real matrix, with infeasible cells excluded via an
explicit boolean mask rather than priced as a large-but-still-comparable number — matching
the brief's own literal algorithm description, which never mentioned padding at all.

## 10. `AssignmentResult`: Status Semantics and Post-Solve Validation

`AssignmentResult.hpp` — runs a solver on an `AssignmentMatrix`, validates the result
against real feasibility, and materialises the final assignments with their actual
`MissionPlan`s.

**Status is about emergencies served, not vehicles used.** `Success` means every emergency
received a real, feasible vehicle. A vehicle ending up idle (more vehicles than emergencies,
or simply not needed) is an expected, unremarkable outcome — never treated as a partial
failure. `PartiallyAssignable` means some but not all emergencies were served;
`Infeasible` means none were.

**Post-solve validation, even though the `(N+M) x (N+M)` design should make it structurally
impossible.** Every real assignment Hungarian returns is checked against
`AssignmentMatrix::feasibleReal[row][col]` before being accepted; any real cell the design
should have made unreachable but that a solver bug somehow selected anyway falls back to
"treated as unused/unassigned" rather than being reported as a legitimate assignment (Phase
6 brief §13's explicit requirement: never fabricate a solution, even defensively).

## 11. The Real ORCA-Data Counterexample (Scenario F)

Phase 6 brief §16: "the tiny matrix [conceptual counterexample] is only a conceptual
validation case" — a real scenario, using actual `TransferModel`-derived costs, is required
too.

**The scenario**: two vehicles sharing the *exact same orbit* (500km altitude, 10°
inclination), differing only in remaining delta-v budget (5.0 km/s vs 2.8 km/s), against two
real emergencies (one nearby, one farther). Both vehicles can reach both emergencies —
nothing is infeasible. Real, physically grounded reason for the divergence: `fuel =
deltaV_required / vehicle_budget`, so the *identical* physical manoeuvre scores as cheap for
the well-fuelled vehicle and expensive for the fuel-constrained one.

```
             E-101       E-102
  V-1        0.450000    0.673828
  V-2        0.493804    1.120000
```

Greedy grabs `(V-1, E-101) = 0.450000`, the single globally cheapest cell, first — stranding
the fuel-limited V-2 with the expensive job. Total: `0.450000 + 1.120000 = 1.570000`.
Hungarian looks ahead and swaps: `(V-1, E-102) + (V-2, E-101) = 0.673828 + 0.493804 =
1.167632`. Both solvers reach full coverage (`AssignmentStatus::Success` for both — an
apples-to-apples comparison, not confounded by one solver leaving an emergency unserved),
and Hungarian wins by a real, measured margin of `0.402368`.

This was not the first geometry tried. Earlier attempts (documented in the implementation
history, not repeated here) using pure-inclination or pure-RAAN differences kept producing
*agreement* between the two solvers — traced to a real structural reason: when one vehicle's
best pair is a genuinely free/coincident-orbit match, angular-distance-based costs on a
sphere satisfy a triangle inequality through that shared point, which forecloses the
greedy-trap condition by construction. Varying delta-v *budget* instead of geometry sidesteps
that constraint, since fuel-fraction cost is not a geometric distance and is not bound by
the triangle inequality — which is why this is the scenario that actually produced
divergence, not an arbitrary choice.

## 12. Hungarian Algorithm: Implementation and Correctness

`algorithms/Hungarian.hpp` — classic `O(n^3)` Kuhn-Munkres via successive shortest
augmenting paths with dual potentials (row potentials `u[]`, column potentials `v[]`),
1-indexed internally with a dummy row/column 0. Maintains `cost[i][j] - u[i] - v[j] >= 0` as
an invariant; grows a shortest-path tree from each row in turn via Dijkstra-like relaxation
over reduced costs, until reaching an unmatched column, then augments.

**Correctness, in one paragraph** (`Hungarian.hpp`'s own header has the full version): the
potentials are a certificate. For any matching `M` respecting `cost[i][j] >= u[i] + v[j]`
everywhere with equality on `M`'s edges, `M`'s total cost equals `sum(u) + sum(v)` — and by
LP duality, no matching can cost less than `sum(u) + sum(v)` for *any* valid potentials. The
algorithm maintains feasibility of the potentials as an invariant at every step, so the
final matching's cost, which equals the final `sum(u) + sum(v)`, is optimal by construction.

**Verified against known answers**: the brief's own `[[1,2],[2,100]]` example (optimal `4`,
not greedy's `101`); a textbook 3x3 (optimal `10`, verified by exhaustive enumeration of all
6 permutations); a 3x3 with one infeasible cell per row (optimal `33`, of the two
sentinel-avoiding permutations); and — independently, sharing no code with `Hungarian.cpp`
— a brute-force permutation oracle (`hungarian_oracle_test.cpp`) checking agreement across
72 randomised matrices (`n=2..7`) plus targeted sentinel-scale and tied-cost cases.

## 13. Independent Verification Discipline

Following the same discipline `pareto_search_oracle_test.cpp` established in Phase 5: the
Hungarian oracle (`hungarian_oracle_test.cpp`) shares nothing with `Hungarian.cpp` — it
enumerates every permutation of `[0, n)` directly via `std::next_permutation` and takes the
minimum by brute force, using its own PRNG (`Xorshift32`, seeded, deterministic) rather than
any shared random source. Bounded to `n <= 7` (5040 permutations, negligible runtime) since
brute force is factorial — an independent-oracle discipline, not a scalability test (that is
`assignment_bench.cpp`'s job, §16).

## 14. Multi-Emergency Integration Scenarios

`tests/integration/assignment_scenarios_test.cpp` — seven scenarios (A–G), each exercising
the full pipeline (real graph, real `TransferModel`, real search) against a distinct
situation:

| Scenario | What it tests | Verified outcome |
|---|---|---|
| A: Independent, easy | No competition; the obvious pairing is optimal | `Success`, total `0.9198` |
| B: Competition | One vehicle is cheaper at everything, but the optimal split still requires it to yield its best match | `Success`, total `1.2532` (vs `1.4758` if V1 kept its own best) |
| C: Capability restriction | One vehicle's budget lets it reach the near emergency but not the far one | `Success`, infeasible pair never selected |
| D: Deadline restriction | Infeasibility from `ConstraintKind::SurvivalWindow`, not delta-v | `Success`, the feasible vehicle serves the emergency |
| E: Priority under scarcity | One vehicle, two emergencies; the higher-priority one must be served | `PartiallyAssignable`; the Critical-severity emergency is served, not the cheaper Low-severity one — the regression test for §8's penalty-direction bug |
| F: Greedy failure, real data | §11's scenario | `Success` for both solvers; Hungarian `1.167632` < greedy `1.570000` |
| G: No feasible global solution | Every vehicle starved of delta-v | `Infeasible`, zero assignments, no fabricated solution |

Scenario D's result is worth stating honestly: the "close" vehicle turned out to be the
*infeasible* one (blocked by phase misalignment, not raw altitude difference), while the
"far" vehicle happened to have a compatible phase and reached for free. This is a genuine
finding about orbital mechanics — deadline feasibility depends on phasing as much as
distance — not a scenario adjusted after the fact to fit a preconceived narrative; the test
and this table describe what the real search actually returned.

## 15. Pareto-Integration Correctness

`tests/integration/pareto_assignment_integration_test.cpp` — checks specific to the
handoff between Phase 5's search and Phase 6's assignment layer:

- Every `Assignment.missionPlan` genuinely belongs to the `(vehicle, emergency, plan)`
  triple the `Assignment` claims (`missionPlan.vehicle()`, `.emergency()`, `.id()` all
  checked against the `Assignment`'s own fields), across a 3-vehicle x 3-emergency scenario
  with a deliberately mixed feasible/infeasible matrix.
- Every `PlanId` generated across an entire planning pass is globally unique — no collision
  between different `(vehicle, emergency)` pairs' fronts.
- An infeasible `(vehicle, emergency)` pair (per `AssignmentMatrix::feasibleReal`) never
  appears in either solver's `AssignmentResult::assignments`, checked for both Hungarian and
  greedy on the same mixed-feasibility matrix.
- §5's weight-change regression: re-scoring the *same* fronts under `balanced()` and
  `fastest()` leaves the front data unchanged while producing genuinely different costs, and
  can change the final Hungarian assignment.

## 16. Complexity and Benchmarks

| Stage | Complexity | Measured (`assignment_bench.cpp`) |
|---|---|---|
| Candidate generation | `O(V*E)` independent Pareto searches, each `O(graph size)` | Dominant stage at every size measured — `10x10`: ~0.4–0.7ms |
| Scoring | `O(V*E)` — reads existing fronts only | Consistently the cheapest stage — `10x10`: ~0.02–0.03ms |
| Cost + matrix construction | `O(V*E)` for costs, `O((V+E)^2)` for the padded matrix | `10x10`: ~0.03ms |
| Hungarian solve | `O((V+E)^3)` | `10x10`: ~0.01–0.02ms; `n=100` raw: ~0.15–0.37ms |

**The honest answer to "is Hungarian the bottleneck": no, not anywhere measured.**
Candidate generation dominates by a 5–40x margin across every scenario size tested (`2x2`
through `10x10`), because it runs a full independent Pareto search per pair while Hungarian
solves one small matrix once. Full protocol, tool description, and the reasoning behind it:
`BENCHMARK_PLAN.md` §13. Raw results: `benchmarks/phase6_assignment_results.txt`.

## 17. Mathematical Correctness Review

**17.1 Hungarian correctness.** §12's LP-duality argument, verified against known answers
and an independent brute-force oracle across 72+ randomised cases (§13). No arithmetic
produces `NaN` or `inf` even with multiple `kInfeasibleAssignmentCost`-scale sentinels in
one matrix (`hungarian_test.cpp`'s `NoArithmeticProducesNaNOrInfEvenWithMultipleSentinels`).

**17.2 The priority-penalty direction bug — found via honest scenario construction, not the
checklist.** While building Scenario E (§14) to demonstrate that priority correctly steers
assignment under scarcity, the *first* run of the real pipeline showed the opposite of the
intended behaviour: the sole vehicle served the Low-severity emergency and left the
Critical-severity one unassigned. Tracing the cause led to `AssignmentMatrix.cpp`'s
`unassignedPenalty()`, which used `(2.0 - priority)` — the same expression as the real-pair
cost multiplier (§7), which correctly *decreases* with priority to make high-priority pairs
cheaper to serve. Applied to a *penalty* rather than a *cost*, the same direction is
backwards: it made abandoning a high-priority emergency **cheaper** than abandoning a
low-priority one. Fixed to `(1.0 + priority)`, the mirror image, increasing with priority
(§8). This is exactly the class of bug Phase 6 brief §31's adversarial self-review exists to
catch, found instead by constructing a real scenario and refusing to accept a result that
looked wrong — the same discipline this project has applied since Phase 4.

**17.3 Dummy-row/column correctness.** Every dummy row (unassigned-emergency escape) and
every idle column (vehicle-idle escape) is priced consistently across all `M`/`N` dummy
rows/columns for a given real column/row (`assignment_matrix_test.cpp`'s
`DummyRowPenaltyMatchesDocumentedFormulaAndIsIdenticalAcrossRows`) — which specific dummy
slot is used carries no meaning, only whether a real row/column lands in that block at all.

**17.4 Infeasibility never silently accepted.** `kInfeasibleAssignmentCost` dwarfs both
escape hatches by 7-8 orders of magnitude; §10's post-solve validation independently
confirms no infeasible real pair is ever reported, on every solver, on every test scenario
run.

**17.5 Assignment-cost comparability.** §7's central fix, verified directly (not merely
argued) by `FrontRelativeScoresCollideButGlobalAssignmentCostDoesNot`.

**17.7 Parallel-array ordering — three gaps found and closed during self-review.** Every
function in this layer threads `(vehicle, emergency)` identity through several parallel
arrays (`entries`, `vehicles`/`emergencies`, `fronts`, `scored`) indexed by the *same*
`row*emergencyCount+col` arithmetic, rather than by a self-describing key. Nothing in the
type system enforces that a caller passes these in matching order. Three real instances
were found by asking "what happens if a caller innocently reorders or narrows one of these
lists" rather than by any checklist item:

1. `buildAssignmentMatrix()` did not check that `entries`' implied `(vehicle, emergency)`
   order matched the separately-passed `vehicles`/`emergencies` lists — a reordering would
   have silently mislabelled every real assignment without any error.
2. `solveAssignmentHungarian()`/`solveAssignmentGreedy()` did not check that `fronts`/
   `scored` had the size `buildAssignmentMatrix()`'s `matrix` was actually built from — a
   mismatched or truncated pair would have either mislabelled assignments or read out of
   bounds.
3. `buildAssignmentCosts()` looked up each candidate's emergency priority via
   `std::unordered_map::at()`, which throws `std::out_of_range` if `scored` references an
   emergency the separately-passed `emergencies` list omits — a different exception type
   than this codebase's uniform caller-error signal (`Validation.hpp`'s `requireThat`,
   `std::invalid_argument`), meaning `CHECK_THROWS` in tests, or any caller catching
   `std::invalid_argument` specifically, would not have recognised it as the same class of
   error.

All three are now explicit `requireThat` checks (`AssignmentMatrix.cpp`,
`AssignmentResult.cpp`, `AssignmentCost.cpp`), each with a regression test
(`assignment_matrix_test.cpp`'s `RejectsVehicleOrEmergencyOrderMismatchedWithEntries`,
`pareto_assignment_integration_test.cpp`'s
`SolversRejectFrontsScoredThatDoNotMatchMatrixShape`,
`assignment_cost_test.cpp`'s `RejectsEmergenciesListThatDoesNotCoverScoredCandidates`).

**17.6 Pareto-integration sufficiency — a deliberate, stated scope boundary, not a hidden
gap.** Phase 6 selects exactly **one** recommended plan per `(vehicle, emergency)` pair
(`MissionScorer::selectRecommendedPlan()`, unchanged from Phase 5) before that pair ever
enters the cost matrix. This means the assignment layer optimises over "the best single
route for each pairing," not over the full joint space of "every Pareto-optimal route for
every pairing, chosen jointly with the pairing itself." A fuller joint optimisation — e.g.
where a vehicle's *second*-best route to emergency A frees up enough delta-v to make a
*better* global assignment possible than any combination of each pair's single best route
— is not explored, and is not claimed to be. This is a real limitation of Phase 6's scope,
stated here rather than discovered later: for the fleet sizes and mission-quality
differentials tested, no case surfaced where this mattered, but that is an absence of
evidence at this scale, not evidence of absence at a larger one.

## 18. Limitations

- **§17.6's one-plan-per-pair simplification**, restated: the assignment layer never
  reconsiders a pairing's *route* jointly with the *pairing* itself.
- **Diagnosis simplification (§4)**: `ConstraintKind::Unreachable` does not distinguish
  "genuinely unreachable under any combination of relaxed constraints" from "both
  constraints are simultaneously binding but relaxing both together would succeed" — an
  honestly bounded, not exhaustive, diagnosis.
- **No automatic dual-solver / regret reporting** (`OPTIMIZATION_MODEL.md` §8.2's
  correction): Hungarian and greedy are independently callable, not run together by
  production code; a `regret` display is Phase 7+ scope if wanted.
- **Rectangular scaling untested beyond `10x10`** at the integrated-pipeline level
  (`BENCHMARK_PLAN.md` §13.3) — raw Hungarian is measured to `100x100`, but the full
  candidate-generation pipeline is not, since candidate generation's cost is dominated by
  the underlying Pareto search's own scaling behaviour, already characterised in
  `PARETO_SEARCH.md` §14-15.
- **Synthetic benchmark graphs are fully cross-connected** — a real scenario's
  `GraphBuilder.hpp` feasibility pruning may eliminate many pairs before search runs at
  all, which `assignment_bench.cpp`'s generation does not model (also noted in
  `BENCHMARK_PLAN.md` §13.3).

## 19. Out of Scope (Deliberately)

Per the Phase 6 brief's explicit boundary: multi-vehicle cooperative rescue (one emergency
served by more than one vehicle jointly), dynamic replanning (re-solving as new emergencies
arrive mid-mission — **built since, as Phase 7; see `DYNAMIC_REPLANNING.md`**), simulation
(advancing time, consuming fuel, executing missions), and any frontend/backend/
visualization work. `Mission` (the committed, in-flight domain object, `domain/Mission.hpp`)
is untouched by Phase 6 itself — it produces `MissionPlan`s and `AssignmentResult`s, never
instantiates a `Mission`, which belongs to `MissionExecutor`/`DynamicReplanner` in a
later phase.

## 20. Summary of Files

| File | Role |
|---|---|
| `algorithms/Hungarian.{hpp,cpp}` | Domain-agnostic `O(n^3)` optimal assignment |
| `algorithms/GreedyAssignment.{hpp,cpp}` | Domain-agnostic rectangular greedy baseline |
| `optimization/EmergencyPriority.{hpp,cpp}` | Which casualty to serve |
| `optimization/MissionCandidate.hpp` | `CandidateFront` / `ScoredCandidate` — the search/score split |
| `optimization/CandidateGeneration.{hpp,cpp}` | Runs Pareto search once per pair; entity-node index; infeasibility diagnosis |
| `optimization/AssignmentCost.{hpp,cpp}` | Global (not front-relative) cost normalisation |
| `optimization/AssignmentMatrix.{hpp,cpp}` | `(N+M) x (N+M)` block padding |
| `optimization/AssignmentResult.{hpp,cpp}` | Solves, validates, materialises final missions |
| `configs/priority.json` | Emergency-priority weights and score tables |
| `tests/algorithms/{hungarian,hungarian_oracle,greedy_assignment}_test.cpp` | 32 tests |
| `tests/optimization/{emergency_priority,assignment_cost,assignment_matrix}_test.cpp` | 29 tests |
| `tests/integration/{assignment_scenarios,pareto_assignment_integration}_test.cpp` | 13 tests |
| `benchmarks/assignment_bench.cpp`, `scripts/run_assignment_benchmark.sh` | Hungarian scaling + integrated pipeline breakdown |
