# ORCA — System Design

Version 1.2 · updated after Phase 4 · 2026-09-03

> **Phase 4.** `algorithms/Dijkstra · AStar` in the diagrams below is now both algorithms,
> as this document already anticipated — A* was added alongside Dijkstra with no change to
> where it sits or what it depends on. See `ALGORITHMS.md` §4 and `HEURISTIC_DESIGN.md` for
> the search and its heuristic; `GRAPH_MODEL.md` §6.1 for the time-dependence rule both
> algorithms share unchanged.

Ten diagrams covering the system end to end. `ARCHITECTURE.md` says *what the layers are*;
this document says *how the work actually flows through them*.

---

## 1. Complete System

```mermaid
flowchart TB
    subgraph BROWSER["Browser"]
        UI["React pages<br/>MissionControl · Planner · OptLab<br/>Network · AlgoLab · Analytics"]
        R3F["react-three-fiber scene<br/>Earth · orbits · markers · trajectories"]
        ST["Zustand store<br/>snapshot · events · UI state"]
        WSC["WS client + REST client"]
        UI --> ST
        R3F --> ST
        ST --> WSC
    end

    subgraph NODE["Node bridge :4000"]
        EXP["Express REST<br/>/api/sim /api/plan /api/scenario"]
        WSS["ws server /ws"]
        SUP["EngineSupervisor<br/>spawn · health · restart policy"]
        CODEC["NDJSON codec<br/>reqId correlation"]
        COAL["Snapshot coalescer<br/>latest-wins"]
        EXP --> CODEC
        CODEC --> SUP
        SUP --> COAL
        COAL --> WSS
    end

    subgraph ENGINE["orca_engine (C++20 child process)"]
        SERVE["app/ServeLoop"]
        SE["simulation/SimulationEngine"]
        EQ["EventQueue (min-heap by time)"]
        PROP["OrbitalPropagator"]
        EXEC["MissionExecutor"]
        COORD["planning/RescueCoordinator"]
        MP["planning/MissionPlanner"]
        GB["graph/GraphBuilder"]
        MG["graph/MissionGraph"]
        PS["algorithms/ParetoSearch"]
        DIJ["algorithms/Dijkstra · AStar"]
        PF["optimization/ParetoFront + Profile"]
        MET["analytics/MetricsCollector"]

        SERVE --> SE
        SE --> EQ
        SE --> PROP
        SE --> EXEC
        SE --> COORD
        COORD --> MP
        MP --> GB
        GB --> MG
        MP --> PS
        MP --> DIJ
        PS --> MG
        DIJ --> MG
        PS --> PF
        MP --> MET
    end

    WSC <-->|"WebSocket + HTTP"| EXP
    WSC <-->|"WebSocket"| WSS
    SUP <-->|"NDJSON / stdin·stdout"| SERVE

    style ENGINE fill:#0d1b2a,color:#e0e1dd
    style NODE fill:#1b263b,color:#e0e1dd
    style BROWSER fill:#22333b,color:#e0e1dd
```

---

## 2. Emergency Detection

```mermaid
sequenceDiagram
    participant CLK as SimulationClock
    participant SE as SimulationEngine
    participant EQ as EventQueue
    participant EG as EmergencyGenerator
    participant PQ as Emergency PQ
    participant CO as RescueCoordinator

    CLK->>SE: advance(Δt) → t = 412.0
    SE->>EQ: popDue(t)
    EQ-->>SE: SatelliteFailure{SAT-017, PROPULSION}

    SE->>EG: onFailure(SAT-017, PROPULSION, t)
    Note over EG: severity from failure kind + subsystem<br/>survivalWindow from seeded model<br/>deadline = t + window
    EG-->>SE: Emergency{E-003, sev=CRITICAL, deadline=2332.0}

    SE->>PQ: push(E-003, priority=f(severity,urgency,feasibility))
    Note over PQ: max-heap on computed priority<br/>NOT insertion order

    SE->>CO: notifyPending()
    CO->>PQ: top()
    PQ-->>CO: E-003
    CO->>CO: plan (see §4)
    SE-->>SE: emit event → snapshot stream
```

**Design note.** Priority is *recomputed*, not static: urgency rises as the deadline
approaches, so a moderate emergency near its window close can overtake a fresh critical one.
The priority queue is rebuilt when the ordering key changes — see `ALGORITHMS.md` §6 for why
this is preferred over a decrease-key heap here.

