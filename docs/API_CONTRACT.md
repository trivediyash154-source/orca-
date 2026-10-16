# ORCA — API Contract

Version 1.0 · `schemaVersion: 1` · Phase 0 · 2026-09-02

Defines the two integration seams: **Node ⇄ Engine** (NDJSON over pipes) and
**Frontend ⇄ Node** (REST + WebSocket). Entity semantics, units and invariants come from
`DATA_MODEL.md`; this document defines only the wire representation.

> **Status as of Phase 9: this document is the original Phase 0 aspirational sketch of the
> FULL eventual contract; `INTEGRATION_ARCHITECTURE.md` documents what was actually built
> and is authoritative for that.** The built contract is deliberately narrower —
> REST-polling only (no WebSocket), nine lifecycle commands (no `plan`/`reselect`/`assign`/
> `getGraph`/`traceAlgorithm`/`exportResults`, since `SimulationEngine` exposes no
> planning-request surface for those to call), and one response shape (a full `snapshot`,
> not the `planResult`/`assignmentResult`/`graphData`/`algorithmTrace` message zoo below).
> `INTEGRATION_ARCHITECTURE.md` §9 has the itemized diff. The sections below remain useful
> as the target shape a future phase (once planning-request commands are actually built)
> would grow into, and `DATA_MODEL.md`'s entity semantics they reference are still accurate
> — but no code in this repository emits or parses most of the messages below yet.

---

## 1. Contract Rules

1. **JSON is transport, not authority.** Every value originates in the C++ engine.
2. **Units follow `DATA_MODEL.md` §0.1** — km, seconds, km/s, fractions in `[0,1]`. Angles
   cross the wire in **degrees** with a `Deg` suffix; radians never leave the engine.
3. **Additive evolution only.** New optional fields are non-breaking. Removing or retyping a
   field requires a `schemaVersion` bump.
4. **Unknown fields are ignored** by receivers, in both directions.
5. **One JSON object per line** on the engine channel. No pretty-printing, no embedded
   newlines. `\n` terminates every message.
6. **`stdout` is protocol; `stderr` is logs.** Logs are never parsed.
7. **Floats are finite.** `NaN`/`Infinity` are never emitted — infeasible values are
   represented by `feasible: false` plus a reason, never by a sentinel number.

---

## 2. Node ⇄ Engine — Framing

```
Node  --stdin-->  {"cmd":"plan","reqId":42,...}\n
Engine --stdout-> {"type":"planResult","reqId":42,...}\n
Engine --stderr-> [INFO ] t=412.0 Graph rebuilt: V=28 E=173
```

Every engine→Node message has a `type`. Every Node→engine message has a `cmd`. Commands
expecting a reply carry `reqId` (monotonic `uint32`); the reply echoes it. Streamed messages
(`snapshot`, `event`) carry no `reqId`.

---

## 3. Commands (Node → Engine)

### 3.1 Lifecycle

```json
{"cmd":"init","reqId":1,"scenarioPath":"scenarios/demo.json","seed":928371}
{"cmd":"start"}
{"cmd":"pause"}
{"cmd":"resume"}
{"cmd":"step","ticks":1}
{"cmd":"setSpeed","multiplier":4.0}
{"cmd":"reset","reqId":7,"seed":928371}
{"cmd":"shutdown"}
```

`multiplier ∈ {0.25, 0.5, 1, 2, 4, 8, 16}`. `reset` with the same seed **must** reproduce the
run exactly — this is FR-N5 and is asserted by the determinism test.

### 3.2 Planning

```json
{
  "cmd": "plan",
  "reqId": 42,
  "emergencyId": "E-003",
  "profile": "BALANCED",
  "algorithm": "PARETO",
  "options": { "maxLabels": 5000, "epsilon": 0.01 }
}
```

`algorithm ∈ {"DIJKSTRA","ASTAR","PARETO","GREEDY"}` — selecting the strategy is what makes
benchmark comparison a configuration rather than a code change.

Re-select on an existing front without re-searching (the two-stage payoff, `SYSTEM_DESIGN.md`
§6):

```json
{
  "cmd": "reselect",
  "reqId": 43,
  "planRequestId": 42,
  "profile": "CUSTOM",
  "weights": { "time": 0.5, "fuel": 0.2, "risk": 0.25, "distance": 0.05 }
}
```

