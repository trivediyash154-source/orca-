# ORCA — Integration Architecture

Version 1.0 · Phase 9 · 2026-09-03

How the C++ engine's already-computed state reaches a browser: `cpp/io/`'s snapshot
projection and NDJSON protocol, `backend/`'s Node bridge, and `frontend/`'s React + Three.js
shell. This document is the authoritative description of what was actually built — not the
Phase 0 aspirational sketch `API_CONTRACT.md` originally carried (§9 below has the exact
corrections).

---

## 1. The three processes, and the one rule that governs all of them

```
React frontend  (browser)  --HTTP (polling)-->  Node bridge  --NDJSON/stdio-->  C++ engine
```

> The browser never independently simulates ORCA. It renders engine-provided state.
> Node never implements ORCA planning logic. It transports and orchestrates the engine.

Concretely: every number a component displays came from a `SimulationSnapshot` the engine
computed; every button click sends one of nine fixed command names the engine itself
recognizes (`cpp/io/CommandProtocol.cpp`); nothing in `backend/` or `frontend/` calls
`TransferModel::evaluate()`, walks a graph, or re-derives a position from an orbit's
elements — those exist only in `cpp/`.

## 2. The C++ Process

`orca_engine --serve` (`cpp/app/main.cpp`) boots the canonical demo scenario
(`cpp/simulation/DemoScenario.hpp` — the exact scenario Phase 8's own end-to-end test
verified), pre-schedules its scripted failure/debris timeline, constructs one
`SimulationEngine`, prints one `ready` message to stdout, and then loops: read one line from
stdin, parse it as a command, apply it to the engine, write one line to stdout in response.

