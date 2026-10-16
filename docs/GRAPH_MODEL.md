# ORCA — Graph Model

Version 1.0 · Phase 3 · 2026-09-02

How ORCA turns a scenario into a searchable graph, and how earliest-arrival Dijkstra
searches it correctly despite every edge's cost depending on when you use it.

---

## 1. Purpose

`cpp/graph/` and `cpp/algorithms/` answer two questions in sequence:

> Given a scenario, what transfers between which orbital objects are structurally
> possible? *(GraphBuilder → MissionGraph)*

> Given that graph and a specific vehicle, what is the fastest way from A to B, departing
> no earlier than now? *(Dijkstra)*

Neither layer computes orbital mechanics itself. Both call `TransferModel` (Phase 2) for
every number they use — this file is about *how they call it correctly*, not about what a
transfer costs.

---

## 2. Node Model

### 2.1 Two kinds, not four

```cpp
enum class NodeKind { Satellite, RescueVehicle };
```

Phase 0's `DATA_MODEL.md` §4 originally sketched four (`VEHICLE_SOURCE`, `SATELLITE`,
`CASUALTY_SINK`, `WAYPOINT`). Phase 3 implements two, matching the underlying entity type,
and `DATA_MODEL.md` has been corrected.

The finer distinctions collapse into predicates the domain entities already have:

| Phase 0 kind | Phase 3 equivalent |
|---|---|
| `CASUALTY_SINK` | `scenario.findSatellite(id)->requiresRescue()` |
| `VEHICLE_SOURCE` | `scenario.findVehicle(id)->isAvailable()` |
| `WAYPOINT` | any `Satellite` node — no synthetic node type needed |

Storing rescue-worthiness as a static node tag would create a second place that fact
lives, alongside the satellite's own status — and unlike the satellite's status, the tag
would not update as the run progressed. Querying the entity is both simpler and correct
by construction.

### 2.2 No synthetic waypoints

`SYSTEM_DESIGN.md`'s staging concept — "healthy satellites → staging nodes" — is realised
by ordinary satellite nodes having outgoing edges, exactly as any other node does. There
is no `WAYPOINT` node kind, and `GraphBuilder` does not invent nodes that do not
correspond to a scenario entity (`IMPLEMENTATION_ROADMAP.md`'s standing guidance against
artificial transfer nodes).

### 2.3 A node owns a copy of its `Orbit`

```cpp
GraphNode(NodeId id, SatelliteId satelliteId, Orbit orbit) noexcept;
GraphNode(NodeId id, VehicleId vehicleId, Orbit orbit) noexcept;
```

`Orbit` is an immutable value type (Phase 1), and ORCA models no perturbations
(`SIMULATION_MODEL.md` §8), so an orbit does not change over a scenario's duration — only
the *phase along it* does, and that is a pure function of time
(`Orbit::positionAtKm`/`phaseAtRad`). A copy is therefore not a stale snapshot the way a
cached *position* would be; it is the complete truth for all time. This is why §5 below
concludes no separate "graph snapshot" concept is needed for Phase 3.

---

## 3. Edge Model and the Three-Way Feasibility Split

This is the section to read before changing anything in `GraphBuilder` or `Dijkstra`.

### 3.1 What makes the split possible

`TransferModel`'s two Phase 2 properties (`TRANSFER_MODEL.md` §13):

1. **FIFO** — departing later never arrives earlier.
2. **Non-time objectives are time-independent** — Δv, fuel, risk and path length depend
   only on the two orbits, never on departure time.

Property 2 means every one of `TransferFailureReason`'s seven values falls into exactly
one of three groups, each with a different lifetime:

### 3.2 Group 1 — permanent facts, decided once by the builder

- `InvalidOrbit`
- `PhasingUnavailable`, the *"zero relative drift and never aligned"* case — a permanent
  geometric fact about the pair (`PhasingModel.hpp`'s proof that with no drift, relative
  phase never changes).
- `RiskExceeded`, `DistanceExceeded` — a single leg's risk and arc length do not depend on
  when it is flown, so a policy ceiling on either is legitimately a build-time decision.

`GraphBuilder` evaluates every candidate pair with **capability and wait/deadline
effectively unbounded** (`kUnboundedDeltaVKms = 1e6` km/s, `kUnboundedWaitSec = 1e12` s —
finite, not literal infinity, so no downstream division risks a NaN). Under those
constraints, `TransferModel::evaluate()` can only reject for a Group 1 reason. An edge
that exists in the graph is therefore **true for every vehicle, at every departure time**.

