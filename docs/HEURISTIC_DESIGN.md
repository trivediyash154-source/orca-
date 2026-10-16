# ORCA — A* Heuristic Design

Version 1.0 · Phase 4 · 2026-09-03

The full derivation, admissibility proof, consistency analysis, and independent
verification method for the lower bound `cpp/algorithms/AStarHeuristic.cpp` computes.
`ALGORITHMS.md` §4.3 has the condensed version inline with the rest of the algorithm
catalogue; this file is where the reasoning behind it is shown in full, including the
parts that turned out to be wrong on a first attempt and were corrected rather than
smoothed over.

---

## 1. Purpose

`findEarliestArrivalAStar` (`AStar.hpp`) solves the identical problem
`findEarliestArrival` (Dijkstra, `Dijkstra.hpp`) does — single-objective earliest arrival
time over a time-dependent `MissionGraph` — guided by a per-node estimate of how much
additional time remains to the target. That estimate is what this document is about.

**What it estimates, precisely:** a lower bound on the *additional* time to reach the
target, starting from a given node, under ORCA's own transfer model — not a real-world
aerospace guarantee, not a delta-v estimate, not an orbital period, not elapsed time since
the search began. "True" means the minimum remaining cost under the *same* model being
searched, nothing more.

**What admissibility buys, and why it is the only property load-bearing here.** A*
guided by an admissible heuristic (one that never overestimates) and using reopening
(§6) returns a provably optimal path — proven in full in `AStar.hpp`'s termination
section, not merely asserted. Consistency (§5) is a separate, stronger property that
would let the search skip reopening entirely; ORCA's heuristic does not have it
unconditionally, so ORCA does not rely on it, and the search's correctness does not
depend on the sampled, non-airtight consistency finding in §5 being any tighter than it
is.

---

## 2. Mathematical Definition

```
h(n) = min( direct(n), floorDetour(n) )

direct(n)      = coast(r(n), r(goal))
floorDetour(n) = coast(r(n), R_earth) + coast(R_earth, r(goal))

coast(ra, rb)  = 0                                    if |ra - rb| < TOLERANCE
               = π · sqrt( ((ra + rb) / 2)³ / μ )      otherwise
```

- `r(n)` — the semi-major axis (equivalently, radius — every ORCA orbit is circular) of
  node `n`'s orbit.
- `R_earth` — `kEarthRadiusKm` (`Constants.hpp`), the physical floor every ORCA orbit is
  constructed strictly above (`Orbit::fromRadius` rejects anything at or below it).
- `TOLERANCE` — `TransferModel::kSameRadiusToleranceKm` (`1.0e-3` km), the same tolerance
  the production transfer path uses to decide "close enough to skip the coast term
  entirely," reused here rather than re-invented so the heuristic's notion of "same
  radius" matches the thing it is bounding.
- `μ` — `kMuEarth` (`Constants.hpp`).

`coast(ra, rb)` is the Hohmann transfer half-period — departure burn to arrival burn —
with the same-radius case special-cased to exactly `0` (a pure phasing or plane-change
manoeuvre needs no coast at all; the *smooth* Hohmann formula does **not** naturally reduce
to `0` as `ra → rb` — it approaches half the circular orbital period at that radius
instead — so this special case is a genuine branch, not a redundant guard; §3.3 explains
why conflating the two caused a real derivation error).

`h` depends on exactly two numbers — the current node's radius and the goal's radius —
and nothing else. §4 explains why phase, inclination and departure time are absent, and
why that is a deliberate looseness, not an oversight.

---

## 3. Derivation

### 3.1 What Phase 0/3 assumed

`ALGORITHMS.md` §4.3 originally specified `h_time(n) = coast(r(n), r(goal))` — the direct
term alone — justified as: "no impulsive two-burn transfer between these radii is faster
than the Hohmann transfer." That statement is true, but it is a claim about a *single*
transfer, and admissibility needs a lower bound over every possible *route*, including
multi-hop ones. Those are not the same claim, and Phase 4 is where the gap between them
was found.

### 3.2 Why a multi-hop route can beat the direct hop