There is no scenario-file loader (`DATA_MODEL.md` §8's own correction already states this)
— Phase 9 did not build one, so `--serve` always runs the same fixed scenario. A future
phase adding scenario selection would extend the `ready`/command surface, not change this
document's architecture.

**stdout is protocol; stderr is logs** (`API_CONTRACT.md` §1.6, honored exactly): `main.cpp`
never writes anything to stdout except one NDJSON line per message. Nothing in this phase
writes diagnostic output at all, so stderr is empty in normal operation — the discipline is
maintained by never adding a stray `std::cout`/`printf` to `serve()`, not by routing existing
logs elsewhere.

## 3. NDJSON Protocol

One JSON object per line, `\n`-terminated, no pretty-printing (`cpp/io/JsonValue.hpp`'s
`dump()` enforces single-line output and rejects non-finite numbers). Three message shapes
flow engine → Node:

```json
{"type":"ready","schemaVersion":1,"engineVersion":"0.9.0","capabilities":["start","pause","resume","reset","step","advance","setSpeed","getSnapshot"]}
{"type":"snapshot","schemaVersion":1,"simulationTimeSec":30,"simulationStatus":"RUNNING", ... ,"reqId":2}
{"type":"error","code":"UNKNOWN_COMMAND","message":"unrecognized command 'plan'","reqId":7}
```

Nine command names flow Node → engine (`cpp/io/CommandProtocol.cpp`), each optionally
carrying a `reqId` that its response echoes:

```json
{"cmd":"start","reqId":1}
{"cmd":"advance","reqId":2,"seconds":30}
{"cmd":"setSpeed","reqId":3,"multiplier":4}
{"cmd":"getSnapshot","reqId":4}
{"cmd":"shutdown"}
```

`start`, `pause`, `resume`, `reset`, `step`, `getSnapshot` take no parameters; `advance`
requires `seconds`; `setSpeed` requires `multiplier`. Every command except `shutdown`
responds with a fresh, complete `snapshot` — there is no separate "ack" message, because a
full snapshot already tells the caller everything an ack would (§9.2 corrects
`API_CONTRACT.md`'s original richer message-type list against this).

## 4. Snapshot Projection (`cpp/io/`)

`ViewProjection.cpp::buildSnapshot(engine)` reads `SimulationEngine::state()`,
`::graph()`, `::clock()` and produces a `SimulationSnapshot` — a tree of the flattened
`*View` structs in `SimulationView.hpp` (`SatelliteView`, `VehicleView`, `DebrisView`,
`EmergencyView`, `MissionView`/`MissionLegView`, `EventView`, `MetricsView`). This is the
boundary `ARCHITECTURE.md` §6.1's rule depends on: every field in a view struct is already
the engine's own computed answer (a position from `positionAtKm()`, a route leg's actual
delta-v from the SAME `computeLegTimings()` `MissionExecutor.cpp` uses to fly the mission,
never a second, parallel computation of it — `SIMULATION_ENGINE.md` §8's `LegTiming`/
`computeLegTimings()` was made a public function specifically so `ViewProjection.cpp` could
reuse it rather than re-derive per-leg timing itself).

`JsonSerializer.cpp` then converts the view tree into a `JsonValue` tree and dumps it to
one compact line. Two layers, two files, so "what data exists" (`ViewProjection.cpp`) and
"how it's encoded on the wire" (`JsonSerializer.cpp`) can each be read and tested in
isolation.

### 4.1 IDs cross the wire as strings

Every entity ID (`"SAT-101"`, `"RV-01"`, `"M-000"`, …) is `toString(Id<Tag>)`'s existing
prefixed form, not the raw `uint32_t` underneath it. This was a deliberate choice, not the
only option — `core/Types.hpp`'s `Id<Tag>` wraps a `uint32_t`, which is nowhere near
JavaScript's `Number.MAX_SAFE_INTEGER` (2^53 − 1) regardless of representation, so precision
loss was never actually at risk for this project's ID range. The string form was chosen
anyway because (a) it matches the convention `API_CONTRACT.md`'s worked examples already
used since Phase 0, (b) it is self-describing in captured NDJSON/browser devtools without
cross-referencing a prefix table, and (c) it makes the "could a number silently become
imprecise" question moot by construction rather than by argument. `EventId` uses the same
convention. No field in the snapshot is a raw numeric entity ID.

### 4.2 Fixed-point vs. computed fields

`remainingWindowSec` and `urgency` (`EmergencyView`) are computed by the engine at snapshot
time (`Emergency::remainingWindowSec(now)`/`urgencyAt(now)`), never left for a consumer to
derive from `deadlineSec - simulationTimeSec` — the same reasoning `API_CONTRACT.md` §4.2
already gave when this convention was designed in Phase 0, now actually implemented.

### 4.3 Phase 10 additions: `scenarioName`, `graph`, `paretoFront`, `explanation`

Four fields were added to the schema to support Mission Control's flagship views, without
retyping `schemaVersion` (purely additive, per `API_CONTRACT.md` §1.3's own evolution
rule):

- **`SimulationSnapshot.scenarioName`** — `Scenario::name()`, so the top bar never
  hardcodes it (brief §11).
- **`SimulationSnapshot.graph`** (`GraphView`: `nodes`/`edges`) — the mission graph,
  projected once per snapshot from `SimulationEngine::graph()` (static per run; included in
  full each time for simplicity, not because it changes). Powers the Orbital Network page.
- **`MissionView.paretoFront`/`.paretoHistory`** (`ParetoPointView[]`/`ParetoHistoryEntryView[]`)
  and **`.explanation`** (`string | null`) — captured by `EventDispatcher.cpp` at the moment
  of decision (global dispatch for pass 1 of the front; a real `Replan` decision appends a
  further pass; `Replan`/`Abort` for the explanation) into `SimulationState` (`missionParetoHistory`,
  `missionExplanations`), keyed by mission ID. All genuinely captured engine data, not
  synthesized in the view layer. Phase 11 (brief §52-54) replaced the single cached front
  with a retained history across every planning pass — `VISUAL_DESIGN.md` §12 documents the
  one remaining known limitation (a pass captured from a local replan is a single-point
  front, not a full re-derived multi-point front) explicitly, including how the UI states it
  rather than hiding it.

## 5. Node Bridge (`backend/`)

`EngineProcess` (`backend/src/engine/EngineProcess.ts`) is the only code in the repository
that spawns or writes to the C++ process. It owns:

- **Process lifecycle** — `spawn(enginePath, ['--serve'])`, `stdin`/`stdout`/`stderr`
  piped, `exit`/`error` handlers.
- **NDJSON line buffering** (`backend/src/ipc/ndjsonParser.ts`'s `NdjsonLineParser`) — never
  calls `JSON.parse()` on a raw chunk; buffers until a `\n` completes a line, so a message
  split across two `stdout` `data` events parses correctly either way (brief §24's explicit
  requirement; `test/ndjsonParser.test.ts` proves it byte-by-byte).
- **Request/response correlation** — `sendCommand()` assigns a sequential `reqId`, writes
  the command line, and returns a `Promise` resolved by the next `snapshot`/`error` message
  carrying that same `reqId`, with a timeout (`commandTimeoutMs`) so a lost response never
  hangs a caller forever.
- **Crash handling** — on `exit`, every still-outstanding request is rejected immediately
  rather than left pending; `status` becomes `CRASHED`, and the last `stderrTail` (20 lines)
  is retained for diagnostics.

`api/server.ts::createApp(engine)` builds the REST surface a browser talks to:

| Method | Path | Body | Behavior |
|---|---|---|---|
| GET | `/health` | — | `{status, engineUp, schemaVersion, uptimeSec}` |
| GET | `/snapshot` | — | latest snapshot, or 503 if none exists yet |
| POST | `/sim/start` \| `/pause` \| `/resume` \| `/reset` \| `/step` | — | `{ok, snapshot}` |
| POST | `/sim/advance` | `{seconds}` | `{ok, snapshot}` |
| POST | `/sim/speed` | `{multiplier}` | `{ok, snapshot}` |

Every mutating command is exactly one of these seven fixed routes — no `execute(command)`
with an arbitrary string exists anywhere (brief §55's explicit prohibition;
`server.test.ts`'s error-path tests confirm an unsupported request is rejected with 400/503,
never forwarded as-is to the engine).

## 6. React Frontend (`frontend/`)

`EngineProvider` (`state/EngineContext.tsx`) polls `GET /health` then `GET /snapshot` on a
fixed interval (default 500 ms — a rendering-cadence choice, unrelated to the engine's own
1.0 s simulation timestep, brief §70/§71's explicit "simulation tick and render FPS are
separate" requirement) and republishes the result as React context: `connectionStatus`,
`snapshot`, `selected`. Every command (`sendCommand`, `advance`, `setSpeed`) goes through
`services/engineService.ts`, the one module that calls `fetch()` against the backend — no
component touches the network directly (brief §36).

`visualization/scene/Scene.tsx` (`react-three-fiber`) renders `Earth`, one `OrbitRing` per
satellite/vehicle (`visualization/orbit/orbitGeometry.ts` traces a ring from real orbital
elements — altitude/inclination/RAAN — using the exact position formula
`Orbit::positionAtKm()` itself uses, evaluated at parametric phase angles rather than real
time; it never invents an element the snapshot didn't provide), one `EntityMarker` per
satellite/vehicle at its real `PositionView`, and one `MissionLink` per route leg connecting
two real endpoint positions. `visualization/coordinates.ts::eciToScene()` is the single,
documented conversion from the engine's ECI kilometres to Three.js's Y-up scene units — no
other file performs an axis swap or unit conversion (brief §42).

### 6.1 Phase 10: pages and UI-only state

`state/UIContext.tsx` holds page navigation (`PageId`) and presentation-mode/reduced-motion
preferences — deliberately separate from `EngineContext` (brief §75), since none of it is
simulation truth. `App.tsx`'s `CurrentPage` is a plain `switch` over nine pages
(`src/pages/`), no router library. Mission Control, Orbital Network, and Optimization Lab
read real snapshot data (`snapshot.graph`, `mission.paretoFront`); Mission Planner and
Algorithm Lab are honest placeholders (brief §10) where no backing engine capability exists
yet. Full visual design: `VISUAL_DESIGN.md`.

## 7. Data, Command and Snapshot Flow

```
Command flow:   Frontend button --fetch POST--> Node route --sendCommand()--> engine stdin
State flow:     engine stdout --NDJSON line--> EngineProcess --latestSnapshot--> GET /snapshot --> EngineProvider --> React tree
```

A command's own response snapshot and the next poll's snapshot are the same kind of
message — `EngineProvider` does not distinguish "snapshot because I asked" from "snapshot
because a poll happened to run"; both **replace** `snapshot` in state wholesale (§8.3).

## 8. Error Handling and Reconnection

### 8.1 Malformed/unsupported input never crashes the engine

`main.cpp`'s command loop catches `JsonError` (malformed JSON) and `CommandError`
(well-formed JSON, invalid/unknown command) around every line individually, emitting a
protocol `error` message and continuing the loop — one bad line never terminates the
process. `EngineDispatcher`-level exceptions (an engine call itself throwing, e.g.
`advance()` given a non-multiple-of-timestep) are caught the same way, mapped to
`INTERNAL_ERROR`.

### 8.2 Node surfaces engine-down state, never hangs

`GET /snapshot`/every `POST /sim/*` checks `engine.getStatus() === 'READY'` before doing
anything; if not, the response is `503 {error:{code:'ENGINE_DOWN', detail:{status,
stderrTail}}}` immediately — no request waits on a process that isn't there.

### 8.3 Frontend reconnection replaces, never merges

`EngineOfflineOverlay` renders whenever `connectionStatus !== 'Connected'` — an honest
"ORCA ENGINE OFFLINE"/"ORCA ENGINE STARTING" message over the (still-visible, last-known)
scene, never a silently frozen dashboard (brief §56). When polling recovers, the next
successful snapshot **replaces** `snapshot` in `EngineContext` state wholesale
(`setSnapshot(snap)`, never a field-by-field merge) — so a backend restart's fresh,
authoritative state (e.g. after the engine process itself restarted and reset) can never be
blended with stale pre-disconnect data (brief §82; `EngineContext.test.tsx`'s
"replaces wholesale" test simulates exactly this: a snapshot with 1 mission followed by one
with 0, and asserts the mission count actually drops to 0, not silently retained).

### 8.4 Process shutdown

`backend/src/index.ts` registers `SIGINT`/`SIGTERM` handlers: stop accepting new HTTP
requests, send `{"cmd":"shutdown"}` to the engine, wait up to 2 s for it to exit on its own,
then `SIGTERM` it directly if it hasn't (`EngineProcess.shutdown()`). No orphaned
`orca_engine` process is left behind in either path — verified manually (§13 of the Phase 9
final report) by starting the full stack and confirming `pgrep orca_engine` finds nothing
after killing the backend.

## 9. Corrections to `API_CONTRACT.md`

`API_CONTRACT.md` is Phase 0's aspirational sketch of the full eventual contract —
`plan`/`reselect`/`assign`/`getGraph`/`traceAlgorithm`/`exportResults` commands, a
WebSocket channel, a nine-endpoint REST surface. None of that is what Phase 9 builds,
because none of it is backed by anything `SimulationEngine` (Phase 8) actually exposes —
building it would mean inventing new C++ planning entry points, which is new engine surface,
not transport (brief §20, §58). `API_CONTRACT.md` itself has been annotated with a status
note pointing here; the corrections, summarized:

1. **No WebSocket.** Brief §31's explicit preference ("choose the simplest architecture...
   do not build a complicated WebSocket infrastructure yet") — REST polling only.
2. **No planning-request commands.** `plan`/`reselect`/`assign`/`getGraph`/`traceAlgorithm`/
   `exportResults` are not implemented; `CommandProtocol.cpp` rejects all of them by name
   (`tests/io/command_protocol_test.cpp`'s
   `PlanReselectAssignGetGraphTraceAreAllUnrecognized` is a permanent regression for this).
3. **One response shape.** Every command responds with a full `snapshot`, not the varied
   `planResult`/`assignmentResult`/`graphData`/`algorithmTrace` message zoo — there is
   nothing yet to produce those payloads from.
4. **`simulationStatus` is two-valued.** `RUNNING` | `PAUSED`, directly
   `SimulationClock::isRunning()` — the engine has no richer run-state machine
   (`STOPPED`/`COMPLETED` from the original sketch do not exist) to report honestly.
5. **No scenario-file loading.** `--serve` always runs the fixed canonical demo scenario;
   `{"cmd":"init","scenarioPath":...}` is not implemented.

## 10. Testing Strategy Across the Boundary

| Layer | What is verified | Where |
|---|---|---|
| C++ serialization | View projection (incl. Phase 10's graph/Pareto/explanation capture) against known/cross-checked values, JSON writer/parser round-trips, command parsing | `tests/io/*_test.cpp` (52 tests) |
| Node ↔ real engine | Process spawn, ready/command/snapshot exchange, crash/timeout handling, malformed-command resilience, the FULL scripted demo narrative to completion | `backend/test/engineProcess.test.ts` (8 tests, spawns the actual compiled binary) |
| Node REST surface ↔ real engine | Every REST route against the real engine process, brief §49's numbered checklist explicitly | `backend/test/server.test.ts` (7 tests) |
| NDJSON parsing in isolation | Fragmented chunks, multiple messages per chunk, malformed lines not corrupting neighbors | `backend/test/ndjsonParser.test.ts` (9 tests) |
| React state/service/page layer | Connection-state transitions, snapshot-replaces-not-merges, command dispatch, error/offline rendering, Mission Intelligence field derivation, event timeline selection, Pareto chart rendering, network graph rendering, mission history, UI navigation | `frontend/test/*.test.tsx` (51 tests, against a fixture snapshot — real values captured from an actual run, not invented) |

**Why the frontend's Three.js scene itself is not unit-tested**: `jsdom` (the frontend test
environment) has no WebGL context, and mocking one well enough to assert on rendered
geometry would test the mock, not the code. `Scene.tsx`/`OrbitRing`/`EntityMarker`/
`MissionLink` were instead verified by running the real dev server against the real backend
and real engine — building and typechecking cleanly, and the underlying data path
(fixture-driven `EngineContext`/`ObjectInspector`/`MissionIntelligence` tests, plus the
backend's real-engine tests proving the exact snapshot shape those components consume) is
what unit tests can and do cover directly. The Orbital Network and Pareto chart, being SVG
rather than WebGL, ARE unit-testable in `jsdom` and are (`OrbitalNetwork.test.tsx`,
`ParetoChart.test.tsx`).
