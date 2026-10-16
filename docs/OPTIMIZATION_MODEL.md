# ORCA — Optimization Model

Version 1.1 · Phase 0, implemented Phase 5 · 2026-09-03

How ORCA turns a mission graph into a recommendation — and why it does it in **two separate
stages** rather than one.

> **Phase 5.** The two-stage design this document specifies (§1) was implemented as
> written: search (`ParetoSearch.cpp`) produces a front knowing nothing of weights;
> `optimization/{ObjectiveNormalizer,MissionScorer,OptimizationProfile}` score and select
> from an already-computed front, exactly the separation `PARETO_SEARCH.md` §5's
> "architectural principle" restates for the search side specifically. §5's normalisation
> formula, §6's profile weights, and §10's tie-break order were all implemented to the
> letter, checked against this document by dedicated tests
> (`optimization_profile_test.cpp`'s preset tests, `mission_scorer_test.cpp`'s tie-break
> tests) rather than re-derived independently. §9's constraint table has one correction —
> see there.

---

## 1. The Central Design Decision

```
STAGE 1 — OBJECTIVE          STAGE 2 — PREFERENCE
Compute the Pareto set.      Choose one point from it.
No weights involved.         Weights involved.
Expensive: graph search.     Cheap: O(|front|) rescore.
Changes when the world       Changes when the operator
changes.                     changes their mind.
```

Almost every naive implementation collapses these into one step: pick weights, build a scalar
cost, run Dijkstra. ORCA deliberately does not, and this separation is the intellectual
content of the project.

### 1.1 Why not scalarise first

**Reason 1 — The weights are a guess made before the options are known.** A scalar score hides
the *exchange rate* between objectives. An operator who sets `w_time = 0.7` can never discover
that accepting a 4% time penalty would have bought a 40% fuel saving, because that trade was
collapsed away before they saw it. The Pareto front makes exactly those exchange rates visible.

**Reason 2 — Weighted sums cannot reach non-convex regions of the front.** This is a
mathematical limitation, not a tuning problem. A weighted sum `Σ wᵢ·xᵢ` selects the point
where a hyperplane of slope `−w` first touches the solution set. Points in a **concave dent**
of the front are never touched by any such hyperplane, for **any** non-negative weights:

```
fuel
  │  ● A
  │      
  │        ● B   ← in a concave dent: no weight vector selects B
  │              
  │            ● C
  └─────────────────── time
```

Sweeping every weight from 0 to 1 returns A and C and never B — even though B may be the best
compromise available. Label-setting finds B. **This alone justifies the whole approach.**

**Reason 3 — Feasibility gets corrupted.** With a scalar penalty for exceeding a fuel budget,
a sufficiently fast route can outrank a feasible one. ORCA treats feasibility as a hard
constraint applied at graph-build time (`SIMULATION_MODEL.md` §6), so an infeasible plan is
never a candidate at all.

**Reason 4 — Interactivity.** Because the front is computed once, moving a weight slider costs
`O(|front|)` — a rescore of a few dozen points, with no graph search. The Optimization Lab
feels instant *because* of the two-stage design, not despite it.

---

## 2. Objectives

Four, all minimised, defined in `DATA_MODEL.md` §1.

| Objective | Unit | Source | Composition |
|---|---|---|---|
| `time` | s | transfer + wait time | additive |
| `fuel` | fraction of the vehicle's own Δv budget | `Δv_edge / Δv_budget` | additive |
| `risk` | `[0,1]` | simulation risk model | `1 − Π(1 − rᵢ)` |
| `distance` | km | path length in ECI | additive |

### 2.1 Why these four, and why not more

**`time` and `fuel` are the primary tension**, and it is measured rather than assumed. The
synodic period between a 500 km and an 800 km orbit is **25.4 hours**
(`SIMULATION_MODEL.md` §4.5), while survival windows run tens of minutes. So the planner
constantly trades *"depart now at a poor phase and pay Δv"* against *"wait for a better phase
and pay time"*. Neither dominates; both matter.

**`risk` is genuinely independent** of both. The fastest route and the cheapest route can each
be the riskiest, depending on debris geometry, which is uncorrelated with orbital energy.

**`distance` earns its place by disagreeing with `fuel`.** This is the non-obvious one, and it
follows directly from the measured plane-change cost: 1° of inclination change costs 83% of a
300 km altitude raise (`SIMULATION_MODEL.md` §4.3). So two satellites that are *close in
kilometres but in different planes* are *expensive in Δv*. Keeping `distance` as a separate
objective exposes that discrepancy instead of hiding it — and gives the benchmark suite a
real effect to measure when comparing against a nearest-neighbour heuristic.

