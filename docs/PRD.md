# ORCA — Product Requirements Document

**Autonomous Orbital Rescue Mission Planning Using Multi-Objective Optimization**

Version 1.0 · Phase 0 · 2026-09-02

---

## 1. Problem

A spacecraft in Earth orbit suffers a failure — propulsion loss, power fault, collision
damage. A limited fleet of rescue vehicles is available. Someone must decide, quickly:

**which vehicle goes to which casualty, by which route, starting when.**

Every part of that decision is contested:

- **Time.** A casualty has a *survival window*. Arriving after it closes is worth nothing.
- **Fuel.** A rescue vehicle carries finite Δv. Fuel spent on this mission is unavailable
  for the next one. In a multi-emergency scenario, the greedy-fastest choice can strand the
  fleet.
- **Risk.** Routes pass near debris. Faster routes are often riskier.
- **Contention.** Two emergencies, one vehicle in range: choosing is unavoidable.
- **Change.** Orbits move, debris is detected mid-transfer, casualties deteriorate. A plan
  computed at *t=0* may be wrong at *t=200*.

These objectives genuinely conflict, and — critically — **the conflict is not manufactured**.
It falls out of orbital mechanics. Repository audit §4.2 measured the synodic period between
a 500 km and an 800 km orbit at **25.4 hours**: the fuel-optimal departure phase recurs about
once a day, while survival windows run tens of minutes. So the planner must continuously
price *"depart now at a bad phase and pay Δv"* against *"wait for a better phase and pay
time"*. There is no single correct answer to that; there is a **trade-off surface**.

**That surface is what ORCA computes, visualises, and defends.**

### 1.1 Why single-objective planning is insufficient

The obvious approach — collapse time, fuel and risk into one weighted score and run
Dijkstra — is what ORCA benchmarks *against*, not what it does. Collapsing first has two
defects:

1. **The weights are a guess made before the options are known.** A planner that picks
   weights up front cannot discover that a 4% time penalty would have bought a 40% fuel
   saving, because the scalar score never exposes the exchange rate.
2. **It hides feasibility structure.** The fastest route may be infeasible on fuel; a scalar
   score can rank an infeasible option above a feasible one.

ORCA instead computes the **Pareto-optimal set first** and applies preference *afterwards*.
Weights become a UI control over an already-computed frontier, not a hidden premise.

---

## 2. Motivation

The academic motivation is that this problem is an unusually *honest* carrier for
data-structures work. The graph is not decorative:

- Nodes and edges have real referents (orbital bodies; feasible transfers).
- Edge weights come from validated physics, not random numbers.
- Edge cost is **time-dependent** — the cost of A→B depends on when you depart — which makes
  the shortest-path work substantive rather than textbook recital.
- The multi-objective structure demands **label-setting with dominance pruning**, a genuine
  algorithm with a real complexity story, not a sort.
- Multi-vehicle contention is an **assignment problem**, giving greedy-vs-optimal something
  real to disagree about.

The engineering motivation is that a mission planner is only trustworthy if you can *see* why
it chose what it chose. ORCA is built so the algorithm is inspectable: the frontier, the
frontier's dominated points, the search frontier of Dijkstra and A*, the labels pruned by
dominance — all visible.

---

## 3. Target Users

| User | Need | Primary surfaces |
|---|---|---|
| **Evaluator / examiner** | Verify the DSA is real, correct, and understood | Algorithm Lab, Network, `cpp/algorithms/`, `VIVA_GUIDE.md` |
| **Student author** | Build, debug, defend, extend the system | CLI, tests, benchmarks, logs |
| **Simulated mission operator** (the in-fiction persona the UI is designed for) | Understand the situation and the recommendation fast | Mission Control, Mission Intelligence |
| **Researcher** (future) | Compare planning strategies with statistics | Benchmark mode, experiment runner, exported CSV |

The operator persona shapes the *visual* design; the evaluator persona shapes the
*structural* design. Where they conflict, the evaluator wins — ORCA is a DSA project first.

