# ORCA — Viva Guide

Version 1.0 · Phase 0 · 2026-09-02

Prepared answers to the questions an examiner is most likely to ask. Each has a **short
answer** (what to say first) and **depth** (what to say when pressed).

**The two rules that make a viva go well:** never claim more than the system does, and always
name where the claim is verified. A defensible limitation stated first is worth more than a
strong claim that collapses under one follow-up.

---

## Part I — Data Structures

### Q1. Why a graph?

**Short.** Because the problem is *routing through a network of feasible transfers*. Orbital
bodies are the nodes; feasible transfers between them are the edges; the cost of a transfer is
the edge weight. The question "which route should this vehicle fly?" is literally a
shortest-path question.

**Depth.** The alternative is direct point-to-point transfers only, which discards a real
capability: multi-leg routes via staging points can be cheaper than a direct transfer, because
a phasing leg at an intermediate orbit can dramatically reduce the Δv penalty of departing at
a bad phase. Only a graph exposes that. Also — the graph is what makes multi-vehicle
contention tractable, because paths from different sources into the same sink are comparable
in one search.

---

### Q2. Why an adjacency list rather than a matrix?

**Short.** The graph is sparse and the operation we perform is neighbour enumeration.

**Depth.** After feasibility pruning, the reference demo accepts 173 edges and rejects 583 —
and the ratio worsens with scale, because Δv budgets and survival windows are absolute limits
while candidate pairs grow as `O(V²)`. Adjacency list is `O(V+E)` space against `O(V²)`; at
`V = 1000` that is roughly 6 000 entries versus 1 000 000.

The matrix's one advantage is `O(1)` edge-existence queries — **and Dijkstra and label-setting
never perform that query.** They only ever iterate a node's neighbours, which the list does in
`O(deg(u))` and the matrix in `O(V)`. So the matrix is worse on the operation we do and better
only on an operation we don't do.

*(`ALGORITHMS.md` §1.3)*

---

### Q3. Why is the graph directed?

**Short.** Because `u→v` and `v→u` genuinely differ.

**Depth.** Three ways: **Δv** — raising an orbit and lowering it are different burns from
different starting speeds; **feasibility** — the vehicle at `u` has a different remaining Δv
budget than whatever is at `v`, so an edge feasible one way may be infeasible the other;
**risk** — the corridor is traversed at different times, so debris geometry differs.

Modelling it undirected would silently create routes that cannot physically be flown.

---

### Q4. Why a priority queue? Why a binary heap specifically?

**Short.** Four places need repeated extract-min from a set that changes during iteration:
Dijkstra's open set, A*'s open set, the label-setting open set, and the event queue. That is
exactly a heap's job. `std::priority_queue` gives `O(log n)` push/pop.

**Depth — the Fibonacci heap question, which is usually the follow-up.** A Fibonacci heap gives
Dijkstra `O(E + V log V)` versus the binary heap's `O((V+E) log V)`. We use the binary heap
anyway, for three reasons: the improvement matters on **dense** graphs and ours is sparse;
Fibonacci heaps have large constants and poor cache locality and routinely lose in practice at
these sizes; and `std::priority_queue` is standard and needs no maintenance.

**And how we avoid needing `decrease-key` at all:** *lazy deletion*. Rather than updating an
entry, we push a new one and skip stale entries on pop (`if (cost > dist[node]) continue;`).
That costs `O(E)` heap entries instead of `O(V)` — a constant-factor memory increase that
removes the indexed-heap machinery entirely.

*(`ALGORITHMS.md` §2.4)*

---

### Q5. The emergency queue's priority changes over time. Why not a decrease-key heap?

**Short.** Because `n` is tiny. We rebuild the heap each tick in `O(n)`.

**Depth.** Pending emergencies typically number under ten. An indexed heap with `decrease-key`
would be more code, more state and more bug surface, all to optimise an `O(10)` operation.
`std::make_heap` on a small vector is `O(n)` and obviously correct. If the pending set ever
grew into the thousands, this decision would flip — and the answer would change with it.

*(`ALGORITHMS.md` §2.5)*

---

## Part II — Algorithms

### Q6. Why Dijkstra, and what is it for here?

**Short.** It is the **baseline**, not the product. It answers "what is the best route if we
care about exactly one thing?" and gives us something to measure ORCA's multi-objective
planning against.

**Depth.** Complexity `O((V+E) log V)` with a binary heap. Correctness needs non-negative
weights, which we have — time, fuel, risk and distance are all `≥ 0` by construction.

**The important caveat, which you should raise before the examiner does** — see Q9.