`coast(ra, rb)` depends on the *average* of the two radii, `(ra+rb)/2`, not their
difference. A route that dips through a lower intermediate radius before continuing can,
in principle, have a smaller total coast time than the direct hop, because a leg's cost
only grows with the average of its endpoints — spending part of a journey at a much lower
radius can pull that leg's average down without proportionally penalising the other leg.
The direct-only heuristic has no defence against this: it simply does not consider that a
cheaper multi-hop route might exist.

### 3.3 How this was actually found, including a wrong turn

This was found by compiling and running the real `hohmann::compute()` against the direct-
only claim, not by re-reading Phase 0's algebra and spotting the gap abstractly. The first
numerical pass over realistic altitude combinations found *zero* violations, which briefly
looked like confirmation that the direct-only heuristic was fine after all.

A **hand derivative analysis** was attempted next, to understand *why* no violations
appeared, and it produced the opposite, incorrect conclusion: that detouring below the
start radius should beat the direct hop for essentially any `(r0, r2)` pair. Re-verifying
that conclusion numerically found it was wrong — the analysis had conflated the *smooth*
Hohmann formula (which approaches a large positive value even as `ra → rb`) with the
*special-cased* pure-plane-change value (exactly `0` when `ra == rb`) — comparing the
smooth curve's derivative at the wrong reference point, across a discontinuity the
derivative calculation did not account for.

Correcting that and re-deriving carefully (`verify_derivative.cpp`, a scratch program
compiled against the real transfer model, not retained in the repository once its finding
was captured here) found the **true threshold**: violations of the direct-only heuristic
begin only beyond roughly 1,000,000–10,000,000 km of target separation — around 28× to
280× geostationary altitude, utterly beyond anything `PRD.md` scopes ORCA to model. Within
that regime the violation is real but small (a few thousand seconds against totals in the
tens of millions).

**The conclusion drawn from this was not "the range is safe, so ship the direct-only
heuristic and document the caveat."** A heuristic whose correctness depends on a scope
caveat that nothing in the type system enforces is a latent bug waiting for whichever
future phase feeds it an input outside the caveat. §3.4 is the fix that removes the need
for the caveat entirely.

### 3.4 The fix: an unconditionally-safe second term

```
floorDetour(n) = coast(r(n), R_earth) + coast(R_earth, r(goal))
```