---

## 4. Product Vision

> A standalone, offline, deterministic orbital-rescue simulator whose planning decisions are
> computed entirely by a C++20 engine, and whose interface makes those decisions —
> and the algorithms that produced them — visible and explicable.

Three commitments follow from that sentence and are treated as inviolable:

1. **The C++ engine is the sole source of truth** for state and every computed value.
2. **The engine is useful without the UI.** It runs headless, plans, benchmarks, and exports.
3. **Nothing is faked.** No mock telemetry, no invented benchmark numbers, no frontend
   recomputation of an engine value.

---

## 5. Goals

**G1 — Real multi-objective planning.** Compute a genuine Pareto set over
(time, fuel, risk, distance); apply preference only afterwards.

**G2 — Real DSA, visibly located.** Graph, priority queue, Dijkstra, A*, multi-objective
label-setting, dominance filtering, assignment — implemented in C++20 under `cpp/algorithms/`,
each unit-testable in isolation.

**G3 — Dynamic replanning.** Detect environment change mid-mission, rebuild affected graph
edges, replan, switch trajectory, and show the switch happening.

**G4 — Reproducibility.** Seed + config ⇒ byte-identical scenario and identical planning
decisions, every run, on every machine.

**G5 — Measured comparison.** Benchmark ORCA-Pareto against Dijkstra, A*, and greedy
strategies on runtime *and* mission-outcome quality, at sizes from 25 to 1000 objects.

**G6 — Explicability.** Every recommendation carries a machine-generated rationale sourced
from engine data.

**G7 — Interface quality.** A mission-control interface that reads as engineering software:
Earth-dominant, restrained, information-dense.

---

## 6. Non-Goals

Explicitly out of scope. Each is a real limitation, stated so it is never oversold.

| Non-goal | Why |
|---|---|
| **Flight-grade orbital mechanics** | ORCA uses circular-orbit, impulsive-burn, patched-Hohmann approximations. Not validated for operational use. |
| **Real satellite catalogue / TLE / SGP4** | Deferred to `RESEARCH_EXTENSION.md`. Scenarios are synthetic and seeded. |
| **Physically calibrated risk** | The risk model is a *simulation* model with documented, tunable factors. It is not a validated collision-probability estimate. |
| **Exact propellant mass** | Fuel is **relative Δv budget fraction**, not kilograms of propellant. |
| **Continuous-thrust / low-thrust transfers** | Impulsive only. |
| **N-body, drag, J2, SRP perturbations** | Two-body only, circular orbits. |
| **Multi-user, auth, database, cloud** | Single-user, local, file-backed, offline. |
| **Machine learning** | No ML in the core product. Listed only as future research. |
| **Mobile** | Desktop-class viewport. |

---

## 7. Use Cases

**UC-1 — Single emergency, single responder.**
Failure occurs → emergency enters the priority queue → graph is built → Pareto set computed →
profile selects a plan → vehicle transfers → rendezvous → analytics update.

**UC-2 — Contention.** Two simultaneous emergencies, two vehicles. Greedy assignment
sends the nearest vehicle to the higher-priority casualty, stranding the second. Hungarian
assignment finds the globally cheaper pairing. *Both are computed and shown.*

**UC-3 — Dynamic replanning.** Mid-transfer debris alert raises risk on the active route's
remaining edges. Replanner re-runs, selects an alternative, trajectory visibly switches.

**UC-4 — Preference exploration.** Operator drags weight sliders in the Optimization Lab;
recommendation moves across a fixed Pareto front. *The front does not recompute — only the
selection does.* This makes the two-stage design tangible.

**UC-5 — Algorithm inspection.** Evaluator opens Algorithm Lab, steps Dijkstra and A* on the
same graph, sees A* expand fewer nodes, then sees label-setting generate and prune labels.