---

### Q7. Why A*? What is your heuristic and is it admissible?

**Short.** `f(n) = g(n) + h(n)`. `g` is actual cost so far, `h` estimates cost to the goal, and
the open set is ordered on `f` so search is pulled toward the goal.

Our heuristics are **physical lower bounds**, which is the cleanest admissibility argument
available:

- **Time:** `h = π√(((r₁+r₂)/2)³/μ)` — the Hohmann half-period. Admissible because no impulsive
  two-burn transfer between those radii is faster, and any real route must additionally pay
  phasing.
- **Fuel:** `h = |Δv_hohmann| + 2v·sin(Δi/2)` — ideal Hohmann Δv plus the irreducible plane
  change. Both terms are unavoidable, so their sum cannot overestimate.

**Depth, and the honest parts.**

*Risk and distance use `h = 0`.* A zero heuristic is trivially admissible, and A* degenerates
to Dijkstra there. We have no useful lower bound on accumulated risk and do not pretend to.

*Admissible but not consistent.* The triangle inequality can fail across edges with large
waiting components, so we use a closed set **with re-opening** — a node may be re-expanded if
reached with a strictly better `g`. That preserves optimality under mere admissibility.

*A\* is not asymptotically better than Dijkstra.* Both are `O((V+E) log V)`. A* is better in
practice by a constant determined by heuristic quality. We **measure** the effect rather than
assert it: the benchmark reports `nodesExpanded` and wall time separately, because fewer
expansions with higher wall time is a real possible outcome when the heuristic is expensive.

*Verification:* a test asserts `cost_A* == cost_Dijkstra` exactly on 500 random graphs, and a
separate test asserts `h(n) ≤ trueCost(n, goal)` for every node in every fixture.

*(`ALGORITHMS.md` §4, `TEST_PLAN.md` §4.3)*

---

### Q8. Your edge weights depend on departure time. Doesn't that break Dijkstra?

**This is the sharpest question available, and having the answer ready is worth a great deal.**

**Short.** It would, without one extra property. Time-dependent Dijkstra remains optimal if the
cost function is **FIFO** (non-overtaking): departing later can never arrive earlier. Formally,
for `t₁ ≤ t₂`: `t₁ + w_time(e,t₁) ≤ t₂ + w_time(e,t₂)`.

**Depth.** Dijkstra's invariant is that a popped node's distance is final. Without FIFO, a
later relaxation could improve an already-finalised node — a longer-cost path yielding an
earlier arrival — and the greedy commitment breaks.

Our transfer model is **constructed** to be FIFO: waiting for a better phase is modelled as
explicit wait time *on the edge*, so `arrival(t) = t + wait(t) + t_transfer(t)` is
non-decreasing in `t` by construction. There is no manoeuvre in the model that overtakes an
earlier departure.

And we don't merely assert it — `tests/algorithms/fifo_test.cpp` samples departure times across
every edge and asserts the inequality. **If that test fails, Dijkstra's and A*'s optimality
claims are void**, and the correct response is to fix the transfer model, not relax the test.

Worth adding: if we later adopted a Lambert solver (`RESEARCH_EXTENSION.md` §3), FIFO might
genuinely break, because Lambert admits transfers where departing later arrives earlier. We
would then need a time-dependent shortest-path variant. Knowing that in advance is part of why
the property is documented rather than assumed.

*(`ALGORITHMS.md` §3.4, `SIMULATION_MODEL.md` §4.6)*

---

### Q9. What is Pareto dominance?

**Short.** `A` dominates `B` if `A` is **no worse in every objective and strictly better in at
least one**.

```
A ≺ B  ⟺  (∀i: Aᵢ ≤ Bᵢ) ∧ (∃j: Aⱼ < Bⱼ)
```

**Depth.** The crucial property is that dominance is a **partial order, not a total one**. Some
pairs are incomparable — a route that is faster but costlier neither dominates nor is dominated.
Because there is no unique minimum, the correct output is a **set**: the non-dominated set, or
Pareto front.

That single fact is what motivates every design decision in ORCA's optimizer. Everything else
follows from it.

We property-test irreflexivity, asymmetry and transitivity over 10 000 random triples, and use
an epsilon band in the comparison — without it, floating-point noise makes dominance
non-transitive, which silently corrupts the front.

*(`ALGORITHMS.md` §6)*

---

### Q10. Why not just combine the objectives into one weighted score and run Dijkstra?

**The central question of the project. Four reasons, and the second is the decisive one.**