**Why not more than four.** Dominance weakens rapidly as `k` grows: with more objectives,
fewer pairs are comparable, so less is pruned and `|L|` explodes (`ALGORITHMS.md` §5.8). Four
is where the trade-offs are real and the search still terminates quickly. The architecture is
dimension-generic (`ARCHITECTURE.md` §15), so a fifth objective is a config change — but it
would be a measured decision, not a free one.

---

## 3. Pareto Dominance

```
A ≺ B  ⟺  (∀i: Aᵢ ≤ Bᵢ)  ∧  (∃j: Aⱼ < Bⱼ)
```

`A` dominates `B` when it is **no worse in every objective and strictly better in at least
one**. Implementation, properties and floating-point handling: `ALGORITHMS.md` §6.

**Dominance is a partial order, not a total one.** Some pairs are incomparable — neither
dominates. That is not a deficiency to be engineered away; it is the precise reason a *set* of
solutions must be returned rather than a single minimum. Every design choice in this document
follows from that fact.

---

## 4. The Pareto Front

The **non-dominated set**: solutions not dominated by any other.

```
     fuel
       │
   0.25│  ○ dominated
       │      ● P-0012  (fastest: 540s, 0.242 fuel)
   0.20│   ○         ○
       │
   0.15│           ● P-0007  ← BALANCED pick (708s, 0.136 fuel)
       │        ○
   0.10│                  ● P-0019  (safest/cheapest: 1025s, 0.098 fuel)
       │
       └────────────────────────────────── time
          500      700      900     1100

     ● non-dominated (the front)     ○ dominated
```

Reading the front is reading the exchange rate. Between P-0012 and P-0007: **+168 s buys
−44% fuel and −60% risk.** No scalar score would ever have shown that trade.

### 4.1 Ideal and nadir

```
ideal[i] = min over front of cost[i]      # best achievable per objective
nadir[i] = max over front of cost[i]      # worst on the front per objective
```

**Neither is generally an achievable plan.** The ideal point takes the best of each objective
independently, which no single route attains — if one did, it would dominate everything and
the front would have one member. They exist to define normalisation bounds (§5), and the UI
must never present them as options.

### 4.2 Dominated solutions are retained

`ParetoFront` keeps `dominated` alongside `solutions` (`DATA_MODEL.md` §7). This costs memory
and buys two things: a far more convincing Pareto chart, and the ability to answer "why wasn't
*that* one chosen?" with the specific dominating plan (`dominatedBy` in `API_CONTRACT.md` §4.4).

---

## 5. Normalisation

Objectives have incompatible units — seconds, fractions, `[0,1]`, kilometres. Weighting raw
values would let `distance` (thousands of km) swamp `risk` (`[0,1]`) purely through magnitude.

```
norm[i] = (cost[i] − ideal[i]) / (nadir[i] − ideal[i])        → [0,1]
if nadir[i] − ideal[i] < ε:  norm[i] = 0                       # objective is constant
```

### 5.1 Why normalise against the front, not against global bounds

Normalising against the **front's own** ideal/nadir means weights describe *relative position
within the actually-available options*. If every plan on the front takes between 700 s and
710 s, then time is effectively decided and normalisation correctly renders it near-irrelevant
— all plans score ~0 on it, and the weight has little effect.

Global bounds would instead make every plan score ~0.001 on time, and a large `w_time` would
have no visible effect while the operator wondered why the slider did nothing.

**The cost of this choice, stated plainly:** normalisation is *front-relative*, so scores are
**not comparable across different planning problems**. A `score = 0.3` for E-003 and a
`score = 0.3` for E-007 mean different things. ORCA therefore never compares raw scores across
emergencies — the assignment cost matrix (§8) is built from a single consistent scalarisation
per problem, and this is called out again there.

### 5.2 Degenerate case

When the front has one member, all objectives are constant, every `norm[i] = 0`, and that
member is selected regardless of weights. Correct, and handled explicitly rather than dividing
by zero.

---

## 6. Preference Profiles

### 6.1 Definitions

```json
{
  "FASTEST":     { "time": 0.70, "fuel": 0.10, "risk": 0.15, "distance": 0.05 },
  "FUEL_SAVING": { "time": 0.10, "fuel": 0.70, "risk": 0.15, "distance": 0.05 },
  "SAFETY_FIRST":{ "time": 0.15, "fuel": 0.10, "risk": 0.70, "distance": 0.05 },
  "BALANCED":    { "time": 0.30, "fuel": 0.30, "risk": 0.30, "distance": 0.10 },
  "CUSTOM":      { "time": 0.25, "fuel": 0.25, "risk": 0.25, "distance": 0.25 }
}
```