---

## 3. Graph Construction

> **Revised in Phase 3.** The diagram and ordering below reflect what `GraphBuilder`
> actually implements, which differs from this section's original Phase 0 sketch in two
> load-bearing ways, both explained in full in `GRAPH_MODEL.md` §3:
>
> 1. **The graph is built once per scenario, not once per `(casualty, t₀)`.** It is
>    vehicle-agnostic and reusable across every search. "Vehicle source" and "casualty
>    sink" are not stored node properties — they are which two node IDs a *caller* of
>    Dijkstra happens to pass in. A node's kind is just `Satellite` or `RescueVehicle`
>    (`GRAPH_MODEL.md` §2.1); which one currently needs rescuing is a query against the
>    live entity (`Satellite::requiresRescue()`), never a cached graph fact.
> 2. **Δv, fuel and deadline cannot be decided at build time**, because build time knows
>    neither the vehicle nor the departure time a search will actually use. Only
>    `InvalidOrbit`, the *permanent* case of `PhasingUnavailable` (no relative drift and
>    never aligned), and the two per-leg policy ceilings (`RiskExceeded`,
>    `DistanceExceeded`) are decidable once, permanently. Everything else moves to
>    Dijkstra, evaluated at the real departure time with the real vehicle's budget.

```mermaid
flowchart TB
    START["GraphBuilder::build(scenario, transferModel, options)"] --> NODES

    subgraph NODES["1 · Nodes — one graph, all eligible entities"]
        N1["non-terminal satellites"]
        N2["non-disabled rescue vehicles"]
    end

    NODES --> PAIRS["2 · Candidate ordered pairs (u,v), u≠v — O(V²)"]
    PAIRS --> COST["3 · TransferModel.evaluate(u, v, t_ref, referenceConstraints)<br/>capability + wait/deadline UNBOUNDED"]

    COST --> F1{"orbits valid?"}
    F1 -->|no| REJ1["reject: INVALID_ORBIT"]
    F1 -->|yes| F2{"rendezvous window<br/>exists at all?"}
    F2 -->|no, permanently| REJ2["reject: PHASING_UNAVAILABLE"]
    F2 -->|yes| F3{"risk ≤ builder's<br/>per-leg ceiling?"}
    F3 -->|no| REJ3["reject: RISK_EXCEEDED"]
    F3 -->|yes| F4{"arc length ≤ builder's<br/>per-leg ceiling?"}
    F4 -->|no| REJ4["reject: DISTANCE_EXCEEDED"]
    F4 -->|yes| ADD["add directed edge u→v<br/>cache TransferResult as a reference sample"]

    ADD --> DONE["MissionGraph<br/>V, E, rejection counts by reason (GraphBuildStats)"]
    REJ1 --> DONE
    REJ2 --> DONE
    REJ3 --> DONE
    REJ4 --> DONE

    style COST fill:#1e3a5f,color:#fff
    style ADD fill:#2d5016,color:#fff
```