**1 — The weights are a guess made before the options are known.** A scalar score hides the
*exchange rate*. An operator setting `w_time = 0.7` can never discover that a 4% time penalty
would have bought a 40% fuel saving, because that trade was collapsed before they saw it.

**2 — Weighted sums provably cannot reach non-convex regions of the front.** This is
mathematics, not tuning. A weighted sum selects the point where a hyperplane of slope `−w`
first touches the solution set. A point sitting in a **concave dent** of the front is never
touched by any such hyperplane, for **any** non-negative weights. Sweeping every weight from 0
to 1 will never return it — even if it is the best compromise available. Label-setting finds
it.

**3 — Feasibility gets corrupted.** With a scalar penalty for exceeding fuel budget, a
sufficiently fast route outranks a feasible one, and the planner recommends a mission that
cannot physically fly. We treat feasibility as a hard constraint at graph-build time, so an
infeasible plan is never a candidate.

**4 — Interactivity.** The front is computed once; changing weights costs `O(|front|)` — a
rescore of a few dozen points. That is *why* the Optimization Lab responds instantly.

**Be precise about what we do, since it is a fair follow-up:** we compute the front exactly,
*then* apply a weighted sum to select from it. Reason 2 does not bite at the selection stage,
because every front point — including concave ones — is already computed and directly
selectable by clicking it.

*(`OPTIMIZATION_MODEL.md` §1.1)*

---

### Q11. How does the Pareto search work?

**Short.** **Martins' label-setting algorithm** with dominance pruning — a named, published
algorithm, not something we invented.

The key departure from Dijkstra: a node no longer has *a* best distance. It has a **set of
mutually non-dominated labels**, because a slower-but-cheaper path is incomparable and both
must be kept.

**Depth.** Pop the lexicographically smallest label; extend it along each outgoing edge;
compute the new cost vector; if any existing label at the target dominates it, discard it; if
it dominates existing labels, remove those; otherwise insert and push.

**Why pruning is sound:** every objective is monotonically non-decreasing along a path. So if
`M` dominates candidate `c'` at node `v`, then for *any* continuation `P`, `M ⊕ P` dominates
`c' ⊕ P`. Discarding `c'` therefore cannot discard any Pareto-optimal solution.

That argument depends on monotonicity, which is why risk composes as `1 − Π(1−rᵢ)` — bounded
in `[0,1]`, monotonically non-decreasing, order-independent — and why we have a test called
`PathCompositionIsMonotonic`.

*Verification:* on graphs with `V ≤ 7`, we enumerate all simple paths, filter by dominance, and
compare the resulting set to label-setting's output **exactly**. Brute force is obviously
correct and obviously too slow; label-setting is fast and subtle. Comparing them is the
strongest evidence available.

*(`ALGORITHMS.md` §5, `TEST_PLAN.md` §4.6)*

---

### Q12. What is the complexity of the Pareto search?

**Short.** `O(|L| log|L| · (k + deg))` where `|L|` is labels generated. **`|L|` can be
exponential in `V` — multi-objective shortest path is NP-hard.**

**Depth — say the NP-hardness first; it is a strength, not a weakness.** The number of
Pareto-optimal paths can grow exponentially, so no polynomial bound exists.

Four mitigations, two exact and two approximate — and we are explicit about which is which:

| Mitigation | Exact? |
|---|---|
| Dominance pruning (demo: 412 generated → 371 pruned → 41 retained) | exact |
| Feasibility pre-pruning at graph build | exact |
| ε-dominance (collapses labels within `ε`) | **approximate** |
| `maxLabels` cap | **truncates** |

When either approximation is active, `planResult.truncated` is set and the UI shows a badge.
**An approximate result is never presented as exact.**

Also worth volunteering: dominance weakens as `k` grows — with more objectives fewer pairs are
comparable, so less gets pruned. That is part of why we chose `k = 4` rather than adding
objectives freely.

---

### Q13. Why implement the Hungarian algorithm when greedy assignment exists?

**Short.** Because greedy can be arbitrarily bad, and we wanted to measure how bad rather than
assume.

**Depth — the counterexample, which is worth memorising:**

```
              RV-01   RV-02
    E-A (crit)  10      12
    E-B (mod)   11      99
```

Greedy takes the global minimum `(E-A, RV-01) = 10`, forcing `(E-B, RV-02) = 99`. Total **109**.
Optimal is `(E-A, RV-02) = 12` plus `(E-B, RV-01) = 11`. Total **23**. Greedy is worse by
nearly 5×, because it commits to a locally cheap choice that destroys the only good option for
the other casualty.