**UC-6 — Benchmark run.** `orca_engine bench --sizes 25,50,100,250,500,1000 --seeds 10`
produces a CSV of runtimes and outcome metrics. No UI involved.

**UC-7 — Infeasibility.** No vehicle can reach a casualty within its survival window. ORCA
must report *no feasible plan* with the binding constraint named — not crash, not fake one.

---

## 8. Functional Requirements

Requirements are testable. `TEST_PLAN.md` references these IDs.

### 8.1 Simulation

| ID | Requirement |
|---|---|
| FR-S1 | Advance a simulation clock in fixed steps, with pause/resume/step and speed multipliers |
| FR-S2 | Propagate circular orbits analytically from seeded initial phases |
| FR-S3 | Maintain satellites, rescue vehicles, debris, emergencies, missions as engine-owned state |
| FR-S4 | Process a timestamped event queue in chronological order |
| FR-S5 | Generate emergencies deterministically from seed + scenario schedule |
| FR-S6 | Execute missions: transfer progress, fuel decrement, rendezvous, success/failure |
| FR-S7 | Close survival windows and mark unrescued casualties failed |
| FR-S8 | Emit a state snapshot at a configurable rate |

### 8.2 Graph

| ID | Requirement |
|---|---|
| FR-G1 | Weighted **directed** graph, adjacency-list representation |
| FR-G2 | Nodes: satellites, rescue vehicles, staging waypoints |
| FR-G3 | Edges: feasible transfers, each carrying a 4-vector `{time, fuel, risk, distance}` |
| FR-G4 | Edge cost is a **function of departure time**, not a constant |
| FR-G5 | Prune infeasible edges (Δv beyond vehicle budget, or arrival past window close) |
| FR-G6 | Rebuild or incrementally patch the graph on environment change |
| FR-G7 | Report `V`, `E`, build time, and rejection counts |

### 8.3 Algorithms

| ID | Requirement |
|---|---|
| FR-A1 | Dijkstra over any single selected objective, binary-heap priority queue |
| FR-A2 | A* with a documented admissible heuristic; identical optimal cost to Dijkstra |
| FR-A3 | Multi-objective label-setting producing the non-dominated label set |
| FR-A4 | Pareto dominance as a standalone, independently-tested predicate |
| FR-A5 | Emergency priority queue ordered by computed priority |
| FR-A6 | Greedy assignment |
| FR-A7 | Hungarian (optimal) assignment, `O(n³)` |
| FR-A8 | Spatial index for debris proximity queries (avoids `O(n²)` risk evaluation) |
| FR-A9 | Every algorithm reports metrics: nodes expanded, edges relaxed, PQ ops, labels generated/pruned, wall time |

### 8.4 Optimization

| ID | Requirement |
|---|---|
| FR-O1 | Represent every candidate as a 4-objective vector |
| FR-O2 | Compute the Pareto set **before** any scalarisation |
| FR-O3 | Normalise objectives against the frontier's own ideal/nadir |
| FR-O4 | Profiles: `FASTEST`, `FUEL_SAVING`, `SAFETY_FIRST`, `BALANCED`, `CUSTOM` |
| FR-O5 | Select from a **fixed** frontier when weights change — no re-search |
| FR-O6 | Deterministic, documented tie-breaking |
| FR-O7 | Enforce hard constraints (fuel budget, survival window) as feasibility, not penalty |

### 8.5 Planning

| ID | Requirement |
|---|---|
| FR-P1 | Plan a mission for a given (vehicle, casualty) pair |
| FR-P2 | Assign vehicles to casualties across the full emergency set |
| FR-P3 | Monitor active missions and trigger replanning on material change |
| FR-P4 | Replan only when the alternative beats the current plan by a configured margin (hysteresis) |
| FR-P5 | Emit a structured, data-sourced explanation for every recommendation |
| FR-P6 | Report infeasibility with the binding constraint identified |

### 8.6 Interface