### 3.3 Group 2 — vehicle-dependent, decided per search by Dijkstra

- `DeltaVExceeded`, `FuelInsufficient` — whether one leg alone is affordable depends on
  which vehicle is asking, never on when it asks.

The builder cannot decide these: it does not know which vehicle will use a given edge, and
the same edge is valid graph structure for every vehicle, affordable or not. Baking one
vehicle's budget into the graph would make the graph single-vehicle and force a rebuild
per search.

### 3.4 Group 3 — genuinely time-dependent, decided at every relaxation

- `PhasingUnavailable`, the *"a window exists but the wait exceeds this search's
  horizon"* case.
- `DeadlineExceeded`.

`waitSec` is a staircase in departure time (`TRANSFER_MODEL.md` §8.2): there is no
single build-time answer to "is the wait acceptable," because the wait itself changes with
when you ask.

### 3.5 The consequence

`GraphEdge::referenceResult()` is a **cached sample**, evaluated once at a documented
reference time, for statistics and future visualisation only. **Dijkstra never reads it.**
Every relaxation calls `TransferModel::evaluate()` fresh, with the real vehicle
constraints and the real arrival time at the node being expanded. Group 1's permanence is
exactly what makes this affordable: only the wait/deadline and vehicle-capability checks
can produce a different verdict than the one baked into the graph at build time, so
nothing is being recomputed that could not, in principle, change.

---

## 4. `MissionGraph`

### 4.1 Adjacency list

```cpp
std::vector<GraphNode> nodes_;
std::vector<std::vector<EdgeId>> adjacency_;  // parallel to nodes_, by NodeId index
std::vector<GraphEdge> edges_;
```

`ALGORITHMS.md` §1.3's reasoning applies unchanged: after feasibility pruning the graph is
sparse, and both Dijkstra and (later) label-setting only ever enumerate a node's
neighbours — never ask "does edge u→v exist?", the one operation a matrix would win on.

### 4.2 Dense IDs, no hash map

`MissionGraph` — not the caller — assigns every `NodeId` and `EdgeId`, sequentially from
zero. `id.value()` is therefore usable directly as a `std::vector` index: `O(1)` lookup, no
hash map, no indirection. This is the same convention `Scenario` already uses for
satellites and vehicles (`DATA_MODEL.md` §10) — `MissionGraph` is the same pattern one
layer up.

### 4.3 Directed, no self-loops

`u → v` existing never implies `v → u` exists (`ALGORITHMS.md` §1.4: different burns, from
different speeds, possibly different feasibility). `addEdge(u, u, ...)` throws — a
transfer to one's own orbit is representable as a plane change between two *different*
same-radius nodes, never as a node pointing at itself.

---

## 5. Why No Separate "Graph Snapshot" Concept

`IMPLEMENTATION_ROADMAP.md`'s Phase 3 preparation raised the possibility of a "graph
snapshot" — orbital state plus connectivity plus evaluated metadata, at a point in time,
for dynamic replanning.

**Phase 3's answer: not needed, and here is the argument rather than an assertion.**

A snapshot is a hedge against state going stale. Two things about `MissionGraph` mean
nothing in it goes stale:

1. **Every node's `Orbit` is exact for all time** (§2.3) — it is not a position, it is the
   generator of positions. There is nothing to refresh.
2. **Every edge's existence is a Group 1 fact** (§3.2) — permanent, independent of when
   you look.

The only thing that *can* go stale is the cached `referenceResult` on an edge, and that is
already treated as non-authoritative (§3.5) — Dijkstra never trusts it, so its staleness
is harmless by construction, not by discipline.

**What a future replanning phase actually needs is narrower than a snapshot:** when new
information arrives (a debris detection, say), only the *edges whose Group 1 verdict could
change* need re-evaluating — and Group 1 is exactly the set of things that do not change
absent new information. `ALGORITHMS.md` §11.3's incremental-patch design already
anticipates this. A full snapshot concept would duplicate machinery `MissionGraph` already
provides for free.

---

## 6. Dijkstra — Earliest Arrival

### 6.1 The critical rule

```
for each outgoing edge (u -> v):
    result = transferModel.evaluate(orbit(u), orbit(v), arrivalTime(u), constraints)
```

**Never** `evaluate(orbit(u), orbit(v), startTimeSec, constraints)`. Reaching an
intermediate node at time `t` changes the departure time of every edge leaving it — that
is the entire content of the graph being time-dependent rather than merely large.