Hungarian is `O(n³)` via the Jonker–Volgenant formulation with dual potentials and shortest
augmenting paths. Correctness rests on LP duality: we maintain potentials such that all reduced
costs are `≥ 0` with equality on assigned pairs (complementary slackness), so a feasible primal
satisfying that with a feasible dual is optimal.

We run **both** solvers on every assignment cycle and report `regret = greedy − optimal`.

**And the honest part:** if regret turns out to be consistently zero on realistic scenarios,
that is a finding we report, not conceal. The counterexample proves regret *can* be large;
whether it *is* large in practice is an empirical question the benchmark answers, and
Hungarian remains justified as the verification that settles it.

*Verification:* `HungarianMatchesBruteForce` compares against all `n!` permutations for `n ≤ 8`.

*(`ALGORITHMS.md` §7–8)*

---

### Q14. Why dynamic replanning? How does it work?

**Short.** Because a plan computed at `t=0` may be wrong at `t=200`. Orbits move, debris is
detected, casualties deteriorate.

**Depth — three design decisions worth defending:**

**Replan from current position, not origin.** Δv and time already spent are sunk. Replanning
from the origin would produce routes the vehicle cannot fly.

**Incremental patch, not full rebuild.** Only edges intersecting the affected region are
re-costed, found via the spatial index. And a test asserts the patched graph is **identical,
edge for edge, to a full rebuild** — the optimisation is verified, not trusted.

**Hysteresis.** We switch only if the alternative beats the current plan by a configured
margin. Without it, two near-equal routes cause oscillation every tick — visually broken and
wasteful in the fuel model.

**The limitation, stated plainly:** replanning is **reactive**. ORCA responds after a change is
detected; it does not plan under uncertainty over possible futures. That is the most valuable
item in `RESEARCH_EXTENSION.md` and the deepest limitation in the current design.

---

## Part III — Modelling

### Q15. How is risk modelled?

**Short.** It is a **simulation risk model** — a designed, tunable heuristic producing a score
in `[0,1]`. It is **not** a calibrated collision probability, and we never describe it as one.

**Depth.** Weighted combination of debris proximity (quadratic falloff to zero at the hazard
radius), manoeuvre complexity, fuel margin on arrival, comms health and vehicle health.
Weights live in `configs/risk.json`.