Data, in `configs/profiles.json` — adding a profile requires no code (`ARCHITECTURE.md` §15).

Note that even `FASTEST` retains `risk = 0.15`. A profile that zeroes an objective entirely
will select an extreme point that may be absurd on the ignored axis — the fastest route at any
risk. Keeping a floor makes the profiles behave like *preferences* rather than *lexicographic
orderings*. This is a modelling decision, and `CUSTOM` lets the operator override it.

### 6.2 Selection

```
score(plan) = Σᵢ wᵢ · norm[i](plan)
recommended = argmin over front of score
```

Weights are normalised to sum to 1 internally, so only *ratios* matter. All-zero or negative
weight vectors are rejected at the API boundary (`ARCHITECTURE.md` §11).

### 6.3 Complexity

`O(|front| · k)`. With `|front| ≈ 40` and `k = 4`, that is ~160 multiply-adds — microseconds.
This is why `reselect` (`API_CONTRACT.md` §3.2) exists as a separate command from `plan`.

---

## 7. Emergency Priority

**Implemented in Phase 6, matching this section exactly** (`optimization/
EmergencyPriority.hpp`, `configs/priority.json`) — the formula and defaults below were
specified in Phase 0 but had no config file or implementation until now.

Distinct from plan selection: this decides **which casualty to serve**, not **which route**.

```
severityScore    = { LOW: 0.25, MODERATE: 0.5, HIGH: 0.75, CRITICAL: 1.0 }
urgency          = 1 − clamp(remainingWindow / originalWindow, 0, 1)
feasibilityScore = 1 if a feasible plan exists, else 0.2

priority = w_s·severityScore + w_u·urgency + w_f·feasibilityScore     ∈ [0,1]
```

Defaults `w_s = 0.4`, `w_u = 0.4`, `w_f = 0.2` in `configs/priority.json`.

### 7.1 Three deliberate consequences

**Priority is dynamic.** `urgency` rises as the deadline approaches, so priority is recomputed
every tick (`SIMULATION_MODEL.md` §9.3). A MODERATE emergency near expiry can and should
overtake a fresh CRITICAL one.

**Feasibility damps rather than excludes.** An unreachable casualty gets `0.2`, not `0`. It
stays in the queue at low priority because the environment changes — a vehicle completing
another mission may bring it into range. Excluding it outright would make the situation
permanently invisible.

**This is triage, and triage is uncomfortable.** The model will sometimes deprioritise a
critical casualty that cannot be saved in favour of a moderate one that can. That is the
intended behaviour of a resource-allocation system under scarcity, and it is stated explicitly
here rather than left as an emergent surprise.

---

## 8. Assignment

Multiple emergencies and multiple vehicles: `ALGORITHMS.md` §7–8 for the algorithms,
`ASSIGNMENT_MODEL.md` for the full Phase 6 design.

**Implemented in Phase 6, correcting §8.1 below.** The formula:

```
C[i][j] = globalScore(vehicle j, emergency i) · (2 − priority(i))
        = kInfeasibleAssignmentCost (1e9)      if no feasible plan exists
```

is the same shape this section specified in Phase 0 — the `(2 − priority)` factor scales
cost inversely with priority, ranging over `[1, 2]`, so high-priority casualties become
cheaper to serve without letting priority alone overwhelm the mission-quality score. What
changed is what `score(...)` means: see §8.1.

### 8.1 The comparability caveat — corrected

§5.1 noted that front-relative normalisation makes scores non-comparable across problems.
**This section originally claimed `C[i][j]` was "comparable within a row (same emergency,
same front)" and relied on that to justify the design. That claim was never true for any
architecture Phase 5 could have built consistently with the rest of this project**: Phase 5
gives every `(vehicle, emergency)` *pair* its own front — never one front shared by every
vehicle attempting the same emergency — so there is no "same front" within a row to be
comparable against. This was written before Phase 5 existed and was never checked against
the real architecture until Phase 6 built the assignment layer and had to actually compare
cells.