| ID | Requirement |
|---|---|
| FR-U1 | 3D Earth with LEO/MEO/GEO orbit rings and correctly-scaled markers |
| FR-U2 | Render satellites, vehicles, debris, emergencies with the status colour language |
| FR-U3 | Render candidate / recommended / active / completed / rejected trajectories distinctly |
| FR-U4 | Mission Intelligence panel driven entirely by engine output |
| FR-U5 | Pareto front chart: dominated points, frontier, recommendation |
| FR-U6 | Weight sliders that re-select without re-searching |
| FR-U7 | Graph view of the mission network |
| FR-U8 | Algorithm Lab stepping Dijkstra / A* / label-setting |
| FR-U9 | Event timeline |
| FR-U10 | Analytics dashboard |
| FR-U11 | Graceful, specific errors for every failure in `ARCHITECTURE.md` §11 |

### 8.7 Analytics, Benchmark, Reproducibility

| ID | Requirement |
|---|---|
| FR-N1 | Track emergencies, successes, failures, mean rescue time / fuel / risk, replan count |
| FR-N2 | Track algorithm runtime, nodes explored, labels generated and pruned |
| FR-N3 | Benchmark mode at sizes 25–1000 with warm-up and repetitions |
| FR-N4 | Export results as CSV and JSON |
| FR-N5 | Same seed + same config ⇒ identical scenario and identical decisions |
| FR-N6 | Experiment matrix: scenario × strategy × seed |

---

## 9. Non-Functional Requirements

| ID | Requirement | Target |
|---|---|---|
| NFR-1 | Plan latency, 100-object scenario | < 100 ms |
| NFR-2 | Snapshot rate sustained | ≥ 10 Hz |
| NFR-3 | Frontend render | ≥ 30 fps at 500 objects |
| NFR-4 | Graph build, 500 objects | < 250 ms |
| NFR-5 | Determinism | Bit-identical across runs on one platform |
| NFR-6 | Offline | Zero network calls at runtime |
| NFR-7 | Engine portability | macOS + Linux, no OS-specific APIs in core |
| NFR-8 | `.cpp` file size | Mostly < 300 lines |
| NFR-9 | Core test coverage | Every algorithm independently unit-tested |
| NFR-10 | Engine independence | Builds and passes tests with no Node and no browser present |

NFR-1/2/4 are **budgets to be measured**, not claims. Actual figures land in
`BENCHMARK_PLAN.md` outputs once Phase 1+ produces them.

---

## 10. DSA Requirements

Each structure is listed with its *justification*, because "we used a heap" is not a defence.

| Structure | Where | Why it is the right choice |
|---|---|---|
| Adjacency-list weighted digraph | `graph/MissionGraph` | Transfer graphs are sparse (feasibility pruning removes most pairs); adjacency list is `O(V+E)` space vs `O(V²)`. Directed because A→B and B→A differ in Δv and feasibility |
| Binary heap (`std::priority_queue`) | Dijkstra, A*, event queue, emergency queue | `O(log n)` push/pop; the standard choice for sparse-graph Dijkstra |
| Multi-objective label set | `algorithms/ParetoSearch` | Pareto-optimal paths are not a single value per node; each node holds a *set* of non-dominated labels |
| Dominance predicate | `algorithms/Dominance` | The pruning rule; isolated so it is testable and provably correct on its own |
| Hash map | ID → entity lookup | `O(1)` resolution during graph build and snapshot assembly |
| Uniform-grid spatial index | `algorithms/SpatialIndex` | Debris proximity is a range query. Brute force is `O(V·D)`; grid makes risk evaluation tractable at 1000 objects |
| Cost matrix + Hungarian | `algorithms/HungarianAlgorithm` | Optimal assignment in `O(n³)`; the counterexample to greedy |
| Union-Find *(optional)* | reachability partitioning | Detects casualties in disconnected graph components — instant infeasibility without running a search. Included only if it earns its place |

**Anti-requirement:** no structure may be inserted where a simpler one is clearly better. If
Union-Find does not earn its place, it is omitted and the omission is documented.