Deferred to Dijkstra, at search time, per relaxation (`GRAPH_MODEL.md` §3.3–§3.4):
`DeltaVExceeded`, `FuelInsufficient` (checked against the specific vehicle's total budget),
`PhasingUnavailable`'s *wait-exceeds-this-search's-horizon* case, and `DeadlineExceeded`.

Three things make this more than a nested loop:

1. **Edges are directed and asymmetric.** `u→v` and `v→u` differ in Δv (raising vs lowering
   orbit) and, once a vehicle is known, in feasibility (the vehicle at `u` has a different
   fuel budget than whatever occupies `v`).
2. **Rejection counts are kept by reason** (`GraphBuildStats`, one field per
   `TransferFailureReason`, `PhasingUnavailable` never folded into a generic bucket). This
   turns "no feasible path" into an actionable message (`ARCHITECTURE.md` §11) and gives
   the Network screen something honest to show.
3. **The check order matches `TransferFeasibility::checkFeasibility` exactly**
   (`TRANSFER_MODEL.md` §12.1): orbit validity, then reachability, then policy limits —
   never fuel or deadline first, because those are not this layer's decision to make.

No spatial index is used for the risk term yet (`GRAPH_MODEL.md` §9) — that arrives with
Phase 9's debris model. At Phase 3's scenario sizes, `O(V²)` candidate evaluation is
unmeasured against a real workload, so pruning it now would be optimising a cost that has
not been shown to matter.

---

## 4. Route Planning (single mission)

> **Corrected in Phase 3.** `GraphBuilder::build()` takes a `Scenario`, not a
> `(casualty, t₀)` pair — the graph is scenario-wide and reusable (§3 above). A real
> `MissionPlanner` (Phase 6+) either reuses an already-built graph or rebuilds it once
> after a state change, independently of which casualty it is about to plan for; `casualty`
> and `t₀` are supplied to the *search* (`PS`, below), not to `GB`.

```mermaid
sequenceDiagram
    participant CO as RescueCoordinator
    participant MP as MissionPlanner
    participant GB as GraphBuilder
    participant FE as FeasibilityEvaluator
    participant PS as ParetoSearch
    participant PF as ParetoFront
    participant EX as MissionExplanation

    CO->>MP: planFor(casualty=SAT-017, fleet, t₀)
    MP->>FE: prefilter(fleet, casualty)
    FE-->>MP: [RV-02, RV-03]  (RV-01 out of Δv range)

    MP->>GB: build(scenario)  — reused across casualties, rebuilt only on state change
    GB-->>MP: MissionGraph{V=28, E=173, rejected=583}

    alt graph empty or casualty unreachable
        MP-->>CO: infeasible{reason, bindingConstraint}
    else
        MP->>PS: search(graph, sources=[RV-02,RV-03], sink=SAT-017, t₀)
        Note over PS: multi-objective label-setting<br/>dominance pruning per node
        PS-->>MP: labels: 412 generated, 371 pruned, 8 non-dominated

        MP->>PF: fromLabels(8 labels)
        PF-->>MP: ParetoFront + ideal/nadir vectors
        MP->>EX: explain(front, chosen, alternatives)
        EX-->>MP: rationale
        MP-->>CO: PlanResult{front, recommended, metrics, explanation}
    end
```

Note `MP` returns the **whole front**, not just a winner. Selection happens next, separately.

---

## 5. Pareto Search (label-setting internals)

```mermaid
flowchart TB
    INIT["for each source s:<br/>push Label{node=s, cost=0⃗, pred=∅}"] --> LOOP

    LOOP{"open heap<br/>empty?"} -->|yes| DONE["collect labels at sink"]
    LOOP -->|no| POP["pop lexicographically<br/>smallest label L"]

    POP --> CHK{"L still in<br/>node's non-dominated set?"}
    CHK -->|no, superseded| LOOP
    CHK -->|yes| EXPAND["for each edge L.node → v"]

    EXPAND --> EVAL["cost' = L.cost ⊕ w(edge, arrivalTime(L))"]
    EVAL --> FEAS{"feasible?<br/>fuel, deadline"}
    FEAS -->|no| LOOP
    FEAS -->|yes| DOM{"dominated by any<br/>existing label at v?"}

    DOM -->|yes| PRUNE["discard<br/>prunedCount++"]
    DOM -->|no| INSERT["remove labels at v now<br/>dominated by cost'<br/>insert new label<br/>push to heap"]

    PRUNE --> LOOP
    INSERT --> LOOP
    DONE --> FRONT["non-dominated set at sink<br/>= Pareto front"]

    style DOM fill:#5f1e1e,color:#fff
    style INSERT fill:#2d5016,color:#fff
```

The `CHK` step (lazy deletion) matters: rather than removing superseded labels from the heap,
they are skipped on pop. This is the standard trick that keeps the heap a plain
`std::priority_queue` with no decrease-key support. See `ALGORITHMS.md` §5.

---

## 6. Final Plan Selection

```mermaid
flowchart LR
    subgraph STAGE1["STAGE 1 — objective, weight-free"]
        F["Pareto front<br/>8 non-dominated plans"]
    end

    subgraph STAGE2["STAGE 2 — preference"]
        N["Normalize each objective<br/>against front's ideal/nadir"]
        W["Apply profile weights<br/>w_time, w_fuel, w_risk, w_dist"]
        S["score = Σ wᵢ · normᵢ"]
        T["Deterministic tie-break:<br/>risk ↑, then time ↑, then id"]
        SEL["Recommended plan"]
        N --> W --> S --> T --> SEL
    end

    F --> N
    SLIDER["Weight sliders"] -.->|"re-runs STAGE 2 only"| W

    style STAGE1 fill:#1e3a5f,color:#fff
    style STAGE2 fill:#2d5016,color:#fff
```

**This separation is the intellectual centre of ORCA.** Moving a slider is `O(|front|)` —
a rescore of at most a few dozen points, with no graph search. That is why the Optimization
Lab feels instant, and it is the concrete payoff of not scalarising early.

---

## 7. Assignment Optimization (multi-emergency)

```mermaid
flowchart TB
    IN["m emergencies × n vehicles"] --> MAT["Build cost matrix C<br/>C[i][j] = scalarised plan cost<br/>(∞ if infeasible)"]

    MAT --> G["Greedy:<br/>repeatedly take global min<br/>strike out its row+col"]
    MAT --> H["Hungarian:<br/>O(n³) optimal<br/>min Σ C[i][σ(i)]"]

    G --> GC["greedy total cost"]
    H --> HC["optimal total cost"]

    GC --> CMP["compare"]
    HC --> CMP
    CMP --> OUT["assignment + regret = greedy − optimal"]

    OUT --> EXEC["dispatch missions"]

    style H fill:#2d5016,color:#fff
    style CMP fill:#1e3a5f,color:#fff
```

**Both are always computed** (they are cheap relative to planning), so the UI and the
benchmarks can always report the *regret* of greedy. That number is the evidence for why the
Hungarian algorithm is in the project at all — if regret is consistently zero on realistic
scenarios, that is an honest finding to report, not a failure to hide.

Rectangular cases (`m ≠ n`) are padded with dummy rows/columns at cost 0.

---

## 8. Simulation Loop

```mermaid
flowchart TB
    T["tick: t += Δt"] --> P["propagate all orbits<br/>θ(t+Δt) = θ(t) + ωΔt"]
    P --> E["pop all events with time ≤ t"]
    E --> H["handle each event<br/>failure · debris · fuel · comms"]
    H --> D["update deadlines<br/>expire closed survival windows"]
    D --> M["advance active missions<br/>progress · fuel burn · rendezvous"]
    M --> RC{"replan trigger<br/>fired?"}
    RC -->|yes| RP["DynamicReplanner (§9)"]
    RC -->|no| NP
    RP --> NP{"pending emergencies<br/>and free vehicles?"}
    NP -->|yes| AS["assignment (§7) → dispatch"]
    NP -->|no| MET
    AS --> MET["collect metrics"]
    MET --> SN{"snapshot due?<br/>(rate-limited)"}
    SN -->|yes| EM["emit snapshot"]
    SN -->|no| T
    EM --> T

    style RP fill:#5f3a1e,color:#fff
    style EM fill:#1e3a5f,color:#fff
```

Ordering is fixed and significant: propagate before events (events reference current
positions), expire deadlines before planning (never plan for an already-dead casualty), and
snapshot last (so a snapshot is always a consistent post-tick state, never mid-update).

---

## 9. Dynamic Replanning

**Status as of Phase 8: every box below now exists.** `DynamicReplanner::evaluate()`
(Phase 7, `DYNAMIC_REPLANNING.md`) remains the plain, directly-callable decision function;
`SimulationEngine` (Phase 8, `SIMULATION_ENGINE.md`) is its first real caller, invoking it
from `EventDispatcher.cpp`'s `handleDebrisAlert`/`handleVehicleDegradation` on the triggers
`SIMULATION_ENGINE.md` §11 lists. **Two corrections to the sequence diagram below, which
predates Phase 8's actual implementation:** there is no `GraphBuilder.patchEdges()` and no
incremental edge patching anywhere in the codebase — `MissionGraph` has no `patchEdges()`
method, and none was built (`ALGORITHMS.md` §11.2/§11.3 has the full correction). Debris
invalidation is instead represented as excluding one specific `EdgeId` from the *same*
stable graph's already-computed candidate front (`SIMULATION_ENGINE.md` §11's
"route-filtering, not graph-rebuilding"), not as a spatial region query against a rebuilt or
patched graph. The sequence below is retained as the general *shape* of the integration
(an event triggers a targeted re-evaluation, which flows into a replan decision, which flows
back into execution) but its "spatial query" and "GraphBuilder" steps should be read as
"proximity heuristic over the mission's remaining route" and "the existing, unmodified
graph," respectively — `SIMULATION_ENGINE.md` is the authoritative description of what
actually runs.