**`reselect` never re-runs graph search.** It rescores the cached front. Latency is
`O(|front|)`, which is what makes slider dragging feel instant.

### 3.3 Assignment, inspection, export

```json
{"cmd":"assign","reqId":44,"solver":"HUNGARIAN"}
{"cmd":"getGraph","reqId":45,"emergencyId":"E-003"}
{"cmd":"traceAlgorithm","reqId":46,"algorithm":"ASTAR","emergencyId":"E-003","maxSteps":500}
{"cmd":"exportResults","reqId":47,"format":"csv","path":"benchmarks/run-001.csv"}
```

`solver ∈ {"GREEDY","HUNGARIAN"}`. `traceAlgorithm` returns a step-by-step search trace for
the Algorithm Lab — computed by the engine, replayed by the UI.

---

## 4. Messages (Engine → Node)

### 4.1 `ready`
```json
{"type":"ready","schemaVersion":1,"engineVersion":"0.1.0",
 "capabilities":["pareto","astar","dijkstra","hungarian","trace"],"seed":928371}
```

### 4.2 `snapshot`

The primary stream. Full state; safe to drop stale ones (§7).

```json
{
  "type": "snapshot",
  "simulationTimeSec": 823.2,
  "runState": "RUNNING",
  "speedMultiplier": 4.0,
  "satellites": [
    {
      "id": "SAT-017", "name": "Meridian-3", "status": "FAILED",
      "position": { "x": 5821.4, "y": -3011.7, "z": 1204.9 },
      "orbit": { "altitudeKm": 512.3, "inclinationDeg": 51.6,
                 "raanDeg": 128.4, "phaseDeg": 214.7, "band": "LEO" },
      "fuelFraction": 0.41,
      "health": { "power": 0.92, "propulsion": 0.0, "comms": 0.88, "thermal": 0.95 },
      "activeEmergencyId": "E-003"
    }
  ],
  "rescueVehicles": [
    {
      "id": "RV-02", "name": "Aegis-2", "status": "TRANSFERRING",
      "position": { "x": 6102.8, "y": -2740.1, "z": 998.2 },
      "orbit": { "altitudeKm": 548.9, "inclinationDeg": 51.9,
                 "raanDeg": 130.1, "phaseDeg": 198.3, "band": "LEO" },
      "deltaVBudgetKms": 2.5, "deltaVRemainingKms": 2.16,
      "fuelFraction": 0.864,
      "activeMissionId": "M-001"
    }
  ],
  "debris": [
    {"id":"D-04","position":{"x":5990.2,"y":-2888.0,"z":1100.5},
     "radiusKm":18.0,"densityFactor":0.7,"detected":true}
  ],
  "emergencies": [
    {
      "id": "E-003", "subjectId": "SAT-017", "kind": "PROPULSION_FAILURE",
      "severity": "CRITICAL", "state": "IN_PROGRESS",
      "detectedAtSec": 412.0, "deadlineSec": 2332.0,
      "remainingWindowSec": 1508.8, "priority": 0.91
    }
  ],
  "missions": [
    {
      "id": "M-001", "vehicleId": "RV-02", "emergencyId": "E-003",
      "state": "IN_TRANSIT", "currentLeg": 1, "progressOnLeg": 0.34,
      "etaSec": 708.4, "deltaVSpentKms": 0.34, "replanCount": 0,
      "totalCost": {"time":708.4,"fuel":0.136,"risk":0.072,"distance":1842.6},
      "trajectory": {
        "visualState": "ACTIVE",
        "polyline": [
          {"t":412.0,"position":{"x":6210.0,"y":-2601.3,"z":940.1}},
          {"t":470.0,"position":{"x":6180.2,"y":-2688.9,"z":962.4}}
        ]
      }
    }
  ],
  "newEvents": [],
  "analytics": {
    "totalEmergencies": 3, "successfulRescues": 1, "failedRescues": 0,
    "avgRescueTimeSec": 640.2, "avgFuelFraction": 0.121, "avgRisk": 0.083,
    "replanCount": 1, "activeMissions": 1
  }
}
```

Note `remainingWindowSec` is engine-computed and shipped, rather than letting the frontend
subtract `deadlineSec − simulationTimeSec`. That subtraction would be trivially correct today
and would silently diverge the moment deadlines become mutable. **The frontend derives
nothing** (`ARCHITECTURE.md` §6.1).