---

## 11. Acceptance Criteria

### 11.1 MVP (Phase 1 target)

- [ ] Engine builds; all core unit tests pass
- [ ] Seeded scenario loads; orbits propagate; snapshots emit
- [ ] Graph builds with time-dependent 4-vector edges
- [ ] Dijkstra and A* return **identical optimal cost**; A* expands **strictly fewer** nodes on a documented case
- [ ] Label-setting returns a verified-correct Pareto set on a hand-checked fixture
- [ ] Profile selection picks different frontier points for different weights
- [ ] One emergency planned and executed end-to-end, headless
- [ ] Same seed ⇒ identical output across 10 runs
- [ ] CSV export works

### 11.2 Full product

- [ ] Mission Control renders live engine state
- [ ] Pareto front chart with working profile switching
- [ ] Dynamic replanning visibly occurs in the demo scenario
- [ ] Algorithm Lab steps all three searches
- [ ] Greedy vs Hungarian shown to differ on a constructed scenario
- [ ] Benchmarks run 25→1000 and export real numbers
- [ ] Every error case in `ARCHITECTURE.md` §11 handled with a specific message
- [ ] Engine still passes all tests with frontend and backend deleted

### 11.3 Explicitly *not* acceptance criteria

Fabricated benchmark numbers, screenshot-only features, or any frontend-computed value
presented as engine output. A feature that only works in a recorded demo is not done.

---

## 12. Feature Tiers

**Tier 1 — Core (required):** simulation loop, orbital propagation, event queue, graph
build, Dijkstra, A*, label-setting, dominance, profiles, greedy assignment, mission
execution, metrics, CLI, determinism.

**Tier 2 — Product (expected):** Mission Control, Earth visualisation, trajectories, Mission
Intelligence, Pareto chart, Optimization Lab, event timeline, analytics, dynamic replanning,
Hungarian assignment, benchmark mode.

**Tier 3 — Advanced (if schedule allows):** Algorithm Lab stepping, Network view, Scenario
Builder, Mission History, presentation mode, spatial index, Union-Find, Electron packaging.

**Tier 4 — Research (future):** `RESEARCH_EXTENSION.md`. Not built now.

Tier 3 items are cut before Tier 1 or 2 items are compromised. The Hungarian algorithm sits
at the Tier 2/3 boundary deliberately: valuable, but not worth destabilising the core.

---

## 13. Presentation Mode

A guided path through the demo scenario (`configs/demo.json`, seed fixed) that steps the
narrative: emergency → graph → search → frontier → recommendation → transfer → debris alert →
replan → rendezvous → analytics. It is a *camera and pacing script over a real simulation
run*, never a recording. Timing is configurable.

---

## 14. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| CMake absent | Blocks all engine work | Install before Phase 1 (audit §3.2) |
| Label-setting blows up combinatorially | Planner stalls | ε-dominance, per-node label cap, objective-count cap. Measured in benchmarks |
| Time-dependent costs break Dijkstra optimality | Wrong results | FIFO/non-overtaking property documented and **tested** (`ALGORITHMS.md` §3.3) |
| Three.js scope creep | Schedule loss | Visual restraint is a stated requirement; Tier 3 cut first |
| Frontend drifts into recomputing engine values | Violates core principle | Single-source-of-truth rule + review checklist (`ARCHITECTURE.md` §10) |
| Scope: 9 screens is a lot | Incomplete product | Tiering; Mission Control is the only screen that must be excellent |

---

## 15. Research Direction

> **How does Pareto-based multi-objective rescue planning perform, relative to greedy and
> single-objective planning, under constrained orbital rescue resources?**

The architecture supports answering this: multiple seeds, multiple scenarios, multiple
strategies, repeated trials, statistical summaries, exported datasets. `BENCHMARK_PLAN.md`
defines the protocol. **No results are asserted anywhere until measured.**