```mermaid
sequenceDiagram
    participant SE as SimulationEngine
    participant DR as DynamicReplanner
    participant GB as GraphBuilder
    participant MP as MissionPlanner
    participant EX as MissionExecutor
    participant UI as Frontend

    SE->>SE: t=598.5 DebrisAlert D-04
    SE->>DR: environmentChanged(D-04)

    DR->>DR: which active missions have<br/>remaining legs in the affected region?
    Note over DR: spatial query, not a full rebuild
    DR-->>DR: M-001 (RV-02 → SAT-017) affected

    DR->>GB: patchEdges(region(D-04))
    GB-->>DR: 14 edges re-costed, risk ↑
    Note over GB: patched graph is asserted<br/>equal to full rebuild in tests

    DR->>MP: replan(M-001, fromCurrentPosition, t)
    MP-->>DR: new front, best alternative:<br/>risk 0.31→0.09, time +42s

    DR->>DR: hysteresis check<br/>improvement ≥ margin?
    alt margin met
        DR->>EX: switchRoute(M-001, alternative)
        EX-->>UI: MissionReplan event + new trajectory
        UI->>UI: animate old route → new route
    else below margin
        DR-->>SE: keep current route, log decision
    end
```

**Hysteresis is essential.** Without a switching margin the planner oscillates between two
near-equal routes every tick, which looks broken and wastes fuel in the model. The margin is
configurable and its value is a documented tuning decision, not a magic number.