Along a path, risk composes as **independent survival**: `1 − Π(1 − rᵢ)`. Two 10% legs give
19%, not 20%. Three properties matter, and all three are required by the algorithm: bounded in
`[0,1]`, monotonically non-decreasing (which dominance pruning's soundness depends on), and
order-independent.

The independence assumption is a simplification — real hazards correlate, e.g. one debris
cloud threatening consecutive legs — and it is listed in `SIMULATION_MODEL.md` §8.

---

### Q16. How is fuel modelled?

**Short.** As a **fraction of each vehicle's own Δv budget**. Not kilograms of propellant.

**Depth.** We don't model spacecraft mass, so the rocket equation doesn't apply and quoting
propellant mass would be fabrication. `fuelCost = Δv_required / Δv_budget`.

**The subtlety worth volunteering:** "13.6% fuel cost" means 13.6% *of that vehicle's own
capability*, so it is **not comparable across vehicles with different budgets**. RV-02's 13.6%
and RV-03's 13.6% are different quantities of Δv. That is why the UI always shows the figure
attached to a named vehicle.

Feasibility is a hard constraint, never a penalty: an over-budget route is excluded from the
graph. A penalty would let a fast infeasible route outrank a feasible one.

---

### Q17. How does your simulation differ from real orbital mechanics?

**Answer this fully and without defensiveness. Volunteering limitations is the strongest
possible move here.**

**Short.** ORCA uses circular two-body orbits with impulsive burns and patched Hohmann
transfers. It is a **simulation model designed to produce a physically plausible, internally
consistent optimization problem** — not a validated astrodynamics tool.

**The main simplifications:**

| | Consequence |
|---|---|
| Circular orbits only, no eccentricity | No elliptical parking orbits |
| Two-body only — no J2, drag, SRP | Orbits never decay or precess; real LEO precesses several °/day |
| Impulsive burns | No finite-burn losses, no low-thrust |
| Patched Hohmann + phasing penalty, not Lambert | Transfers approximate |
| No mass modelling | Fuel is Δv fraction; no propellant mass |
| Instantaneous rendezvous | No proximity operations or docking |
| Perfect information once detected | No sensor or state-estimation error |
| Synthetic scenarios, no TLE | Findings are about algorithms, not real orbital traffic |

**Then the important qualifier.** These constrain the *physical* claims. They do **not** weaken
the *algorithmic* claims, which are the point: the graph is genuinely sparse, directed and
time-dependent; the objectives genuinely conflict; dominance, label-setting, Dijkstra, A* and
Hungarian are exact and independently verified regardless of where edge weights came from.

**The one-sentence version:** *ORCA solves a real optimization problem over an approximate
physical model.*

**And what we did validate:** the formulae reproduce textbook values — LEO(300 km)→GEO Hohmann
Δv = 3.8926 km/s against ≈3.9; circular velocity at 500 km = 7.6126 km/s against ≈7.61; orbital
period at 500 km = 94.62 min. Compiled and run, not asserted.

*(`SIMULATION_MODEL.md` §8)*

---

### Q18. Where does the time/fuel conflict actually come from? Did you just assume it?

**A question worth inviting, because the answer is the strongest fact in the project.**

**Short.** It was **measured**, not assumed.

**Depth.** Two orbits return to the same relative geometry once per **synodic period**. For a
500 km and an 800 km orbit we computed `T_syn = 1525.3 minutes ≈ 25.4 hours`.

So the fuel-optimal departure phase recurs about **once a day**, while survival windows run
**tens of minutes**. The planner essentially never has the option of waiting for perfect
alignment. It must choose between *"depart now at a poor phase and pay Δv"* and *"wait for a
better phase and pay time"*.

**That is ORCA's time/fuel Pareto tension, and it falls out of orbital mechanics rather than
being asserted to justify using a multi-objective algorithm.** That distinction is the
difference between a project that uses Pareto optimization and a project that *needs* it.

A second measured finding shapes the cost structure: a 1° plane change at 500 km costs
0.1329 km/s, while a full 500→800 km Hohmann raise costs 0.1608 km/s. So **one degree of
inclination costs 83% of a 300 km altitude raise**, and about 1.2° equals it. Inclination
dominates — which means Euclidean distance is a *physically wrong* proxy for cost, and that is
precisely why `distance` is a separate objective rather than a stand-in for fuel.

*(`REPOSITORY_AUDIT.md` §4, `SIMULATION_MODEL.md` §4.3, §4.5)*

---

## Part IV — Architecture

### Q19. Why C++ for the core and JavaScript for the UI? Isn't that over-engineered?

**Short.** Each layer uses the technology that is genuinely best for it, and the seam between
them is deliberately tiny — one process boundary carrying newline-delimited JSON.

**Depth.** C++20 owns everything computational: graph, algorithms, optimization, simulation,
planning, analytics. React and Three.js own presentation, which is what they are good at and
what C++ is not. Node is a thin bridge: it spawns the engine, relays messages, and manages the
socket. **No mission logic exists in Node or React.**

**The evidence this is real, not aspirational:** the UI technology already changed once during
Phase 0 — an earlier pass selected a Qt desktop UI — and **the computational core required zero
changes**. That is the property the layering was designed to have, demonstrated rather than
claimed.

**And there's a build target enforcing it:** a CI job builds and runs the entire C++ test suite
in a container **with no Node installed**. If the engine ever developed a dependency on the
frontend, that job fails.

---

### Q20. Why NDJSON over pipes instead of a native Node binding?

**Short.** Because it is simpler, more robust, and easier to debug — and the native binding
buys nothing we need.

**Depth.**

| | NDJSON over stdio | N-API native binding |
|---|---|---|
| Dependencies | none | node-gyp, per-Node-version rebuilds, ABI concerns |
| Engine crash | kills a child process; Node reports "engine unavailable" | **takes down the web server too** |
| Debugging | `echo '{"cmd":...}' \| ./orca_engine --serve` | attach a debugger to Node |
| Testing | golden-file: feed recorded stdin, diff stdout | mocking framework |
| Engine standalone | same binary, same code path, runs headless | needs a shared-library build |

Crash isolation and hand-debuggability are worth a great deal on a project like this. Choosing
the native binding would be picking the more complicated mechanism because it sounds more
advanced — and it would have made the "engine runs standalone" guarantee harder to keep.

We do use WebSocket, but only between Node and the browser, where it is the natural fit. Right
technology at each layer.

*(`ARCHITECTURE.md` §4)*

---

### Q21. How do you guarantee the frontend isn't doing the real work?

**Short.** Three mechanisms, one of which is a test that fails the build.

**Depth.**

1. **Types.** Snapshot fields are `readonly` and mirror the engine schema. A value not in the
   snapshot literally isn't available to compute with.
2. **The engine ships derived values.** For instance `remainingWindowSec` is computed by the
   engine and sent, rather than letting React subtract `deadline − simTime`. That subtraction
   would be trivially correct today and would silently diverge the moment deadlines became
   mutable.
3. **A contract test.** `DisplayedValuesMatchFixtureExactly` renders components with recorded
   fixture data and asserts displayed numbers equal fixture numbers. If a component starts
   deriving instead of displaying, and its derivation drifts, the test fails.

**One deliberate exception, which you should state rather than let it be found:** between
snapshots the frontend interpolates marker *positions* for visual smoothness — 60 fps rendering
from 10 Hz data. It is bounded by three rules: it never feeds back into engine state, it is
discarded on every snapshot, and **no displayed number is derived from it**. Interpolated
positions move pixels only.

---

### Q22. Can the engine run without the frontend?

**Short.** Yes, and there is a CI job that proves it.

**Depth.** One binary, four modes:

```
orca_engine --serve                    NDJSON server (what Node spawns)
orca_engine plan  --scenario X         one-shot plan → JSON
orca_engine run   --scenario X         headless full simulation
orca_engine bench --sizes 25,...,1000  benchmark suite
```

All benchmarks, experiments and tests run headless. If the frontend and backend directories
were deleted tomorrow, ORCA would still build, still pass its full test suite, still plan
missions, and still produce every research result.

The frontend makes the algorithms *visible*. It does not make them *work*.

---

### Q23. How is the project reproducible?

**Short.** Seed plus config determines the run exactly. Ten runs at the same seed produce
byte-identical state hashes.

**Depth — four things had to be true:**

1. **No `std::random_device`, ever.** All randomness derives from the scenario seed.
2. **Stream splitting.** Each subsystem draws from its own derived generator, so adding a
   debris object does not shift the failure schedule. Without this, any change to one
   subsystem's draw count would perturb every later draw and destroy cross-run comparability.
3. **No wall-clock in logic.** `speedMultiplier` changes how fast the simulation is *watched*,
   never how it is *computed*. A run at 1× and at 16× produce identical histories.
4. **Total tie-breaking.** Ties resolve on score, then risk, then time, then fuel, then
   `planId` — which is unique, making the order total. Without a unique final key, container
   iteration order could change the winner.

**A subtle one worth mentioning if pressed:** we implement our own uniform and normal
transforms rather than using `std::uniform_real_distribution`, because the standard
distributions' output is **implementation-defined**. Using them would give determinism on one
machine but not across compilers. `std::mt19937_64` itself *is* specified, so the underlying
bit stream is portable.

*(`SIMULATION_MODEL.md` §7)*

---

## Part V — Judgement

### Q24. What is the weakest part of your design?

**Answer honestly. This question rewards candour and punishes deflection.**

Three, in order:

**1 — Reactive replanning.** ORCA responds to changes after detection; it does not plan under
uncertainty. A route that is slightly worse now but robust to future hazards is something ORCA
cannot currently prefer, because it has no representation of "might become dangerous". This is
the deepest limitation and the highest-value extension.

**2 — Front-relative normalisation.** Normalising against the front's own ideal and nadir makes
weights behave intuitively *within* a planning problem, but it means scores are **not comparable
across** problems. That matters in the assignment cost matrix, where scores from different
emergencies sit side by side. We mitigate it by relying on the priority factor rather than raw
scores for cross-row weighting, but it remains a real modelling compromise.

**3 — We designed the model, the scenarios and the algorithm.** A model built by people
expecting multi-objective planning to win can produce a model where it wins. Partial
mitigations: the central trade-off comes from an independently validated physical quantity
rather than a tuned parameter; the `weighted-single` comparator uses identical weights, graph
and cost model so the comparison isolates one variable; and we pre-registered the honesty
condition that if Pareto does not beat weighted-single, we report that.

It is a partial mitigation, not a solution.

---

### Q25. What would you do differently?

Three things.

**Design for distributional costs from the start.** `ObjectiveVector` assumes scalar costs, and
that assumption is the one blocker to the most valuable extension. Making it generic over a
cost type would have been cheap at the start and is expensive now.

**Decide the assignment cost normalisation more carefully.** The comparability issue in Q24
surfaced late, during optimization-model design. Thinking about cross-problem comparability
before writing the cost matrix would have been better.

**Question the fourth objective harder, earlier.** `distance` earns its place — but only
because the plane-change measurement showed it genuinely disagrees with `fuel`. We arrived at
that justification after choosing the objective. It would have been better to demand the
justification first. It happened to work out; that is luck, not method.

---

### Q26. If you had to cut features, what goes first?

In order: Electron packaging, Scenario Builder, Mission History, Union-Find, the spatial index
(below ~200 objects brute force is genuinely fine), then presentation mode.

**What never gets cut:** the C++ core, the algorithms, the tests, determinism, and Mission
Control. The engine plus its test suite *is* the project. Everything else makes it legible.

Note that Union-Find is already conditional: it is implemented **only if** benchmarks show a
measured benefit in the multi-emergency path. Adding a data structure to a DSA project for the
sake of having it is exactly what we set out to avoid.

---

### Q27. Convince me this is a DSA project and not a graphics project.

Navigate to `cpp/algorithms/`. Dijkstra, A*, label-setting, dominance, priority scheduling,
Hungarian, spatial index — each in its own file, each independently unit-tested, each with a
documented complexity and correctness argument.

Then delete `frontend/` and `backend/`. Everything still builds, every test still passes, every
benchmark still runs, every research result is still producible. **There is a CI job that does
exactly this.**

Then look at what the visualisation actually shows: the mission graph, the search frontier, the
labels being pruned by dominance, the Pareto front with its dominated points. The UI's purpose
is to make the data structures *visible*.

The graphics layer is the least essential part of the system, and it is built that way on
purpose.

---

## Part VI — Phase 11 (Scaling, Benchmarking, Research)

### Q28. You claim A\* is faster than Dijkstra. Prove it, with real numbers.

`docs/RESEARCH_EVALUATION.md` §5.1, from 972 real `orca_engine --bench` rows (972, not
estimated — `benchmarks/results/phase11_search_comparison.csv`). At 100 objects: Dijkstra
expands 53.0 nodes on average, A\* expands 29.9 (43.6% fewer) and runs 19.7% faster in wall
time (0.191ms vs 0.153ms). At 25 objects the two are essentially tied (A\* is fractionally
*slower*, 0.01334ms vs 0.01330ms) — heuristic-evaluation overhead cancels the small
node-count saving at that scale. **The honest claim is "A\* wins once the graph is large
enough to amortize heuristic overhead," not a blanket "A\* is always faster."**

### Q29. Why does multi-objective (Pareto) search cost so much more than single-objective search?

Because it maintains a *set* of non-dominated labels per node instead of one best-so-far
distance — `ALGORITHMS.md` §5's own complexity argument. Measured, not just argued: Pareto
search is 9.5×–30.4× slower than Dijkstra at the tested sizes (25–100 objects), and its own
scaling ratio (time at 100 objects ÷ time at 25 objects) is 45.9× versus Dijkstra's 14.4× for
the same 4× object-count increase — Pareto's cost doesn't just start higher, it *grows
faster* (`RESEARCH_EVALUATION.md` §5.2). This is also *why* candidate generation dominates
assignment cost (`docs/FINAL_BENCHMARK_REPORT.md` §4): generating one Pareto search per
`(vehicle, emergency)` pair means an `O(vehicles×emergencies)` loop over an operation that
itself scales badly.

### Q30. You built a benchmark suite that runs the brief's full requested matrix. Did it work?

No, and that failure is itself reported, not hidden. The first attempt (8 families, sizes to
500, assignment matrices to 50) ran 36+ minutes and was killed. The default `--bench` suite
now runs a smaller, documented subset (972+144 real rows, 7.2 seconds) —
`docs/FINAL_BENCHMARK_REPORT.md` §3-4 states exactly what was cut and why (an `O(N²)`
candidate-generation cost at large assignment-matrix sizes, confirmed independently by
`RESEARCH_EVALUATION.md`'s per-search scaling numbers). The underlying capability
(`runAssignmentBenchmark()`) still accepts any size; only the CLI default was scoped down.

### Q31. Is greedy assignment actually worse than Hungarian, or does it just look that way?

A naive comparison of `totalAssignmentCost` said greedy was *cheaper* 75% of the time — which
would have been a wrong, reportable-sounding but false finding. The real cause: greedy and
Hungarian often assign a *different number* of pairs (greedy leaves emergencies unserved),
so summing cost over fewer served emergencies looks artificially cheap.
`docs/RESEARCH_EVALUATION.md` §5.3 shows the corrected, like-for-like comparison (restricted
to the 15/60 paired runs where both assigned the same count): Hungarian is *never* worse than
greedy (regret ≥ 0 always), confirming its optimality as an exact solver. The more
operationally important finding is that greedy serves fewer emergencies overall — a coverage
failure, which the raw cost sum hid completely.

### Q32. What did instancing the satellite/vehicle markers actually save?

Measured with a real headless-Chromium session (Playwright) against the real app, WebGL
draw-call counters instrumented at the browser API level (`WebGLRenderingContext.prototype.draw*`),
comparing the same 500-satellite/62-vehicle scenario before and after: 1,128 → 568 draw calls
per frame (49.6% reduction), matching exactly the expected 562-marker → 2-`InstancedMesh`
math (`docs/FINAL_BENCHMARK_REPORT.md` §6). **Stated honestly**: orbit rings, left
un-instanced (each ring's shape/orientation genuinely differs, not just scale), now dominate
the scene's remaining draw calls (~566 of ~568/frame) — instancing markers did not make the
scene "fast," it removed the *specific* O(N) cost that was cheapest to fix this phase.

### Q33. What's the most interesting bug you found this phase, and how?

A bootstrap deadlock: the frontend's Start button was disabled until `connectionStatus ===
'Connected'`, which required a successful `/snapshot` fetch, which never succeeded until
*some* command had been sent — but Start was the only control that could send one. A fresh
page load could never recover. It was invisible to every prior automated test because
`server.test.ts`'s REST tests use Node's own `fetch` (no CORS enforcement, and they always
call `/sim/start` directly, never relying on a UI button's enabled state) and jsdom component
tests never load a real WebGL/CORS-governed browser session. It surfaced only because Phase
11's P4 rendering measurement required loading the real app in real headless Chromium for the
first time in the project's history. Fixed by sending one `getSnapshot` at backend boot
(`backend/src/index.ts`) — `docs/FINAL_BENCHMARK_REPORT.md` §7 has the full account,
including a second real bug (missing CORS headers) found the same way.

---

## Quick Reference — Complexity

| Algorithm | Time | Space | Optimal |
|---|---|---|---|
| Graph build | `O(V²·C)` | `O(V+E)` | exact |
| Heap push/pop | `O(log n)` | `O(n)` | exact |
| Dijkstra | `O((V+E) log V)` | `O(V+E)` | ✅ non-neg + FIFO |
| A* | `O((V+E) log V)` | `O(V+E)` | ✅ admissible `h` |
| Label-setting | `O(\|L\| log\|L\|·(k+deg))` | `O(\|L\|·k)` | ✅ if `ε=0`, untruncated |
| Dominance test | `O(k)` | `O(1)` | exact |
| Front extraction | `O(n log n + n·k)` | `O(n)` | exact |
| Greedy assignment | `O(m·n·min(m,n))` | `O(mn)` | ❌ |
| Hungarian | `O(n³)` | `O(n²)` | ✅ |
| Spatial query | `O(1)` expected | `O(D)` | exact |
| Union-Find | `O(α(V))` | `O(V)` | exact |

## Quick Reference — Validated Numbers

| Quantity | Value |
|---|---|
| `μ` Earth | 398 600.4418 km³/s² |
| `R⊕` | 6378.137 km |
| Circular velocity @ 500 km | 7.6126 km/s |
| Orbital period @ 500 km | 94.62 min |
| LEO(300 km)→GEO Hohmann Δv | 3.8926 km/s |
| LEO(300 km)→GEO transfer time | 316.5 min (5.28 h) |
| Plane change 1° @ 500 km | 0.1329 km/s |
| Hohmann 500→800 km | 0.1608 km/s |
| **Inclination : altitude cost ratio** | **0.83 — break-even ≈ 1.21°** |
| **Synodic period 500/800 km** | **1525.3 min ≈ 25.4 h** |
| A\* node-expansion reduction vs Dijkstra @ 100 objects | 43.6% fewer nodes |
| A\* wall-time advantage @ 100 objects | 19.7% faster |
| Pareto vs Dijkstra cost @ 100 objects | 30.4× slower |
| Pareto scaling ratio (100 vs 25 objects, 4× size) | 45.9× time |
| Assignment candidate-gen ÷ solver time @ matrixSize=40(padded) | 5,970.6× |
| Marker draw calls, 500 sats/62 vehicles, before/after instancing | 1,128 → 568/frame |
| `--bench` default suite: rows generated, wall time | 1,116 rows, 7.2 s |
| 500-object `SimulationEngine` construction (dense family) | ~102–131 ms |
| 1000-object `SimulationEngine` construction (dense family) | ~402–409 ms |

All of Phase 11's added numbers above trace to `docs/RESEARCH_EVALUATION.md` and
`docs/FINAL_BENCHMARK_REPORT.md`, both computed from real `orca_engine --bench` output or
real standalone-probe runs — none estimated.