### 4.3 `event`

Ordered, never dropped.

```json
{"type":"event","event":{
  "id":"EV-00318","timeSec":598.5,"kind":"DebrisAlert","severity":"HIGH",
  "subjectId":"D-04","message":"Debris D-04 within 12.3 km of M-001 leg 2",
  "payload":{"missionId":"M-001","closestApproachKm":12.3,"affectedEdges":14}}}
```

### 4.4 `planResult`

```json
{
  "type": "planResult", "reqId": 42, "emergencyId": "E-003", "feasible": true,
  "algorithm": "PARETO", "profile": "BALANCED",
  "front": [
    {"planId":"P-0007","vehicleId":"RV-02","route":["N-0003","N-0011","N-0042"],
     "cost":{"time":708.4,"fuel":0.136,"risk":0.072,"distance":1842.6},
     "departureTimeSec":412.0,"arrivalTimeSec":1120.4,"recommended":true},
    {"planId":"P-0012","vehicleId":"RV-02","route":["N-0003","N-0042"],
     "cost":{"time":540.1,"fuel":0.242,"risk":0.181,"distance":1502.0},
     "departureTimeSec":412.0,"arrivalTimeSec":952.1,"recommended":false},
    {"planId":"P-0019","vehicleId":"RV-03","route":["N-0005","N-0028","N-0042"],
     "cost":{"time":1024.7,"fuel":0.098,"risk":0.041,"distance":2210.3},
     "departureTimeSec":412.0,"arrivalTimeSec":1436.7,"recommended":false}
  ],
  "dominated": [
    {"planId":"P-0021","cost":{"time":890.0,"fuel":0.201,"risk":0.155,"distance":2004.1},
     "dominatedBy":"P-0007"}
  ],
  "ideal": {"time":540.1,"fuel":0.098,"risk":0.041,"distance":1502.0},
  "nadir": {"time":1024.7,"fuel":0.242,"risk":0.181,"distance":2210.3},
  "truncated": false,
  "metrics": {"algorithm":"ParetoSearch","nodesExpanded":186,"edgesRelaxed":412,
              "pqPushes":412,"pqPops":398,"labelsGenerated":412,"labelsPruned":371,
              "labelsRetained":41,"wallTimeMs":11.4,"optimal":true},
  "explanation": {
    "summary":"RV-02 via N-0011 balances a 31% time penalty against 44% lower fuel and 60% lower risk than the fastest option.",
    "factors":[
      {"label":"Fastest alternative","detail":"P-0012 arrives 168s sooner but costs 78% more fuel and 2.5× the risk"},
      {"label":"Safest alternative","detail":"P-0019 halves risk again but arrives 316s later, inside the window by only 895s"},
      {"label":"Binding consideration","detail":"RV-03 has 0.42 km/s less remaining Δv than RV-02"}
    ]
  }
}
```

**Note `ideal` and `nadir` are per-objective extremes across the front — they are generally
not attained by any single plan.** They exist so normalisation is well-defined; the UI must
not present them as achievable options.

Infeasible result — a **normal outcome**, not an error:

```json
{
  "type":"planResult","reqId":42,"emergencyId":"E-007","feasible":false,
  "reason":"NO_FEASIBLE_PATH",
  "bindingConstraint":{
    "kind":"DELTA_V_BUDGET","detail":"Best route requires 2.91 km/s; RV-02 has 2.16 km/s remaining",
    "shortfall":0.75,"unit":"km/s"},
  "graphStats":{"vertexCount":24,"edgeCount":0,
                "rejectedFuel":412,"rejectedDeadline":88,"rejectedRisk":0,"buildTimeMs":2.8},
  "metrics":{"algorithm":"ParetoSearch","nodesExpanded":1,"wallTimeMs":0.3,"optimal":true}
}
```

`reason ∈ {"NO_FEASIBLE_PATH","EMPTY_GRAPH","NO_ASSET","UNREACHABLE","DEADLINE_PASSED"}`.

### 4.5 `assignmentResult`

```json
{
  "type":"assignmentResult","reqId":44,
  "assignments":[
    {"emergencyId":"E-003","vehicleId":"RV-02","planId":"P-0007","cost":0.412},
    {"emergencyId":"E-005","vehicleId":"RV-01","planId":"P-0033","cost":0.588}],
  "greedy":{"totalCost":1.201,"solver":"GREEDY","wallTimeMs":0.08},
  "optimal":{"totalCost":1.000,"solver":"HUNGARIAN","wallTimeMs":0.31},
  "regret":0.201,
  "unassigned":[{"emergencyId":"E-009","reason":"NO_ASSET"}]
}
```