This is the cost of detouring through Earth's own surface radius. `coast(ra, x) +
coast(x, rb)`, as a function of the free via-radius `x`, is strictly increasing in `x`
(its derivative is a sum of two positive square-root terms) — so its infimum over every
physically valid `x` is approached at the smallest one, `R_earth`, and no real node can sit
below that floor (`Orbit::fromRadius` enforces it structurally). The "dip to a lower
radius to save cost" move §3.2 depends on is therefore structurally unavailable once the
reference point already *is* the lowest point anything can be: no real multi-hop route, of
any length, can produce a smaller total coast time than this term, because any such route's
lowest waypoint is bounded below by `R_earth`, and `floorDetour` already assumes the
cheapest possible waypoint at exactly that bound.

`h = min(direct, floorDetour)` inherits `floorDetour`'s unconditional admissibility
regardless of what `direct` does: taking a minimum with an always-safe value can only pull
the combined result down or leave it unchanged, never push it above the true cost. This is
why `h` is admissible for *every* input, not merely within a documented range — §4 makes
this precise.

---

## 4. Admissibility Proof

**Claim.** For any node `n` and target `goal`, `h(n) ≤` the true minimum remaining time
from `n` to `goal` under ORCA's transfer model, over every possible route of any length.

**Step 1 — remaining time reduces to remaining coast time.** The true remaining time along
any route is `Σ(wait_i + coast_i)`. Every `wait_i ≥ 0` (`PhasingModel.hpp` never produces
a negative wait — proven, not sampled, in `TRANSFER_MODEL.md` §13), so `true remaining
time ≥ Σ(coast_i)` unconditionally, for any route, regardless of phase, inclination or
departure time. A lower bound on `Σ(coast_i)` alone is therefore automatically a lower
bound on the true remaining time — this is *why* the heuristic can ignore phase and
departure time entirely and stay safe: it is not that phase does not matter, it is that
ignoring it can only loosen the bound, never invalidate it.

**Step 2 — `floorDetour` lower-bounds the coast time of every route.** By §3.4's
monotonicity argument, no multi-hop sequence of coast legs from `r(n)` to `r(goal)` can sum
to less than `coast(r(n), R_earth) + coast(R_earth, r(goal))`, because every intermediate
radius any real route can pass through is bounded below by `R_earth`, and the two-leg sum
is minimised exactly there.

**Step 3 — the minimum of an always-valid bound and anything else is still a valid
bound.** `h(n) = min(direct(n), floorDetour(n)) ≤ floorDetour(n) ≤` true remaining coast
time `≤` true remaining time (Step 1). This holds regardless of whether `direct(n)` itself
is ever an overestimate — the `min` structurally cannot let a bad `direct` value push the
combined result above the proven-safe `floorDetour` value.

**Independent numerical verification**, not just the analytic argument above:
`heuristic_admissibility_test.cpp` checks `h(n) ≤` real cost from three independent
angles, none of which call the heuristic's own code to produce the "true cost" side of the
comparison:

1. `AdmissibleAgainstDirectTransferAcrossRealisticGrid` — `h` against one real direct
   `TransferModel::evaluate()` transfer, swept across a 10×10 realistic-altitude grid.
2. **`AdmissibleAgainstBruteForceMultiHopGraphOracle`** — `h` against the true minimum
   over *every simple path* in real, multi-node graphs (an independently-written
   brute-force enumerator, the same kind `dijkstra_oracle_test.cpp` uses), including nodes
   placed deliberately near Earth's surface to stress `floorDetour` specifically. This is
   the check a direct-transfer-only sweep cannot perform, since a multi-hop detour beating
   a direct hop is precisely the failure mode §3.2 describes.
3. `FloorDetourMonotonicityLemma` — re-derives the §3.4 monotonicity claim itself by
   sweeping the real `hohmann::compute()` (never the heuristic's own `coastSec` helper)
   over the via-radius `x` and confirming the floor value is never beaten.

All three: zero violations observed.

---

## 5. Consistency

**Definition.** `h` is consistent if, for every edge `(n, n')` with real cost `c(n,n')`,
`h(n) ≤ c(n,n') + h(n')`. Consistency is strictly stronger than admissibility and, if it
held everywhere, would let A* skip reopening (§6) entirely.

### 5.1 What is proven, not sampled

`floorDetour` alone is consistent **unconditionally**, by the same "already at the floor"
argument as its admissibility proof: `floorDetour(n) = coast(r(n),R_earth) +
coast(R_earth,r(goal))`, and for any edge `(n,n')`, the triangle-style relation
`coast(r(n),R_earth) ≤ c(n,n') + coast(r(n'),R_earth)` holds because `coast(r(n),R_earth)`
is, itself, a lower bound on the coast cost of reaching radius `R_earth` from `n` — and
routing through `n'` first cannot do better than going directly, by the same monotonicity
argument §3.4 already establishes.

### 5.2 What is sampled, and a documentation error this phase found and corrected

`direct` alone is **not** consistent in general. This was checked directly rather than
assumed, and the check found real counterexamples inside ORCA's realistic altitude range:
with `n` and `goal` both near GEO and an edge to `n'` near low LEO, `direct(n)` can exceed
`c(n,n') + direct(n')` by several thousand seconds
(`heuristic_consistency_test.cpp`'s `BothComponentsIndividuallyAcrossRealisticRange`).

An earlier draft of this document and of `AStarHeuristic.hpp` §5 argued that the combined
heuristic's consistency followed from a general lemma: **the minimum of two individually
consistent heuristics is itself consistent.** That lemma is correct — proof: let
`h = min(h1,h2)`, and at successor `n'` let `h_i` be whichever of the two realises
`min(h1(n'), h2(n'))`; then `h(n) ≤ h_i(n)` (since `h` is a min) `≤ c(n,n') + h_i(n')`
(`h_i`'s own consistency) `= c(n,n') + h(n')` (by the choice of `i`) — but its
**precondition is both components individually consistent**, and §5.2's finding shows that
precondition is false here, since `direct` fails it. Citing the lemma as the reason the
combined heuristic is consistent was therefore a genuine error, not a simplification —
caught by writing a test that decomposed the combined check into its two components
separately, rather than only ever re-checking the already-combined value. It is corrected
here and in `AStarHeuristic.hpp` §5, rather than left for a future reader to rediscover.

### 5.3 What actually holds for the combined heuristic

`min(direct, floorDetour)` — the heuristic A* actually uses — has **zero observed
consistency violations** across:

- 5,000 samples across ORCA's realistic altitude range (150–40,000 km altitude),
  `SampledTriplesAcrossRealisticRangeSatisfyConsistency`.
- 5,000 samples across a far wider range (1–3,500,000 km altitude),
  `WideRangeFarBeyondRealisticScenariosStillSatisfiesConsistency`.
- **20,000 samples deliberately concentrated on the exact regime that breaks `direct`
  alone** — source and goal both near GEO (25,000–40,000 km altitude), intermediate near
  low LEO (150–3,000 km altitude) — `CombinedHeuristicSurvivesAdversarialNearGeoToLeoRegime`.
- An additional 400,000-sample adversarial scratch search (200,000 broad + 200,000
  concentrated on the same near-GEO/near-LEO shape) run once during Phase 4's exploration
  to decide whether the in-range sample sizes above were trustworthy before committing to
  them as permanent tests. Zero violations found.

**This is stated as an empirical, sampled property, not a proven one.** No compositional
argument from the two components is claimed — §5.2 shows the obvious one does not apply.
The evidence is a large, adversarially-targeted sample with zero counterexamples, which is
meaningfully strong evidence but is not a proof, and this document does not present it as
one.

### 5.4 Why none of this is load-bearing for correctness

A* here does not require consistency for optimality — it requires only admissibility
(§4), used together with **reopening** (§6), which is safe under admissibility alone. The
consistency finding in this section explains *why reopening essentially never triggers on
realistic ORCA scenarios in practice* (§6.2) — it is documentation of an operational
property, not a dependency the search's correctness proof relies on.

---

## 6. Reopening

### 6.1 What it is and why it is needed

Unlike Dijkstra's `finalised[]` array, `AStar.cpp` never marks a node permanently "done."
`bestG[]` plus a lazy-deletion comparison on pop is the only staleness check; if a node's
`bestG` improves after it has already been expanded once, it is expanded again. This is
necessary, not optional, precisely because §5 does not establish unconditional
consistency: under mere admissibility, a node's first-popped label is not guaranteed
optimal, so refusing to re-expand it could return a suboptimal path.

### 6.2 Why it essentially never triggers in practice, and why that is documented rather than assumed

Within ORCA's realistic range, the *combined* heuristic is (empirically, §5.3)
consistent, and consistency is exactly the condition under which reopening never actually
improves anything — so on realistic scenarios, reopening is dead code that happens to be
reachable, not a routinely-exercised path. Every other A* test in the suite (structural
correctness, time-dependence, cross-validation, benchmarks) uses the real heuristic and,
consistent with that, never exercises reopening.

That is exactly why `astar_time_dependence_test.cpp` has a dedicated test for it,
`ReopeningCorrectsWronglyExpandedNodeUnderInconsistentButAdmissibleHeuristic`, built with a
**synthetic** heuristic passed through `findEarliestArrivalAStar`'s override hook — real
enough to obey each node's own admissible ceiling (every `h` value used is `≤` that node's
real `trueRemaining`, confirmed against the real `TransferModel`), but deliberately
inconsistent across one specific edge, so that a genuinely worse route is expanded first,
and a genuinely better one is found and must correct it. The graph and every numeric
constant in that test were found by compiling and running this exact scenario — not
hand-derived — and the test asserts, via the search's optional trace, that the affected
node's `NodeId` appears at least twice with a strictly improving `g`, and that the final
answer still matches independent Dijkstra ground truth exactly.

### 6.3 Termination is still safe on the very first pop of the target

Reopening applies to intermediate nodes; the target itself is handled differently, and the
reason is proven in full in `AStar.hpp`'s "WHY THE FIRST GOAL POP IS STILL SAFE TO TRUST"
section (summarised in `ALGORITHMS.md` §4.5). The short version: admissibility alone
(no consistency needed) is enough to guarantee the target's `g` is already optimal the
first time it is popped, so `AStar.cpp` breaks out of the search loop immediately rather
than draining the queue further.

---

## 7. What This Heuristic Deliberately Ignores

- **Inclination.** Coast time does not depend on inclination in this model — only delta-v
  does (`TransferModel.hpp`) — so folding an inclination term into a *time* estimate would
  not lower-bound time at all, whatever else it might bound. `ALGORITHMS.md` §4.3's
  deferred `h_fuel` sketch is the right direction *if* a delta-v-objective search is ever
  built; it has no place in this one.
- **Phase.** Ignoring it is proven safe, not merely convenient (§4, Step 1): real wait is
  always `≥ 0`, so a bound that ignores wait can only be loosened, never invalidated, by
  doing so.
- **Departure time.** A direct consequence of ignoring phase: `coast(ra,rb)` does not
  depend on *when* the transfer happens, only on the two radii, so `h` is a pure function
  of two orbits and needs no time argument.

Verified directly, not merely asserted: `astar_heuristic_test.cpp`'s
`IgnoresInclinationEntirely` and `IgnoresPhaseIncludingNearWrapAround` construct orbits
that differ only in the ignored quantity and check `h` is bit-for-bit unaffected,
including right at the phase's `2π` wrap seam.

---

## 8. Numerical Tolerances

- **Same-radius special case:** `TransferModel::kSameRadiusToleranceKm = 1.0e-3` km,
  reused rather than duplicated (§2).
- **Admissibility/consistency violation threshold:** a slack of `-1e-6` seconds or worse
  is treated as a genuine violation in every test in this document; smaller negative
  slack is floating-point noise, consistent with the tolerance
  `TRANSFER_MODEL.md` §13.3 documents for independently-computed arrival times elsewhere
  in the project.
- **Reopening's lazy-deletion epsilon:** `1.0e-6` seconds (`AStar.cpp`'s
  `kRelaxationEpsilonSec`), identical to `Dijkstra.cpp`'s, for the same reason: floating-
  point noise on independently computed arrival times is a few ulp, not a genuine
  improvement or a genuine tie.

---

## 9. Known Weaknesses

- **The heuristic's informativeness degrades as a route's wait-time share grows.** It
  bounds coast time only, never wait, by design (§4, Step 1) — so scenarios where the
  optimal route accumulates substantial rendezvous waiting are exactly where the bound is
  loosest relative to the true cost. The Phase 4 benchmark measured this directly: the
  heuristic-quality ratio `h(source,target) / trueCost` fell from 0.167 to 0.003 as
  scenario size grew from 10 to 150 nodes (`benchmarks/phase4_dijkstra_vs_astar_results.txt`
  — `ALGORITHMS.md` §4.6 has the full reading).
- **`direct` is not consistent even within ORCA's realistic range (§5.2).** The combined
  heuristic empirically is, across a very large sample, but this is not a proof, and a
  scenario constructed specifically to violate it (rather than sampled) has not been ruled
  out.
- **Admissibility beyond ORCA's realistic range relies entirely on `floorDetour`.**
  `direct` alone can overestimate beyond roughly 1,000,000–10,000,000 km of separation
  (§3.3) — utterly outside `PRD.md`'s Earth-orbital rescue scope, but worth stating
  precisely rather than leaving as an implicit assumption: the combined heuristic remains
  admissible there because `floorDetour` alone already guarantees it (§4), not because
  `direct`'s misbehaviour has been bounded away.

---

## 10. Future Improvements

Not built in Phase 4, and not needed by anything Phase 4 requires — listed so a future
phase does not have to rediscover the option space:

- **A tighter multi-hop-aware term** that improves on `floorDetour` without losing its
  unconditional safety, if a future benchmark on a topology where reopening or heuristic
  looseness actually bites (§9) shows it matters in practice. Nothing in Phase 4's
  measurements demonstrates this is currently worth the complexity.
- **A delta-v (`h_fuel`) or risk-aware heuristic**, if a future phase builds a single-
  objective search over an objective other than time. Not relevant to Phase 4's scope
  (`ALGORITHMS.md` §4.1) and not a preparatory abstraction built ahead of that need.
- **A closed-form consistency proof for `direct`, or its replacement with a term that has
  one**, if the sampled evidence in §5.3 is ever judged insufficient for a use case that
  needs a guarantee rather than a very large sample.