---

## 10. Frontend ⇄ Backend ⇄ Engine Communication

> **Status as of Phase 9: the `orca_engine` subgraph below is accurate as built. The
> Frontend/Node bridge subgraphs describe a WebSocket-based design Phase 9 deliberately did
> not build — REST polling instead** (`ARCHITECTURE.md` §4.1's correction,
> `INTEGRATION_ARCHITECTURE.md` §5-6 for the real design). Concretely: no `useEngineSocket()`
> (`EngineProvider`'s `useEffect` poll loop instead), no Zustand store (React Context), no
> `ws server`/snapshot coalescer/event buffer (a plain Express REST surface — `GET /health`,
> `GET /snapshot`, `POST /sim/*` — where every response already IS the latest snapshot, so
> there is nothing to coalesce), no class named `EngineSupervisor` (`EngineProcess`). The
> backpressure/correlation table below describes problems a push-based WebSocket channel
> has that a pull-based REST poll does not: there is no "engine outruns the browser" to
> solve (the browser asks whenever it asks, always for the newest state); no
> reconnect handshake beyond the same polling loop's next successful call succeeding again.
> `reqId` correlation is real and built exactly as described, just over HTTP request/response
> pairs rather than WebSocket frames.

```mermaid
flowchart TB
    subgraph FE["Frontend"]
        A1["useEngineSocket()"]
        A2["Zustand store"]
        A3["Components subscribe<br/>to selective slices"]
        A1 --> A2 --> A3
    end

    subgraph BE["Node bridge"]
        B1["ws server"]
        B2["Snapshot coalescer<br/>keep newest only"]
        B3["Event buffer<br/>ordered, lossless"]
        B4["Pending request map<br/>reqId → resolver"]
        B5["EngineSupervisor"]
        B2 --> B1
        B3 --> B1
        B4 --> B1
    end

    subgraph EN["orca_engine"]
        C1["stdout: NDJSON<br/>snapshot · event · planResult · error"]
        C2["stdin: NDJSON commands"]
        C3["stderr: human logs"]
    end

    A1 <-->|"WS frames (JSON)"| B1
    B5 -->|"write line"| C2
    C1 -->|"read line"| B5
    C3 -.->|"captured, surfaced on crash"| B5

    style C3 stroke-dasharray: 5 5
```

### Backpressure and correlation

| Concern | Handling |
|---|---|
| Engine outruns the browser | Coalescer holds one snapshot; older ones are dropped **by design** |
| Events must not be dropped | Separate lossless ordered buffer; if it exceeds a bound the engine is throttled instead |
| Slider dragged rapidly | Each request carries `reqId`; responses with a stale `reqId` are discarded client-side |
| Client reconnects | Server sends the latest snapshot immediately, then resumes streaming |
| Engine dies | Supervisor emits `engineStatus:down` with the captured `stderr` tail; UI dims to last-known state |

### Why `stderr` is separate

Protocol on `stdout`, logs on `stderr`. A stray `std::cout` in debugging code would otherwise
inject a non-JSON line into the protocol stream and desynchronise the codec. Keeping them on
different file descriptors makes that class of bug impossible rather than merely unlikely.

---

## 11. Visualisation Data Pipeline

The orbital-link visual language (`UI_UX_SPEC.md` §1.3) makes a specific demand on the
architecture: **every drawn relationship must correspond to engine data.** A line between two
orbital objects is only meaningful if the engine computed that the relationship exists.

This is what closes the loop between the visual design and the single-source-of-truth rule.

```mermaid
flowchart LR
    subgraph ENGINE["C++ engine — computes relationships"]
        O["Orbit<br/>a, i, RAAN, phase"]
        P["positionAtKm(t)<br/>derived, never stored"]
        E["GraphEdge<br/>feasible transfer + 4-vector cost"]
        T["Trajectory polyline<br/>engine-sampled"]
        R["rejectedAlternatives<br/>what was chosen against"]
        L["layout coords<br/>engine-computed"]
    end

    subgraph VISUAL["Frontend — draws them"]
        M["marker position"]
        RING["orbit ring geometry"]
        CAND["candidate route line"]
        ACT["active trajectory"]
        REJ["rejected route line"]
        NET["network graph"]
    end

    O --> RING
    P --> M
    E --> CAND
    T --> ACT
    R --> REJ
    L --> NET

    style ENGINE fill:#0d1b2a,color:#e0e1dd
    style VISUAL fill:#22333b,color:#e0e1dd
```

| Visual element | Engine source | Why it cannot be frontend-derived |
|---|---|---|
| Marker position | `Orbit::positionAtKm(t)` | Requires `mu` and the orbital model; duplicating it creates a second physics implementation |
| Orbit ring | `Orbit` elements | Ring radius must match marker orbit exactly, or the marker visibly floats off its own ring |
| Candidate route | `GraphEdge` set | The frontend has no feasibility model and cannot know which transfers are possible |
| Active trajectory | engine-sampled polyline | Interpolating a transfer arc is propagation, which is engine work |
| Rejected alternatives | `Mission::rejectedAlternatives` | These are search results; they exist nowhere else |
| Network layout | engine `layout` coords | A force-directed layout in React would be a real algorithm in the UI (`ARCHITECTURE_REVIEW.md` §2) |

**The consequence for Phase 1**, and the reason this section exists now rather than at Phase
17: the domain model had to expose position as a *function of time* rather than as stored
state, so the renderer can ask for any instant — including instants between snapshots, and
instants in the past when scrubbing. `Orbit::positionAtKm(t)` is that seam, and it was built
in Phase 1 precisely so the visualisation layer has nothing left to compute.

> **Status as of Phase 10: marker position, orbit ring, and candidate-route rows above are
> built exactly as this table describes.** Two rows are corrected against what was actually
> built (`VISUAL_DESIGN.md` §8/§13 have the full reasoning):
>
> - **Active trajectory** is NOT an engine-sampled polyline — `MissionLegView` carries only
>   each leg's two real endpoint positions (no intermediate points), and the frontend draws
>   a straight connector between them. This is deliberate, not a shortcut: exact mid-leg
>   position is not modelled anywhere in the engine either
>   (`DYNAMIC_REPLANNING.md` §13.2), so an engine-sampled arc would have nothing real to
>   sample from. A straight line between two real points is the honest choice, stated as
>   such in `VISUAL_DESIGN.md`, not presented as a computed trajectory.
> - **Network layout** is NOT engine-computed — `GraphNodeView`/`GraphEdgeView`
>   (`cpp/io/SimulationView.hpp`) carry no layout coordinates at all; the Orbital Network
>   page positions nodes with a simple, deterministic, presentation-only ring layout
>   (`frontend/src/pages/OrbitalNetwork/networkLayout.ts` — vehicles inner ring, satellites
>   outer ring, evenly spaced by index). This is explicitly NOT a force-directed layout (the
>   concern this table's own row raises) and is documented as carrying no algorithmic
>   meaning, per brief §37's own instruction.
>
> `Mission::rejectedAlternatives` is not currently surfaced to the frontend at all (a
> possible small future addition, not fabricated here).