### 4.6 `graphData`

For the Network screen. Positions are **engine-computed layout coordinates**, so the frontend
runs no layout algorithm.

```json
{"type":"graphData","reqId":45,
 "nodes":[{"id":"N-0003","kind":"VEHICLE_SOURCE","entityId":"RV-02",
           "layout":{"x":0.12,"y":0.44},"altitudeKm":548.9}],
 "edges":[{"id":"E-0011","from":"N-0003","to":"N-0011",
           "cost":{"time":302.1,"fuel":0.061,"risk":0.028,"distance":812.4},
           "onRecommendedRoute":true}],
 "stats":{"vertexCount":28,"edgeCount":173,"rejectedFuel":511,
          "rejectedDeadline":72,"rejectedRisk":0,"buildTimeMs":3.1}}
```

### 4.7 `algorithmTrace`

```json
{"type":"algorithmTrace","reqId":46,"algorithm":"ASTAR","complete":true,
 "steps":[
   {"i":0,"current":"N-0003","frontier":["N-0011","N-0019"],"visited":[],
    "g":{"N-0003":0.0},"h":{"N-0003":486.2},"f":{"N-0003":486.2}},
   {"i":1,"current":"N-0011","frontier":["N-0019","N-0027"],"visited":["N-0003"],
    "g":{"N-0011":302.1},"h":{"N-0011":198.4},"f":{"N-0011":500.5}}],
 "finalRoute":["N-0003","N-0011","N-0042"],
 "metrics":{"algorithm":"AStar","nodesExpanded":37,"edgesRelaxed":112,"wallTimeMs":1.2,"optimal":true}}
```

For `PARETO`, each step additionally carries `labelsAtNode`, `generated`, `prunedThisStep`.

### 4.8 `error`

Protocol-level only. Domain outcomes like infeasibility use `planResult`.

```json
{"type":"error","reqId":42,"code":"INVALID_WEIGHTS",
 "message":"Profile weights must be non-negative and sum > 0",
 "detail":{"received":{"time":-0.1,"fuel":0.5,"risk":0.3,"distance":0.3}}}
```

`code ∈ {"UNKNOWN_COMMAND","MALFORMED_JSON","INVALID_WEIGHTS","SCENARIO_INVALID",`
`"NOT_INITIALIZED","UNKNOWN_ENTITY","INTERNAL_ERROR"}`.

---

## 5. Frontend ⇄ Node — REST

Base `http://localhost:4000/api`. All responses `application/json`.

| Method | Path | Body | Returns |
|---|---|---|---|
| `GET` | `/health` | — | `{status, engineUp, schemaVersion, uptimeSec}` |
| `GET` | `/scenarios` | — | list of available scenarios |
| `POST` | `/scenario/load` | `{path, seed?}` | `{ok, scenario}` |
| `POST` | `/sim/start` \| `/pause` \| `/resume` \| `/reset` | `{seed?}` | `{ok, runState}` |
| `POST` | `/sim/step` | `{ticks}` | `{ok, simulationTimeSec}` |
| `POST` | `/sim/speed` | `{multiplier}` | `{ok, speedMultiplier}` |
| `POST` | `/plan` | `{emergencyId, profile, algorithm?, options?}` | `planResult` |
| `POST` | `/plan/reselect` | `{planRequestId, profile, weights?}` | `planResult` |
| `POST` | `/assign` | `{solver}` | `assignmentResult` |
| `GET` | `/graph?emergencyId=` | — | `graphData` |
| `POST` | `/trace` | `{algorithm, emergencyId, maxSteps}` | `algorithmTrace` |
| `POST` | `/export` | `{format, path}` | `{ok, path, rows}` |
| `GET` | `/analytics` | — | `analytics` summary |

### Error shape

```json
{"error":{"code":"ENGINE_DOWN",
          "message":"Engine process is not running",
          "detail":{"lastExitCode":139,
                    "stderrTail":["[ERROR] t=412.0 assertion failed: adjacency_.size()==nodes_.size()"]}}}
```