`TimeDependence.IndirectLegUsesArrivalTimeAtIntermediateNodeNotGlobalStart`
(`dijkstra_time_dependence_test.cpp`) exists specifically to fail if a future refactor gets
this backwards. It does not hardcode expected numbers; it compares Dijkstra's actual leg
against two independent, freshly-called `evaluate()` invocations — one at the correct time,
one at the wrong one — and asserts which it matches.

### 6.2 What the priority key is, and is not

The key is **earliest arrival time**, full stop — not a weighted combination, not
multi-objective. That is deliberate: `ALGORITHMS.md` §3.2 already frames Dijkstra as *"the
baseline, not the product"* — the argument for Pareto search (Phase 5+) is precisely that a
single scalar cannot express the time/fuel/risk trade-off
(`OPTIMIZATION_MODEL.md` §1.1). Phase 3 does not pre-empt that argument by quietly folding
extra objectives into Dijkstra's comparison.

### 6.3 The limitation this creates, found and kept rather than hidden

**Per-edge delta-v feasibility is sound to check here. A genuinely cumulative,
path-wide delta-v budget is not — and Phase 3 does not attempt it.**

The reasoning, in full, because it is easy to get wrong in a way that looks like a small
addition:

A per-edge check — *"is this one leg, by itself, within the vehicle's total budget"* — is
a fact about the edge alone, independent of which path reached it. Folding it into
single-label Dijkstra is sound.

A cumulative check — *"does the sum of every leg's delta-v so far, plus this one, exceed
the budget"* — is **not** safe to fold into single-label Dijkstra. A slower path that has
spent less fuel so far could be wrongly discarded in favour of a faster-but-thirstier one
that later turns out to be the only affordable option — and single-label search has
already thrown the alternative away by the time that becomes apparent. Enforcing a
cumulative resource constraint correctly requires keeping multiple non-dominated labels
per node, which is exactly what Pareto label-setting (Phase 5) already does. A second,
narrower version of that machinery here would duplicate it for a case Phase 5 handles
correctly and generally.

**Concretely:** `Dijkstra::findEarliestArrival` uses the vehicle's total Δv budget as
`maxDeltaVKms` on *every* relaxation, uniformly — never decremented as the route consumes
fuel. This is what makes the per-edge check sound (§6.3, first paragraph) and is exactly
why it cannot also catch the multi-leg overspend case.

`Dijkstra.CumulativeBudgetIsNotEnforcedByDesign` proves this is real: two 10° plane changes
at 500 km (1.326965 km/s each — the same value `PlaneChange` validated in Phase 2), a
2.0 km/s vehicle budget. Each leg individually fits (1.326965 ≤ 2.0); together they do not
(2.653930 > 2.0). The search reports the route as **found**. `totalObjectives().fuel`
honestly reports 1.33 — over budget — because it is computed the same way any caller would
(`compose()` across the actual legs); the search simply did not use that fact to reject the
route. This is a **tested boundary**, not an undiscovered gap: if Phase 5 or a future
change to Dijkstra silently "fixes" this by adding an unsound single-label cumulative
check, this test's assertion that the route is *found* despite the overspend will start
failing, and that failure is the signal to reach for multi-label search instead of a
patch here.

### 6.4 Complexity

Structurally `O((V+E) log V)` with a binary heap and lazy deletion — same shape as
`ALGORITHMS.md` §3.5's static-graph analysis. But every relaxation calls
`TransferModel::evaluate()`, so the practical cost is

```
O((V + E) log V · C)
```

where `C` is the cost of one transfer evaluation. `C` is a handful of closed-form
trigonometric evaluations (Hohmann + plane change + one phasing search) — no iteration, no
root-finding — so it is cheap, but it is not the free, amortised-away weight lookup a
static-graph Dijkstra enjoys. `BENCHMARK_PLAN.md` measures `C`'s real contribution once
there is a scenario large enough for it to matter; no number is asserted here that has not
been measured.

### 6.5 Lazy deletion and lookahead skip

Two efficiency choices, both already-established ORCA patterns rather than new ones:

- **Lazy deletion** (`ALGORITHMS.md` §2.4): stale heap entries are skipped on pop rather
  than removed on update, avoiding an indexed decrease-key heap.
