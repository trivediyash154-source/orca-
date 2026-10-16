# ORCA — Pareto Search

Version 1.0 · Phase 5 · 2026-09-03

How ORCA finds not just *a* route but *every genuinely useful trade-off*, and enforces the
cumulative delta-v budget Dijkstra and A* documented as a limitation rather than fixed.

---

## 1. Problem

Find every Pareto-optimal (non-dominated) route from a source to a target over ORCA's four
simultaneously-minimised objectives — time, fuel, risk, distance
(`core/ObjectiveVector.hpp`) — under a time-dependent transfer model and a hard, cumulative
delta-v budget. This is the Multi-Objective Shortest Path Problem (MOSP), solved here via
**Martins' label-setting algorithm** with dominance pruning — a named, published algorithm,
not an ad-hoc invention.

## 2. Why Single-Label Search Is Not Enough

Dijkstra (`Dijkstra.hpp`) and A* (`AStar.hpp`) each track one best value per node. That is
correct and sufficient for a single objective, but ORCA's actual question is rarely "what
is the single cheapest route" — it is "what are the genuinely different, all-defensible
strategies, and which fits the mission's current priorities." A route that is slower but
burns less fuel is not worse than a faster, thirstier one; it is **incomparable**, and a
single-label search structurally cannot represent that — it would have to pick one and
silently discard the other's information.

**The concrete limitation this fixes.** Both `Dijkstra.hpp` and `AStar.hpp` document, and
test, that they enforce only *per-edge* delta-v feasibility, never a *cumulative* path-wide
budget — because a single-label search has already discarded the accumulated-cost
information a sound cumulative check needs by the time it would need it. Multi-label search
is exactly the structure that fixes this: every label carries its own accumulated cost, so
a candidate whose cumulative delta-v would exceed budget can be rejected outright,
regardless of how attractive it looks on any other objective. §9 has the mechanism;
`pareto_search_test.cpp`'s `CumulativeDeltaVHardConstraintRejectsWhatDijkstraWronglyAccepts`
demonstrates it directly against the same scenario Dijkstra/A* document as unfixed.

## 3. Multi-Objective Formulation

```
minimise  (time, fuel, risk, distance)  simultaneously
subject to:
  - per-edge feasibility (TransferModel's Group 1-3 checks, GRAPH_MODEL.md §3)
  - cumulative delta-v <= vehicle's total budget (§9 -- new in Phase 5)
```

A solution `A` **dominates** solution `B` if `A` is no worse in every objective and
strictly better in at least one (`Dominance.hpp`, §6). Because dominance is a **partial**
order, not a total one, there is in general no unique minimum — hence a *set* of solutions,
the Pareto front, rather than one answer.

## 4. Objective Vector