| HTTP | When |
|---|---|
| 200 | success (**including `feasible:false`** — infeasibility is a result) |
| 400 | malformed request, invalid weights |
| 404 | unknown scenario or entity |
| 409 | command invalid in current run state (e.g. `step` while `RUNNING`) |
| 503 | engine down or still starting |
| 504 | engine did not reply within timeout |

---

## 6. Frontend ⇄ Node — WebSocket

`ws://localhost:4000/ws`. Server → client frames are the engine messages of §4 relayed
verbatim, plus two bridge-generated frames:

```json
{"type":"hello","schemaVersion":1,"engineUp":true,
 "capabilities":["pareto","astar","dijkstra","hungarian","trace"]}

{"type":"engineStatus","up":false,"reason":"CRASHED","exitCode":139,
 "restartAttempt":1,"maxRestarts":3,
 "stderrTail":["[ERROR] t=412.0 assertion failed"]}
```

Client → server is limited to `{"type":"ping"}` and
`{"type":"subscribe","channels":["snapshot","event"]}`. **All state-changing commands go over
REST**, so every mutation has a request/response pair and an HTTP status — the socket carries
no authority.

On connect the server immediately sends `hello`, then the latest snapshot, then resumes
streaming. Heartbeat: server pings every 15 s; a client silent for 45 s is dropped.

---

## 7. Stream Disciplines

| Stream | Discipline | Rationale |
|---|---|---|
| `snapshot` | **Latest-wins.** Bridge holds at most one undelivered snapshot | Full-state message; a stale one is worthless once a newer exists. Bounds memory under backpressure |
| `event` | **Lossless, strictly ordered.** Unbounded within a cap; if the cap is hit the engine is throttled rather than events dropped | Events are the causal narrative; a gap corrupts the timeline |
| `planResult` | **Correlated by `reqId`.** Client discards responses whose `reqId` is older than its newest outstanding request | Rapid slider drags generate superseded requests |

---

## 8. TypeScript Contract

`frontend/src/types/engine.ts` mirrors §4 exactly. Every field is `readonly`; the store never
mutates engine data.

```ts
export interface ObjectiveVector {
  readonly time: number;      // seconds
  readonly fuel: number;      // fraction of the vehicle's own Δv budget, [0,1]
  readonly risk: number;      // [0,1], simulation risk
  readonly distance: number;  // km
}

export interface SimulationSnapshot {
  readonly type: 'snapshot';
  readonly simulationTimeSec: number;
  readonly runState: 'STOPPED' | 'RUNNING' | 'PAUSED' | 'COMPLETED';
  readonly speedMultiplier: number;
  readonly satellites: readonly SatelliteView[];
  readonly rescueVehicles: readonly VehicleView[];
  readonly debris: readonly DebrisView[];
  readonly emergencies: readonly EmergencyView[];
  readonly missions: readonly MissionView[];
  readonly newEvents: readonly EngineEvent[];
  readonly analytics: AnalyticsSummary;
}

export type EngineMessage =
  | SimulationSnapshot | EventMessage | PlanResult | AssignmentResult
  | GraphData | AlgorithmTrace | EngineError | HelloMessage | EngineStatus;
```

`EngineMessage` is a discriminated union on `type`, so `switch` handling is exhaustiveness-
checked by the compiler. Adding an engine message type without handling it becomes a build
failure — which is the specific reason TypeScript was chosen (`ARCHITECTURE.md` §3).

---

## 9. Fixtures and Contract Testing

`tests/fixtures/` holds recorded NDJSON captured from real engine runs at a fixed seed:

```
tests/fixtures/
├── demo-seed-928371.ndjson      # full run
├── planresult-feasible.json
├── planresult-infeasible.json
├── assignment-greedy-vs-optimal.json
└── algorithmtrace-astar.json
```

These serve three purposes:

1. **Frontend development without a built engine** — the bridge can replay a fixture file
   instead of spawning a child process (`ENGINE_MODE=replay`). This is what keeps the CMake
   gap (audit §3) from blocking UI work.
2. **Contract tests** — fixtures are parsed against the TypeScript types; a schema drift
   fails the build on both sides.
3. **Regression detection** — a re-recorded fixture that differs at the same seed means
   either an intentional change or a determinism bug. Either way it demands attention.

**Fixtures are recorded from real runs. None are hand-written.** A hand-written fixture would
encode what we *believe* the engine emits rather than what it *does*, which defeats the
purpose.