- **Finalised-node skip**: an edge into an already-finalised node is skipped before
  calling `evaluate()` at all — cheap to check, and it is exactly the nodes near the sink
  that accumulate the most redundant incoming edges as the search fans out.

---

## 7. `PathResult` — Route Reconstruction

```cpp
static PathResult notFound(source, target, startTimeSec, metrics);
static PathResult found(source, target, startTimeSec, legs, metrics);   // legs non-empty
static PathResult trivial(sourceAndTarget, startTimeSec, metrics);      // source == target
```

Named factories, mirroring Phase 1's `MissionPlan::makeFeasible`/`makeInfeasible`: reaching
"not found" cannot require supplying a route, and reaching "found" cannot happen without
one. `found()`'s constructor rejects an empty `legs` vector outright — the invariant
"found implies non-empty legs" is structural, not merely documented, with the trivial
`source == target` case handled by its own named factory rather than as a zero-leg special
case of `found()`.

**Each `PathLeg` carries the `TransferResult` Dijkstra actually computed** — at that leg's
real departure time — never `GraphEdge::referenceResult()` (§3.5). `totalObjectives()`
composes those real per-leg results via the canonical `compose()` rule
(`ObjectiveVector.hpp`): additive for time, fuel and distance; independent-survival for
risk. No accumulation rule is invented locally — `ALGORITHMS.md` §5.6's monotonicity
argument, which the eventual Pareto search depends on, applies to this total exactly as it
does everywhere else risk is composed.

---

## 8. Verification Strategy

### 8.1 Independent oracle

`dijkstra_oracle_test.cpp` enumerates every *simple* path (bounded by a `visited` set,
so a graph with cycles still terminates) between source and target, evaluating each
sequentially — leg 1 at `t₀`, leg 2 at leg 1's arrival, and so on — using
`TransferModel::evaluate()` called directly. It shares no logic with `Dijkstra.cpp` beyond
the transfer model both are required to use as ORCA's single source of transfer truth.

Restricting the oracle to simple paths is legitimate, not merely convenient: every
objective composed along a path is non-negative (`ObjectiveVector`'s invariant), so
revisiting a node can never produce a cheaper route than the best simple path already
found. This was verified directly on a seven-node graph containing a real cycle
(`n3 → n1`) before being trusted: Dijkstra and the independent oracle agree exactly, to
the same route, on that graph.

### 8.2 Why the phase values in the tests are boring numbers

An earlier draft of the "does Dijkstra use the right departure time" test tried to
construct a rendezvous window that opened *exactly* at the intermediate node's arrival
time — a clean 0-vs-full-synodic-period contrast. It was discarded: the exact alignment
instant is a knife-edge, and a Python-derived phase literal landed a few ulp to the wrong
side of it when checked against the real C++ implementation, flipping the "correct"
answer from a 0 s wait to a ~188,000 s wait. That is not a usable regression test — it
would be one bad compiler upgrade away from silently testing the wrong thing.

Every phase value that ended up in the committed tests is instead a simple, arbitrary
number (`0.0`, `1.0`, `2.0`, `3.0` radians), checked to produce a wide, perturbation-stable
margin before being trusted (`tests/algorithms/dijkstra_time_dependence_test.cpp`'s header
comment). This is recorded here because it is a real methodological finding: **when a
test's premise is an exact alignment between two independently-computed values, prefer a
value verified directly against the implementation under test, or better, avoid the exact
alignment entirely** — a generic, unremarkable input is more robust than a cleverly
special-cased one.

---

## 9. What Phase 3 Does Not Build

- No spatial index (`ALGORITHMS.md` §9 — deferred; `O(V²)` candidate generation is
  unmeasured against a real scenario size, so pruning it now would be optimising a cost
  that has not been shown to matter).
- No A* (Phase 4 — since implemented, sharing this file's `MissionGraph`/`GraphEdge`
  model and the critical time-dependence rule in §6.1 unchanged; see `ALGORITHMS.md` §4
  and `HEURISTIC_DESIGN.md` for the search itself and its heuristic).
- No Pareto/multi-objective search, and specifically no attempt to make Dijkstra
  multi-objective by folding a cumulative resource constraint into it (§6.3).
- No Union-Find reachability partitioning (`PRD.md` §10's anti-requirement: conditional on
  a measured benefit, not included speculatively).
- No frontend. `GraphNode`/`GraphEdge` expose everything the future Network screen needs
  (source, target, feasibility, per-objective cost, engine-computed layout is a later
  addition) without any rendering code in this layer.