**The fix Phase 6 built**, rather than the "noted in `RESEARCH_EXTENSION.md`" alternative
this section previously deferred: `optimization/AssignmentCost.hpp`'s
`computeGlobalIdeal()`/`computeGlobalNadir()` compute `ideal`/`nadir` across **every
feasible candidate's selected-plan cost in the entire matrix**, not per pair, and every
candidate is normalised and scored against those shared bounds via the exact same
`normalize()`/`scorePlan()` machinery from §5/§6 — no new scoring math, only new (global,
not front-relative) bounds fed into it. This makes every cell in the assignment matrix
answer the same question (how good is this candidate relative to the full range of what any
vehicle could achieve for any emergency this cycle) rather than each cell answering a
different, incomparable question.

**Checked directly, not just argued**: `assignment_cost_test.cpp`'s
`FrontRelativeScoresCollideButGlobalAssignmentCostDoesNot` constructs two vehicles with very
different fronts to the same emergency (fuel cost ~0.03 vs ~0.86) whose *front-relative*
scores land at the identical value (`0.3` for both, by §5's own normalisation) despite the
absolute difference — and confirms the corrected *global* assignment cost does not repeat
that collision (`0.45` vs `1.05`).

### 8.2 Both solvers are independently callable, not run automatically together

**This section originally claimed greedy and Hungarian "are both computed on every
assignment cycle" so a `regret` field would "always [be] available to the UI." No such
field or automatic dual-run policy was built — this was Phase 0 planning that got ahead of
what Phase 6 actually scoped.** `optimization/AssignmentResult.hpp`'s
`solveAssignmentHungarian()` and `solveAssignmentGreedy()` are two independent functions
with matching signatures, each callable on the same `AssignmentMatrix`; a caller wanting
`regret` computes `greedyResult.totalCost - hungarianResult.totalCost` itself.
`assignment_bench.cpp` and `assignment_scenarios_test.cpp`'s `ScenarioF` do exactly that for
benchmarking and testing, but no production code path in Phase 6's scope runs both solvers
automatically — wiring that into a UI-facing "regret" display, if wanted, is Phase 7+ scope
(`ALGORITHMS.md` §7.4 has the matching correction).

If regret turns out to be consistently zero on realistic scenarios, **that would be a
finding to report**, not a failure to conceal. It is not zero: `ASSIGNMENT_MODEL.md` §11's
real-ORCA-data scenario shows Hungarian beating greedy by a real, measured margin (1.167632
vs 1.570000), not merely the hand-picked matrix in `ALGORITHMS.md` §7.5.

---

## 9. Constraints

Hard constraints, never score penalties. Enforcement point varies by *what kind* of fact
each constraint is — corrected below from an earlier version of this table, which claimed
the cumulative Δv row was "edge rejected at build" throughout. That was never actually
achievable: a single edge, in isolation, has no way to know the *cumulative* spend of
whatever path reaches it. Per-edge Δv affordability genuinely is a build-time (or
per-search, per Group 2/3 — `GRAPH_MODEL.md` §3) fact; the *running total* is not, and
Phase 3/4 documented this precisely as a tested, honest limitation of single-label search
(`Dijkstra.hpp`'s file comment) before Phase 5 built the multi-label structure needed to
enforce it soundly (`PARETO_SEARCH.md` §9):

| Constraint | Rule | Enforced |
|---|---|---|
| Per-edge Δv affordability | `Δv_edge ≤ vehicle.deltaVBudget` | edge rejected at build/search (Group 2/3) |
| **Cumulative Δv budget** | `Σ Δv_legs ≤ vehicle.deltaVBudget` | **candidate label rejected during Pareto search** (`ParetoSearch.cpp`) — not achievable at build time; single-label Dijkstra/A* cannot enforce it either (`ALGORITHMS.md` §3.6, §4.7) |
| Survival window | `arrivalTime ≤ emergency.deadline` | edge rejected at build |
| Risk ceiling | `risk_edge ≤ maxEdgeRisk` | edge rejected at build |
| Vehicle capacity | one casualty per vehicle (v1) | assignment constraint |
| Vehicle availability | `status ∈ {IDLE, ON_STATION}` | pre-filter |

**Why constraints are not penalties.** A penalty makes infeasibility a matter of degree — with
a large enough time advantage, an over-budget route outranks a feasible one and the planner
recommends a mission that physically cannot fly. Rejecting as early as each constraint's
nature actually permits (build time for per-edge facts, search time for cumulative ones)
makes that structurally impossible either way — the point is "never a score," not "always
at build."

The cost of this rigour is that "no feasible plan" happens, and must be a first-class,
well-explained outcome rather than an error — which is why `planResult` carries
`feasible: false` with a named `bindingConstraint` and per-reason rejection counts
(`API_CONTRACT.md` §4.4).

---

## 10. Tie-Breaking

Ties are common: symmetric scenarios, equal-cost routes, quantised values. Ordering must be
**total and deterministic**, or reproducibility fails.

```
1. lowest score
2. lowest risk                  — prefer safety when otherwise equal
3. lowest time                  — prefer speed when otherwise equal
4. lowest fuel
5. lowest planId                — final, guaranteed-unique arbiter
```

Step 5 makes the order total. `planId` is assigned in deterministic generation order, so the
result is stable across runs and platforms. Without a guaranteed-unique final key, two
equal-scoring plans could be selected differently depending on container iteration order —
the classic source of "deterministic except when it isn't" bugs.

---

## 11. Explanation

Every recommendation carries a rationale (`API_CONTRACT.md` §4.4). Generated by
`planning/MissionExplanation` from engine data via templates.

**No language model. No external API.** Explanations are template-filled from real numbers:

```
"RV-02 via N-0011 balances a 31% time penalty against 44% lower fuel
 and 60% lower risk than the fastest option."
```

Every figure is computed by comparing the chosen plan against specific front members. The
templates cover: chosen vs fastest, chosen vs safest, chosen vs cheapest, binding constraints,
and rejected alternatives. If a comparison is not available (e.g. the front has one member),
the corresponding sentence is omitted rather than filled with a guess.

---

## 12. Worked Example

Scenario: SAT-017 propulsion failure at `t = 412`, window 1920 s. Two vehicles.

**Stage 1 — search.** Label-setting generates 412 labels, prunes 371 by dominance, retains 41,
and yields **8 non-dominated plans** at the sink in 11.4 ms.

Three of them:

| Plan | Vehicle | time (s) | fuel | risk | distance (km) |
|---|---|---|---|---|---|
| P-0012 | RV-02 | 540.1 | 0.242 | 0.181 | 1502.0 |
| P-0007 | RV-02 | 708.4 | 0.136 | 0.072 | 1842.6 |
| P-0019 | RV-03 | 1024.7 | 0.098 | 0.041 | 2210.3 |

None dominates another — P-0012 is fastest, P-0019 is cheapest and safest, P-0007 is between.

**Normalisation** against `ideal = (540.1, 0.098, 0.041, 1502.0)` and
`nadir = (1024.7, 0.242, 0.181, 2210.3)`:

| Plan | n_time | n_fuel | n_risk | n_dist |
|---|---|---|---|---|
| P-0012 | 0.000 | 1.000 | 1.000 | 0.000 |
| P-0007 | 0.347 | 0.264 | 0.221 | 0.481 |
| P-0019 | 1.000 | 0.000 | 0.000 | 1.000 |

**Stage 2 — selection**, same front, three profiles:

| Profile | P-0012 | P-0007 | P-0019 | Winner |
|---|---|---|---|---|
| `FASTEST` (.70/.10/.15/.05) | **0.250** | 0.327 | 0.750 | **P-0012** |
| `BALANCED` (.30/.30/.30/.10) | 0.600 | **0.298** | 0.400 | **P-0007** |
| `SAFETY_FIRST` (.15/.10/.70/.05) | 0.800 | 0.257 | **0.200** | **P-0019** |

Three different answers, **one search**. Switching profiles costs a rescore of 8 points — no
graph traversal. That is the two-stage design paying for itself, and it is exactly what the
Optimization Lab demonstrates live.

---

## 13. Limitations

| Limitation | Detail |
|---|---|
| Front-relative normalisation | Scores are not comparable across planning problems (§5.1, §8.1) |
| ε-dominance is lossy | With `ε > 0` the front is approximate; flagged via `truncated` |
| Label cap truncates | Large graphs may return an incomplete front; flagged, never hidden |
| Weighted-sum selection | Applied *after* the exact front, so §1.1 reason 2 does not bite — but the *selection* step still cannot express non-linear preferences (e.g. "minimise time subject to risk < 0.1"). Lexicographic and constraint-based selection are noted in `RESEARCH_EXTENSION.md` |
| Deterministic objectives | No uncertainty modelling; costs are point estimates, not distributions |
| Priority weights are a design choice | Not derived from any real triage protocol |
| Single-casualty vehicles | `rescueCapacity > 1` is in the data model but not exercised in v1 |

The fourth row deserves emphasis: ORCA computes the front *exactly* and then applies a
*weighted-sum* selection to it. The weighted sum's inability to reach concave regions is
irrelevant here — every front point is already available and directly selectable, including
concave ones, by choosing `CUSTOM` weights or by clicking the point in the Pareto chart. The
limitation is only that the *slider* parameterisation cannot express every conceivable
preference structure.