Reused verbatim from `core/ObjectiveVector.hpp` (Phase 1/2) — not reintroduced. All four
fields are minimised; the type deliberately has no `operator<`, because no single natural
total order exists (the file's own comment: "this is not a stylistic preference — it keeps
the dominance predicate free of per-objective knowledge"). `compose()`'s risk rule
(independent-survival: `1-(1-a)(1-b)`) and its documented monotonicity property are what
make dominance pruning sound (§12).

## 5. Label Definition

```cpp
struct ParetoLabel {
    NodeId node;
    ObjectiveVector cost;                     // accumulated via compose()
    std::optional<LabelId> predecessor;       // flat-pool index, not a pointer
    std::optional<EdgeId> viaEdge;
    std::optional<TransferResult> legResult;  // the ACTUAL evaluation, not the edge cache
    Seconds arrivalTimeSec;                   // absolute time -- next edge's departure time
    bool active;                              // lazy-deletion flag
};
```

(`algorithms/ParetoLabel.hpp`.) Labels live in one flat `std::vector`, addressed by
`LabelId` — an index, never a pointer, so predecessor chains survive the pool
reallocating as it grows (Phase 5 brief §64; the same reasoning `NodeId`/`EdgeId` already
follow).

**`cost.time` vs `arrivalTimeSec`, named explicitly (Phase 5 brief §14):** `cost.time` is
*elapsed* time since the search began (an objective to minimise); `arrivalTimeSec` is
*absolute* simulation time (what the next edge's departure must use). For a search starting
at `t=0` these are numerically equal, but they are not the same field for a different
reason, and both are kept: `cost.time` composes via `ObjectiveVector::compose()` alongside
fuel/risk/distance, participating in dominance; `arrivalTimeSec` never participates in
dominance directly (`ObjectiveVector` has no time-of-day field) and exists purely to drive
the next `TransferModel::evaluate()` call correctly.

**Why `legResult` is stored, not looked up from the edge:** the same reason `PathLeg`
(`PathResult.hpp`) stores it — a time-dependent edge's cost depends on when it is used, so
the edge's cached `referenceResult` is only ever correct for one specific departure time.

**Why there is no separate `accumulatedDeltaVKms` field**, despite the Phase 5 brief's own
warning (§22) that fuel-fraction and delta-v-budget "are not necessarily identical":
checked directly against `TransferModel.cpp`, `fuelCost = deltaVKms / constraints
.deltaVBudgetKms` per leg, and the budget is the same constant (the searching vehicle's
total capability) for every relaxation in one search. Since `compose()` adds `fuel`
additively, `label.cost.fuel` after `n` legs already equals `(sum of every leg's deltaVKms)
/ budget` — the accumulated-delta-v-over-budget ratio the cumulative constraint needs,
exactly, with no redundant field. This was verified against the real formula, not assumed
— see `ParetoLabel.hpp`'s file comment for the full derivation.

## 6. Dominance

```
A dominates B  <=>  (for all i: A[i] <= B[i])  and  (exists j: A[j] < B[j])
```

Implemented in `algorithms/Dominance.hpp`/`.cpp` — **not** `optimization/`, despite the
Phase 5 brief's own illustrative directory sketch putting it there. `ParetoSearch.cpp`
calls `dominates()` at every label insertion, and `ARCHITECTURE.md`'s dependency table has
`optimization/` depend on `algorithms/`, never the reverse; putting `Dominance` in
`optimization/` would recreate the exact cycle `ObjectiveVector` was moved out of
`optimization/` to avoid in Phase 1. `optimization/`'s scoring code reaches down into
`algorithms/` for it, the same direction it already reaches for everything else.

**Two epsilons, never confused.** `dominates()` uses a fixed, small, non-configurable
floating-point noise tolerance (`kDominanceNoiseEpsilon = 1e-6`, the same role and
magnitude Dijkstra/A*'s relaxation epsilon plays). `dominatesWithEpsilon()` is a *separate*
function for controlled approximation (ε-dominance, §14), requiring the caller to pass a
real epsilon every time, with no default — so approximate pruning can never be reached by
accident. See `Dominance.hpp`'s file comment for the full reasoning, including why
`dominatesWithEpsilon` is deliberately kept irreflexive for any epsilon rather than using
the (also valid, but inconsistent with the rest of this file's property tests) reflexive
definition of ε-dominance found in some literature.

**Properties — property-tested, not assumed** (`dominance_test.cpp`): irreflexive,
asymmetric, transitive, partial (not total — the entire justification for returning a set).

## 7. Duplicate Handling

Phase 5 brief §9's distinction, honoured precisely: if `A == B` (within noise tolerance),
**neither dominates the other**, but they are not a useful trade-off either — they are the
same point. `isEquivalent()` (`Dominance.hpp`) is a separate, explicitly named predicate
from dominance, checked at label-insertion time (§8) so that duplicate routes do not
silently double the frontier. The deterministic policy: the *first-inserted* equivalent
label wins; a later-arriving duplicate is discarded outright. Verified directly
(`pareto_search_test.cpp`'s `DuplicateRoutesHandledDeterministicallyNotDoubled`, and the
oracle test's set-based, not count-based, comparison — see §13).

## 8. Search Process

```
FIND_PARETO_ROUTES(G, source, target, t_start, constraints):
    pool <- [root label at source, cost=0, arrivalTime=t_start]
    labelsAtNode[source] <- {root}
    open <- min-heap keyed on arrivalTime; push root

    while open not empty:
        L <- open.pop()
        if not L.active: continue          # lazy deletion, not reopening -- see §11
        if L.node == target: continue      # never expand outgoing edges of target
        for each edge (L.node -> v):
            result <- transferModel.evaluate(orbit(L.node), orbit(v), L.arrivalTime, ...)
            if not result.feasible: continue
            candidateCost <- compose(L.cost, result.objectives())
            if candidateCost.fuel > 1.0: continue    # THE CUMULATIVE HARD CONSTRAINT (§9)
            outcome <- insertCandidate(candidateCost, v, ...)   # dominance update, §8.1
            if outcome == Inserted: open.push(new label)

    return { reconstruct(L) : L in labelsAtNode[target], L.active }
```

(`ParetoSearch.cpp`; the pseudocode above is a condensed version of the real
implementation, not the reverse.)

### 8.1 The dominance update, precisely

For each candidate `C` arriving at node `v`, against `v`'s existing non-dominated labels:

1. If any existing label dominates `C`: discard `C`.
2. Else if `C` is equivalent to an existing label: discard `C` (§7's policy).
3. Else: remove every existing label `C` dominates.
4. Insert `C`.

`insertCandidate()` (`ParetoSearch.cpp`) implements exactly this, in two passes over `v`'s
label list — matching, not reinventing, the structure `ALGORITHMS.md` §5.4's original
sketch outlined, but corrected where that sketch was internally inconsistent (it used
`dominates_ε` for the "should I add this candidate" check and plain `dominates` for the
"should I remove existing labels" check — an asymmetry that would make approximate and
exact mode disagree about which labels survive. The real implementation uses the *same*
mode, exact or epsilon, consistently for both checks within one search).

### 8.2 Search priority is not what determines the front

The open queue orders by earliest arrival time (the same simplest-correct policy
Dijkstra/A* use) — this is a **processing order only**, deciding which retained label is
expanded next. It has no bearing on which labels survive; dominance (§6) alone decides
that (Phase 5 brief §57).

## 9. Hard Constraints, and Why Cumulative Delta-V Is One

`GRAPH_MODEL.md`'s three-way feasibility split (Group 1: permanent, decided at graph-build
time; Group 2: vehicle-dependent per-edge; Group 3: genuinely time-dependent per-relaxation)
already treats delta-v, survival window and risk ceiling as **hard rejections, never score
penalties** — `OPTIMIZATION_MODEL.md` §9's reasoning applies unchanged: a penalty would let
a large enough time advantage make an over-budget route outrank a feasible one, recommending
a mission that cannot physically fly.

The **cumulative** delta-v budget is the same kind of hard constraint, enforced the same
way, just at a different point: Group 1-3 can only ever see one edge at a time, so they
cannot catch a violation that only appears after several legs. `ParetoSearch.cpp` checks
`candidateCost.fuel > 1.0` (§5's derivation: exactly the accumulated-delta-v-over-budget
ratio) at every label extension, **before** the candidate is even considered for the
dominance update — an infeasible label can never enter the frontier, and can never be used
to dominate a feasible one (Phase 5 brief §21).

## 10. Time Dependence

**The critical rule, unchanged from Dijkstra/A*:** every edge is evaluated at the label's
actual arrival time, never the search's global start time (`GRAPH_MODEL.md` §6.1). Verified
directly, the same way: `pareto_search_test.cpp`'s
`IndirectLegUsesLabelArrivalTimeNotGlobalStart` compares the actual leg against two
independent, freshly-called `evaluate()` invocations, exactly as the Dijkstra and A*
versions of this test do.

## 11. Label-Setting, Not Label-Correcting — And Why A*'s Reopening Is Not Reused

This is **label-setting**: once a label is popped and expanded, it is never revisited. That
claim needs its own justification, and it is *not* A*'s (Phase 5 brief §16's explicit
warning). A*'s reopening exists because its heuristic is admissible but not unconditionally
consistent, so a node's first-popped value is not always trustworthy. This search uses
**no heuristic at all** (§16) — multi-objective admissibility is a different, harder
problem than single-objective admissibility (`ALGORITHMS.md` §4.3, `HEURISTIC_DESIGN.md`),
and building one is explicitly out of Phase 5's scope.

Instead, correctness here rests on **monotonicity** (§12): every objective is non-decreasing
along any path, so once a label is dominated, no future extension of a competing label can
un-dominate it. `ParetoSearch.cpp`'s `active` flag exists only to skip labels superseded
*while still sitting in the open queue* (the multi-label analogue of Dijkstra's lazy
deletion), not to permit re-expansion of an already-processed label.

## 12. Termination

**This is not Phase 4's FIFO-based, single-label argument, reused** (Phase 5 brief §49
requires this be established fresh, and it is a genuinely different argument).

**Monotonicity.** Every edge composes non-negative time/fuel/distance additively and risk
via the independent-survival rule, which is bounded in `[0,1]` and non-decreasing
(`core/ObjectiveVector.hpp`'s `compose()` doc comment — proven, not assumed, in Phase 1/2).
So extending any label can only make its cost componentwise no better.

**Non-trivial cycles self-dominate.** Consider a label `L` at node `n`, and the label `L'`
produced by extending `L` around any cycle back to `n` with at least one edge of strictly
positive cost in some dimension. By monotonicity, `L'.cost` is componentwise >= `L.cost`,
strictly worse in at least one dimension — so `L` dominates `L'` outright. Since `L` is
already present at `n` by the time `L'` is generated (an edge relaxation can only happen
*after* its source label has been popped and expanded), `L'` is caught and discarded by
dominance-update Step 1 (§8.1), regardless of what else exists at `n`. No label can usefully
loop through a cycle with any real cost.

**Zero-cost cycles are caught by duplicate handling, not a special case.** If every edge in
a cycle costs exactly `(0,0,0,0)` (physically possible — e.g. two graph nodes at the exact
same orbit, `TransferEdge.SameOrbitSamePhaseIsFreeAndInstant`), `L'` is *equivalent* to `L`,
not dominated, and is caught instead by Step 2. Either way, no label surviving a full loop
back to a node it already occupies ever adds new information.

**Consequence.** Since the graph is finite and no cycle can ever produce a genuinely new,
surviving label at a node it has already reached, the total number of labels that ever
survive insertion is bounded (though potentially large — MOSP is NP-hard, §13) by a finite
function of the graph's structure. The search therefore terminates. Verified directly, not
just argued: `pareto_search_test.cpp`'s `CyclicGraphTerminatesAndAgreesWithDijkstraOnArrival
Time` runs this exact search on a real graph containing a real cycle
(`dijkstra_oracle_test.cpp`'s 7-node fixture) and confirms both termination and agreement
with independently-verified Dijkstra.

## 13. Independent Verification

`pareto_search_oracle_test.cpp` — a brute-force multi-objective oracle sharing **nothing**
with the production code it checks: it does not call `dominates()`/`isEquivalent()`, does
not call `findParetoRoutes()`, and does not use `ParetoFront`. Dominance is reimplemented
from scratch, with its own epsilon constant, in the test file — reusing the production
predicate to verify the production search would let one shared bug validate itself
(the same discipline `dijkstra_oracle_test.cpp` and `heuristic_admissibility_test.cpp`
already establish for the single-objective searches).

The oracle enumerates every simple path, evaluates it through the real `TransferModel`,
applies the same per-edge and cumulative-delta-v feasibility rules production enforces,
and extracts the non-dominated set with its own dominance check. **Comparison is by
distinct cost vector, not raw count**: a naive oracle retains every equal-cost duplicate
path (equal vectors never dominate each other), while production search deliberately keeps
one representative per duplicate group (§7) — both are legitimate policies, so agreement is
checked at the level where they actually must agree.

Checked against: two genuine trade-off scenarios (hand-inspected), a directed graph with a
real cycle, an unreachable target, the cumulative-delta-v-rejection scenario, and five
structurally distinct randomised small graphs (chain, diamond, star, back-edge cycle,
wider fan-in) — chosen the same way `dijkstra_oracle_test.cpp`'s cases are, not generated
from a single template. All agree.

## 14. Complexity

| | |
|---|---|
| Time | `O(\|L\| · log\|L\| · (k + deg))`, where `\|L\|` = labels ever inserted |
| Space | `O(\|L\| · k)` |
| Worst case | `\|L\|` exponential in `V` — MOSP is NP-hard; not claimed otherwise |
| Practice | Bounded by dominance pruning (exact, free) and optional ε-dominance (approximate) |

**Measured, not asserted**: `benchmarks/pareto_search_bench.cpp`,
`benchmarks/phase5_pareto_search_results.txt`. On four deterministic "fan" scenarios
(10-32 nodes, chosen for genuine multi-way trade-offs — see §17), dominance comparisons
grew from 72 to 2401 as the label-inserted count grew from 19 to 154 — comparisons growing
faster than labels is expected, since each insertion is checked against every existing
label at that node (§8.1, `O(n)` per insertion). No scenario in this benchmark approached
label counts large enough to need `maxLabels` truncation.

## 15. Label Explosion

Deliberately not addressed by aggressive pruning as a default — §16's exact baseline comes
first. Two documented, measured mitigations exist for when it matters:

- **`maxLabels` cap** (`ParetoSearchOptions`): a hard bound on labels ever inserted.
  Truncation is recorded (`ParetoSearchMetrics::truncated`), never silent; a truncated
  result must never be presented as a complete front.
- **ε-dominance** (§16): approximate, opt-in, and its effect is measured, not assumed —
  on the benchmark's richest scenario (19-route exact front), `epsilon=0.02` collapsed it
  to 3 routes and cut dominance comparisons by 95% (2401 -> 117); `epsilon=0.20` collapsed
  it to 1 route.

Neither is active by default. Every correctness test in this codebase runs in exact mode.

## 16. Approximation: ε-Dominance

`ParetoSearchOptions::epsilonDominance` (`std::nullopt` by default — exact mode). When set,
every dominance check in the search (§8.1) uses `dominatesWithEpsilon()` (§6) instead of
`dominates()`, consistently for both the "discard candidate" and "remove dominated
existing" steps (the asymmetry `ALGORITHMS.md` §5.4's original sketch had — §8.1 — is not
present here). This is a controlled approximation: it may discard a genuinely non-dominated
label whose cost differs from a survivor's by less than the configured tolerance, in
exchange for a smaller, faster-to-compute front. Measured effect: §14, §15.

## 17. Limitations

- **No heuristic guidance.** Every label is explored via arrival-time ordering only, with
  no multi-objective admissible lower bound (§11). A future phase could add one, but it is
  a genuinely harder problem than A*'s single-objective case and was not attempted here.
- **Natural three-or-more-way trade-offs from real orbital parameters proved harder to
  construct than expected.** A single backbone or small candidate set overwhelmingly
  produces one or two-way trade-offs, or a single dominant route — most orbital-parameter
  combinations correlate across objectives (a bigger manoeuvre costs more time, fuel *and*
  risk together) rather than trading them off. The permanent tests' multi-way scenarios
  (`pareto_search_test.cpp`) came from a genuine, documented search over a wide parameter
  grid, not a single hand-picked example. This is a property of ORCA's transfer model and
  realistic altitude/inclination ranges, not a search bug — stated here so a future reader
  does not assume trade-offs are as easy to find as the textbook picture suggests.
- **`maxLabels` truncation, when it triggers, is a real incompleteness**, not merely a
  performance knob — a truncated front is not the true Pareto front, and callers must check
  `metrics.truncated` rather than assume completeness.
- **ε-dominance's discarded labels are not recoverable** — there is no "show me what
  epsilon hid" mode in this phase; a caller wanting both exact and approximate results must
  run the search twice.
- Objective composition (§4) inherits every property and limitation `ObjectiveVector`
  already has (documented in `core/ObjectiveVector.hpp` and `TRANSFER_MODEL.md`) — nothing
  new is introduced or assumed about time/fuel/risk/distance's units or composition rules
  here.

## 18. Future Heuristics

Not built in Phase 5, and not needed by anything Phase 5 requires — listed so a future
phase does not have to rediscover the option space:

- **A multi-objective admissible bound**, if a future benchmark shows label-setting's
  unguided exploration is a real bottleneck on realistic scenario sizes. `HEURISTIC_DESIGN
  .md` §10 already flags this as deferred rather than designed; the single-objective A*
  heuristic there does not transfer directly (bounding one objective's remaining cost does
  not bound a *vector* remaining cost in the sense dominance needs).
- **A tighter ε-dominance policy** (e.g. per-objective epsilon rather than one shared
  value), if the current all-dimensions-equal tolerance proves too coarse for a specific
  objective's natural scale.
- **Recoverable ε-dominance** (retaining discarded-but-close labels in a secondary set),
  if a future UI wants to show "these were folded into the recommendation" rather than
  silently dropping them.
