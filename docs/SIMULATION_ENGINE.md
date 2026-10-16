# ORCA — Simulation Engine

Version 1.0 · Phase 8 · 2026-09-03

How ORCA turns Phase 7's directly-callable `DynamicReplanner::evaluate()` into something a
running mission actually calls — a deterministic, fixed-timestep clock driving an event
queue, a single owned world state, and leg-by-leg mission execution, wired together so that
a material change in that world (a fuel drop, a debris hazard, a new casualty) reaches the
replanner and the replanner's decision reaches the mission, all through existing domain
state machines and nothing else.

---

## 1. Motivation

Phase 7's own scope statement said it plainly: *"the continuous simulation clock, the event
queue, debris detection, or anything that decides when to call the replanner — that is
Phase 8."* Phase 7 built the decision; Phase 8 builds the machinery that decides when to ask
for one, and the machinery that carries a mission from dispatch to completion in the first
place — before Phase 8, no code path anywhere in the repository advanced a mission through
time or consumed a vehicle's delta-v leg by leg. `SimulationEngine` is that first caller: it
owns a clock, a queue, and the world's state, and on every tick it processes due events,
advances every in-transit mission's execution, and — through those two paths — invokes
`DynamicReplanner`/`GlobalReplanner` exactly where the brief's minimum trigger list requires.

**Deliberately not built here**: any planning or transfer-physics logic of its own. Every
route search still runs through `optimization/CandidateGeneration.hpp` and
`algorithms/ParetoSearch.hpp` (Phase 5/6, unmodified); every feasibility/validity question
still runs through `planning/MissionValidity.hpp` and `DynamicReplanner.hpp` (Phase 7,
extended, not replaced); every transfer evaluation still runs through
`transfer/TransferModel.hpp` (Phase 3, untouched). `cpp/simulation/` is 100% orchestration —
clock, queue, state ownership, and the glue that calls the existing stack at the right
moments. Also deliberately not built: any frontend, WebSocket, or Node bridge code (Phase 9).

## 2. Files

```
cpp/simulation/
├── Event.hpp                 EventKind, eventPriorityTier(), the Event struct
├── EventQueue.hpp / .cpp     deterministic (timestamp, tier, id) schedule
├── SimulationClock.hpp / .cpp   fixed-timestep clock, no wall-clock dependency
├── SimulationState.hpp / .cpp   single source of truth for a running scenario
├── EmergencyGenerator.hpp / .cpp   scripted + seeded-random failure scheduling
├── MissionExecutor.hpp / .cpp   leg-by-leg mission progression, real resource spend
├── SimulationEngine.hpp / .cpp   the tick loop; owns clock/state/queue/graph/replanner
└── EventDispatcher.hpp / .cpp   free-function EventKind switch; the only place that
                                  mutates Mission/RescueVehicle/Emergency in response
                                  to an event or a replan decision

cpp/planning/MissionValidity.{hpp,cpp}   extended (not rewritten): evaluateMissionValidity()
                                          and its helpers gained an optional invalidatedEdge
                                          parameter (the debris-invalidation mechanism) and
                                          an optional legArrivalTimesOut output parameter
cpp/planning/DynamicReplanner.cpp        extended: evaluate() forwards invalidatedEdge and
                                          filters candidate routes through it, including a
                                          dominated-solution fallback (§10)

tests/simulation/
├── simulation_clock_test.cpp        16 tests
├── event_queue_test.cpp             10 tests
├── simulation_state_test.cpp        7 tests
├── emergency_generator_test.cpp     6 tests
├── mission_executor_test.cpp        5 tests
├── simulation_engine_test.cpp       7 tests
└── end_to_end_demo_test.cpp         1 test (§13)
```

No file exceeds ~300 lines; `EventDispatcher.cpp` (the largest, at the switch over 14 event
kinds) was kept as a free-function file separate from `SimulationEngine.cpp` for exactly this
reason, per the brief's own file-size guidance.

## 3. The Simulation Clock

`SimulationClock` (§brief 6-11) is a fixed-timestep logical clock with no notion of wall
time whatsoever — `std::chrono` appears nowhere in `SimulationClock.cpp`. `step()` always
advances `currentTimeSec()` by exactly `timeStepSec()` (default 1.0s, taken from
`Scenario::config().timeStepSec` — confirming the fixed-timestep convention that
`SimulationConfig` already declared back in Phase 2's domain model, now actually driving
something). `speedMultiplier()` is stored and exposed but never read internally by anything
in `cpp/simulation/` — it exists solely for a future real-time caller to decide how often to
call `step()` per wall-clock second; it changes nothing about `dt`, event ordering, or any
computed result. A run at speed 1 and the same run at speed 16 (same seed, same script)
produce byte-identical final state, because nothing in this codebase's simulation path ever
reads a multiplier to change what happens, only (potentially, for a future caller) how often
`step()` is invoked.

`start()`/`pause()`/`resume()` are idempotent — calling `start()` on an already-running clock
is a no-op, not an error. `step()`/`advance()` are no-ops (not exceptions) while paused, so a
caller need not guard every call with `isRunning()`.

## 4. Timestep Policy

The default timestep is 1.0 simulated second, matching `SimulationConfig::timeStepSec`'s own
existing default. `SimulationEngine::advance(seconds)` requires `seconds` to be a
non-negative whole multiple of `timeStepSec()` (validated to within `kEpsilon`) and then
calls `tick()` exactly that many times — never `clock_.advance(seconds)` followed by a single
event drain, which would jump straight to the destination time and could silently skip every
event scheduled strictly in between. This is asserted directly by
`SimulationEngine.LargeAdvanceProcessesEveryEventNoneSkipped` (§12): a single
`advance(1000.0)` call and one thousand individual `step()` calls produce identical
`eventsProcessed`, mission, and emergency counts.

## 5. State Ownership

`SimulationState` (`SimulationState.hpp`) is the single source of truth for everything that
exists during a run: every `Satellite`, `RescueVehicle`, `DebrisObject`, `Emergency`,
`Mission`, and the `MissionPlan` currently governing each mission, plus `SimulationMetrics`
and the `nextEmergencyId`/`nextMissionId`/`nextPlanId` counters that mint fresh identity for
entities created during the run. `SimulationEngine` holds exactly one `SimulationState`
(`state_`) and nothing else in `cpp/simulation/` keeps a competing copy — `EmergencyGenerator`
and `MissionExecutor` both take a `SimulationState&` (or the specific entities within it) and
mutate through validated domain APIs (`Satellite::applyDamage()`-style setters,
`Mission::setState()`, `RescueVehicle::consumeDeltaV()`) rather than writing fields directly.

Storage is `std::unordered_map<std::uint32_t, T>`, not `std::vector<T>`: `Mission`,
`Emergency`, `Satellite`, `RescueVehicle`, and `DebrisObject` all have deleted default
constructors (the project-wide "no sentinel values" convention), so a growing `vector`
risks a reallocation invalidating a reference `MissionExecutor` or `EventDispatcher.cpp`
is holding mid-tick. `unordered_map`'s node-based storage keeps every value's address
stable across insertions.

Satellites, vehicles, and debris keep the `Id` values `Scenario` assigned at load time.
Emergencies, missions, and mission plans are created *during* a run and get freshly minted
IDs from the counters above — the caller-managed-sequence convention `CandidateGeneration.hpp`
already established for `PlanId` in Phase 5/6.

## 6. Event Queue and Ordering

`Event` (`Event.hpp`) is a flat struct — `EventKind` plus optional `satellite`/`vehicle`/
`emergency`/`mission`/`debris`/`invalidatedEdge` IDs, optional `emergencyKind`/`severity`/
`survivalWindowSec`, and a free-text `detail` — the same "one shape, kind decides which
fields matter" choice `MissionChange.hpp` already made in Phase 7, for the same reason: one
`switch` in `EventDispatcher.cpp`, not a polymorphic hierarchy for a handful of fields.

`EventQueue` orders on `(timestampSec, eventPriorityTier(kind), id)`, strictly ascending, all
three keys compared in that order — never on `std::multiset`'s incidental insertion-order
behaviour for equal keys, which the standard does not actually guarantee. `id` is assigned
sequentially by `schedule()` (never a memory address, never random), so the same scenario
always produces the same IDs in the same order, which is what makes the same-timestamp tier
ordering below fully reproducible rather than merely usually-consistent.

Same-timestamp tier policy (`Event.hpp::eventPriorityTier()`), the brief's own five-tier
statement, implemented exactly:

| Tier | Kinds | Rationale |
|---|---|---|
| 1 — Environmental | `DebrisAlert` | Environmental facts are established before anything reacts to them. |
| 2 — Vehicle/system state | `SatelliteFailure`, `PropulsionFailure`, `CommunicationLoss`, `FuelCritical`, `VehicleStateChange` | State changes land before missions react to them. |
| 3 — Mission milestones | `MissionStart`, `Rendezvous`, `RescueStart` | What actually happened this tick. |
| 4 — Replanning triggers | `MissionReplan`, `EmergencyEscalation` | Consume the state changes above. |
| 5 — Completion notifications | `RescueComplete`, `MissionFailure`, `EmergencyResolved` | Summarise what already happened; nothing reacts to these within the same tick. |

`EventQueue.SameTimestampFiresInDocumentedTierOrder` schedules five events at one identical
timestamp in the *reverse* of this order and asserts they still pop in tier order —
registration order provably does not determine behaviour.

`std::multiset`, not `std::priority_queue`, backs the queue specifically because `cancel()`
needs to remove an arbitrary, already-scheduled event by ID — a side index
(`std::unordered_map<uint32_t, iterator>`) gives `O(log n)` cancellation, which a
`priority_queue` cannot do short of rebuilding the whole container.

## 7. Orbital Evolution

Unchanged from Phase 0-3: `Orbit::positionAtKm(t)` is a pure function of simulation time,
analytically derived from the circular two-body elements — there is no
`position += velocity * dt` mutation anywhere, in this phase or any earlier one.
`SimulationEngine::tick()`'s documented step 2 ("orbital positions are derived... nothing to
update explicitly") is a no-op *by construction*, not an omission: nothing in
`cpp/simulation/` stores a per-tick position at all. This is also why long runs cannot
accumulate positional drift (§11) — every position query re-derives from `t`, never from
the previous query's result.

## 8. Mission Execution

`MissionExecutor::advanceMissionProgress()` progresses an *already-approved* mission through
time; it never plans a route. It requires `mission.state() == InTransit` and that the
supplied `plan.route()` matches `mission.currentRoute()` exactly (both enforced,
`requireThat`-checked, and each covered by its own permanent test:
`RejectsNonInTransitMission`, `RejectsAPlanThatDoesNotMatchTheMissionsCurrentRoute`).

It computes per-leg timing via a small, local `computeLegTimings()` helper that walks the
whole route once, calling nothing but `TransferModel::evaluate()` — the one shared source of
transfer physics — under deliberately permissive constraints (the route was already
confirmed feasible when it was adopted; re-litigating feasibility is `MissionValidity.hpp`'s
job, not execution's). This is a small, single-purpose function distinct from
`MissionValidity.cpp`'s own `walkRoute()` only in what it reports per leg (arrival/departure
times and delta-v here, versus a composed `ObjectiveVector` cost there); both call nothing
else.

The function then completes every leg whose arrival time has passed as of `nowSec` — a
`while` loop, not a single-leg `if`, so a large `advance()` call correctly flies through
several completed legs in one call rather than only ever advancing one leg per call
(exercised directly by the multi-leg path of the end-to-end demo, §13). It does **not**
itself transition the mission out of `InTransit` on arrival — it returns
`ExecutionOutcome::Arrived`, and the caller (`SimulationEngine::tick()`) schedules a
`Rendezvous` event that fires on the *next* tick, which is the one place
(`EventDispatcher.cpp::handleRendezvous`) that actually calls `mission.setState(Rendezvous)`
then `mission.setState(Success)`. This one-tick lag is deliberate: it keeps `tick()`'s step 4
non-recursive (arriving does not trigger further execution within the same call).

## 9. Resource Consumption

Delta-v is consumed per leg, at the moment that leg's arrival time is actually reached — not
upfront at mission-plan time and not as a fixed increment — via
`vehicle.consumeDeltaV(leg.deltaVKms)` and the matching `mission.recordDeltaVSpent(...)`,
both of which accumulate monotonically and never reset. A replan mid-mission
(`Mission::adoptRoute()`) resets *route progress* (`currentLeg_`/`progressOnLeg_`, back to 0,
because the new route starts from the vehicle's current position) but never touches
`deltaVSpentKms_` — delta-v already spent on earlier legs, including legs of a
now-abandoned route, is never given back or reset. `MissionExecutor.
ResourceConsumptionIsCumulativeNeverReset` asserts this directly across several mid-flight
calls plus arrival.

**Idempotence guard (bug found and fixed during test-writing, not by a crash).** Calling
`advanceMissionProgress()` a second time after a mission has already arrived at its final leg
— exactly the sequence `SimulationEngine::tick()` itself produces, since the state transition
out of `InTransit` happens one tick later via the scheduled `Rendezvous` event, not
immediately — would otherwise re-enter the completion loop and re-consume the final leg's
delta-v a second time, because `currentLeg()` never advances past the final leg's index
(`advanceLeg()` is only called for non-final legs). Fixed with an explicit guard at function
entry: if the mission is already on its final leg with `progressOnLeg() >= 1.0`, the call
returns `Arrived` with zero consumption immediately. §14 details the discovery.

## 10. Emergency Generation

`EmergencyGenerator` only *schedules* `SatelliteFailure` events; it never touches a
satellite's health or constructs an `Emergency` directly — that happens in
`EventDispatcher.cpp::handleSatelliteFailure` only when the event actually fires. This
mirrors `DebrisObject.hpp`'s own stated principle: "the surprise is in the detection," not
the schedule.

Two modes, kept as genuinely separate code paths rather than one parameterised blend (so
tuning random-mode parameters can never perturb a scripted demo's exact timing):

- **Scripted** (`scheduleScriptedFailures`) — an exact list of `{atSec, satellite, kind,
  severity, survivalWindowSec}`, scheduled verbatim.
- **Seeded random** (`scheduleRandomFailures`) — draws `failureCount` *distinct* eligible
  satellites (excluding any already `Failed`/`Rescued`/`Lost`) without replacement via a
  caller-supplied `RandomGenerator` (always obtained through
  `RandomGenerator::forStream(RandomStream::Failures)`, never a fresh or global generator),
  each given a uniformly random time in `[0, durationSec]`. Eligible satellites are sorted by
  raw ID before drawing, since `unordered_map` iteration order is not guaranteed portable —
  the draw depends only on `rng`'s sequence, confirmed by
  `RandomFailuresAreReproducibleFromTheSameSeed` running two independent generators from the
  same seed and asserting an identical resulting schedule.

The drawn `kind`/`severity`/`survivalWindowSec` are carried on the `Event` itself from
scheduling time through to fire time, rather than re-derived when the event fires — re-drawing
at fire time would consume the random stream in an order dependent on which events actually
fire, which is not reproducible the moment a scenario's timeline changes at all.

## 11. Dynamic Replanning Integration

Every path from a world-state change to a replan decision goes through
`DynamicReplanner::evaluate()` or `evaluateGlobalReplan()` — Phase 7's pipeline, unmodified in
its search logic. `EventDispatcher.cpp` is the only caller:

| Trigger event | Handler | `MissionChange.kind` |
|---|---|---|
| `SatelliteFailure` | `handleSatelliteFailure` → `runGlobalReplanAndDispatchMissions` | (global replan, not a per-mission `MissionChange`) |
| `DebrisAlert` | `handleDebrisAlert` | `ChangeKind::DebrisDetected`, with `invalidatedEdge` set |
| `PropulsionFailure` | `handleVehicleDegradation` | `ChangeKind::VehicleUnavailable` |
| `FuelCritical` | `handleVehicleDegradation` | `ChangeKind::FuelChanged` |
| `CommunicationLoss` | `handleVehicleDegradation` | `ChangeKind::CommunicationLost` |
| Rendezvous completing (vehicle freed) | `handleRendezvous` → `runGlobalReplanAndDispatchMissions` | (global replan) |

**Debris invalidation is route-filtering, not graph-rebuilding.** `Mission.currentRoute()`
stores `EdgeId`s meaningful only relative to the one stable `MissionGraph` instance built at
`SimulationEngine` construction; rebuilding the graph would reassign `EdgeId`s from scratch
and orphan every existing mission's route. Instead, `handleDebrisAlert` uses a deliberately
simple proximity heuristic (`findThreatenedEdge` — either endpoint's orbit within
`config().debrisProximityThresholdKm` of the debris object's own semi-major axis; not
conjunction analysis) to find one threatened `EdgeId` on the mission's *remaining* route, and
passes it as `MissionChange::invalidatedEdge` into `evaluate()`. `MissionValidity.cpp` and
`DynamicReplanner.cpp` were both extended (not rewritten) to accept this: a leg using
`invalidatedEdge` is forced infeasible regardless of its physics, and both the mandatory and
optional search paths filter candidate routes through
`excludeSolutionsUsingEdge(front, invalidatedEdge)` before selecting a recommended plan. No
change was needed to `GraphBuilder`, `TransferModel`, or `TransferRiskModel` — the next full
graph rebuild (a future scenario reload) will naturally account for a changed environment
through the existing feasibility layer; this phase does not build a second route-planning
system inside the event code.

**`applyReplanDecision()`** (`EventDispatcher.cpp`) is the single place any event handler
mutates `Mission`/`RescueVehicle`/`Emergency` in response to a decision, entirely through
domain APIs:

- `Continue` — no-op.
- `Replan` — `mission.setState(Replanning)` → `mission.adoptRoute(newPlan)` →
  `mission.setState(InTransit)` (§14.3), records the new plan in `state.missionPlans`, and
  schedules a `MissionReplan` event.
- `Abort` — `mission.setState(Aborted)`, frees the vehicle (`Transferring → Returning → Idle`,
  each step checked with `canTransitionTo()`), returns the emergency to `Pending` so a later
  global replan can pick it back up with a different vehicle, and schedules a `MissionFailure`
  event.
- `Complete` — no-op (a rare race where the emergency was already resolved by the time
  `evaluate()` ran; `Rendezvous`/`RescueComplete` handling already covers the normal path).

**Global replan** (`runGlobalReplanAndDispatchMissions`) reruns Phase 6's exact pipeline
(`evaluateGlobalReplan`) over every currently `availableVehicleIds()`/`pendingEmergencyIds()`
— collected fresh at call time, never cached — and, for each resulting `Assignment`,
materialises a real `Mission`, walks vehicle/emergency state through their domain APIs, and
schedules a `MissionStart` event. It fires whenever a new emergency appears
(`handleSatelliteFailure`) and whenever a vehicle is freed by a completed rescue
(`handleRendezvous`) — both are single-threaded, sequential calls within one tick, so a
freshly-read `availableVehicleIds()` always reflects every mutation already applied earlier
in the same tick; no double-booking is possible even when two dispatch calls occur in the
same tick.

## 12. Deterministic Replay

Two independent `SimulationEngine` instances built from the same `Scenario` (same seed),
given the identical scripted event sequence and advanced by the identical amount, produce
identical `SimulationMetrics`, identical mission counts, and an identical final `Mission`
state (`state`, `deltaVSpentKms`, `completedAtSec`) —
`SimulationEngine.SameSeedAndScriptProduceIdenticalOutcomes`. `reset()` then re-running the
same script on the *same* engine instance reproduces the same outcome again
(`SimulationEngine.ResetThenRerunProducesTheSameOutcomeAsAFreshEngine`). Nothing in
`cpp/simulation/` reads wall-clock time, a global random generator, or unordered-map
iteration order for anything that affects an outcome — every source of nondeterminism a
C++ program can accidentally introduce was checked against directly (§14 lists what audit
each of the 16 categories received).

## 13. Reset Semantics

`SimulationEngine::reset()` returns the engine to its exact initial state: the clock to
`t=0`, not running; the world state rebuilt fresh via `makeInitialState(scenario_)` (which
also resets `SimulationMetrics` to all zeros); the event queue replaced with a fresh, empty
one; and the `DynamicReplanner` replaced with a fresh instance (`replanner_ = DynamicReplanner
(config_.replanPolicy)`) so its replan history/metrics — a record of decisions made, the same
category of thing `state_.metrics` is — also starts clean. The scenario itself (and therefore
its seed) is unchanged, which is exactly what makes re-running the same sequence of
`scheduleEvent()`/`step()`/`advance()` calls after `reset()` reproduce the same run.
`SimulationEngine.ResetRestoresExactInitialState` checks every one of these fields directly,
including that the event queue is empty and every vehicle's delta-v is back at its scenario
budget.

## 14. Numerical Stability and Long-Run Behaviour

Two properties make drift structurally impossible rather than merely unobserved in testing:

1. **Fixed timestep** — every tick advances by exactly the same `dt`; there is no adaptive or
   variable step that could accumulate rounding differences between runs.
2. **Analytical position, not integrated position** (§7) — `Orbit::positionAtKm(t)` recomputes
   directly from `t` every time it is called; there is no running position state to drift.

`SimulationEngine.LongRunProducesNoInvalidState` runs a full scenario to completion
(600,000 ticks) and asserts: no `NaN` in simulation time or any vehicle's remaining delta-v;
every vehicle's remaining delta-v stays non-negative; every mission's `progressOnLeg()` stays
within `[0,1]`; the tick counter matches the requested duration exactly; and the event queue
does not accumulate unboundedly (`size() < 100` after 600,000 ticks of an otherwise-quiet
run — every scheduled event was eventually drained, none left stranded).

`SimulationEngine.RepeatedIdenticalTriggersDoNotCauseUnboundedReplanning` fires the same
`CommunicationLoss` trigger against the same mission 20 times in a row and asserts
`replanCount()` stays at zero throughout — Phase 7's hysteresis (no materially better
alternative exists to switch to, on this scenario's single-topology graph) prevents thrashing
by construction, not by an ad hoc rate limiter.

## 15. Adversarial Self-Review

Sixteen categories were checked directly against the actual code (not merely reasoned about),
each with either a passing permanent test or a direct code-reading confirmation cited below.

| # | Question | Finding |
|---|---|---|
| 1 | FPS-dependence | `SimulationClock` never reads wall time; `speedMultiplier` is stored but unread internally. No bug. |
| 2 | Same-timestamp event ordering | `(timestamp, tier, id)` comparator, tested directly (§6). No bug. |
| 3 | Resource reset after replan | `adoptRoute()` resets route progress only; `deltaVSpentKms_` is untouched. Tested (§9). No bug. |
| 4 | Progress reset on replan | Deliberate: a new route starts from the vehicle's current position, not the mission's origin. By design. |
| 5 | `DynamicReplanner` cross-run statefulness | `reset()` rebuilds `replanner_` fresh (§13). No bug. |
| 6 | Bypassing domain state machines | Every mutation in `EventDispatcher.cpp` goes through `setState()`/`canTransitionTo()`/dedicated mutators; confirmed by reading every handler. No bug. |
| 7 | Double-execution of events | `EventQueue::pop()` removes from both the ordered set and the ID index; nothing re-schedules a fired event under its old ID. No bug. |
| 8 | Stale events surviving `reset()` | `queue_ = EventQueue()` on reset; asserted directly (§13). No bug. |
| 9 | RNG reproducibility | `randomStream()` derives only from `scenario_.seed()` via `forStream()`; no global random state anywhere in `cpp/simulation/`. Tested (§10, §12). No bug. |
| 10 | Mission completes but vehicle stays "assigned" | `handleRendezvous` calls `vehicle.clearMission()` before walking status to `Idle`. Tested by the end-to-end demo (§16). No bug. |
| 11 | Vehicle stuck unavailable after rescue | Same handler walks `Transferring → OnStation → Returning → Idle`, each step `canTransitionTo()`-checked against `RescueVehicle.hpp`'s own transition table. No bug. |
| 12 | Failed/aborted mission keeps executing | `tick()`'s step 4 iterates `activeMissionIds()` (excludes all three terminal states by construction) and additionally skips anything not `InTransit`. No bug. |
| 13 | Global replan double-booking | `availableVehicleIds()`/`pendingEmergencyIds()` are read fresh at each call, and Hungarian assignment cannot assign one vehicle twice within a single call; sequential single-threaded execution means no call ever sees stale availability. No bug. |
| 14 | Repeated unnecessary replanning | Hysteresis tested directly (§14). No bug. |
| 15 | Large-`dt` event skipping | `advance()` loops `tick()`, never jumps the clock directly; tested directly (§4). No bug. |
| 16 | Orbital drift over long runs | Positions are always analytically re-derived from `t`, never integrated; structurally impossible (§7, §14). No bug. |

**One genuine gap found and fixed by this review** (not one of the 16 named categories, but
surfaced while checking #6): `Mission.hpp`'s own state table declares `InTransit →
Replanning → InTransit` for exactly the moment a route is swapped mid-flight, but no caller —
neither this phase's `EventDispatcher.cpp` nor Phase 7's `DynamicReplanner` — ever actually
entered `Replanning`; `adoptRoute()` silently replaced the route while `mission.state()` read
`InTransit` throughout. Fixed in `applyReplanDecision()`'s `Replan` case by walking
`setState(Replanning)` → `adoptRoute()` → `setState(InTransit)`, both transitions confirmed
legal against `Mission.cpp::isValidTransition()`. This is synchronous within one function
call (no tick boundary passes between the two `setState()` calls), so it has no externally
observable timing effect today — its value is that `mission.state()` now genuinely reflects
the documented machine rather than only approximating it, which matters the moment a future
snapshot/telemetry consumer reads state mid-mutation. Full regression (588 tests) confirmed
no regression from this change.

Two further bugs were found earlier, during implementation and test-writing rather than this
review pass — both documented where they were fixed: the dominated-solution-pool fallback
gap in `DynamicReplanner::excludeSolutionsUsingEdge()` (§11; see §16 below for how it
surfaced) and `MissionExecutor`'s idempotence guard (§9).

## 16. The End-to-End Demo, and How the Two Route-Level Bugs Were Found

`tests/simulation/end_to_end_demo_test.cpp::EndToEndDemo.
PropulsionFailureThroughDebrisRerouteToRescueComplete` is the brief's own narrative,
hand-verified end to end: SAT-101 fails at t=5 → `handleSatelliteFailure` creates an
emergency and a global replan dispatches RV-01 on a real two-leg route (V1 and SAT-101 share
an altitude but differ in phase — a permanent same-period lock, so no direct edge exists) →
debris detected at t=25, threatening the leg through the first intermediate (MID-A) →
`DynamicReplanner` finds and adopts a genuine alternative through a second intermediate
(MID-B) → the mission completes: `Success`, emergency `Resolved`, satellite `Rescued`,
vehicle back to `Idle` with `deltaVRemainingKms ≈ 8.9910` (from a 10.0 budget). Every number
in the test came from compiling and running this exact scenario — nothing is asserted from
hand-derivation.

Reaching this scenario took four iterative attempts, and each of the first three failures was
a genuine finding, not a scenario-tuning triviality:

1. **Debris placed near an intermediate satellite's altitude, but the chosen route bypassed
   it entirely** — combined-burn direct transfers are almost always cheaper than staged ones
   (Phase 3's own established finding), so the search never routed through the intermediate
   the debris was meant to threaten. Not a bug — confirmed the search was choosing correctly.
2. **Debris placed exactly on the target's own altitude, on a single-leg mission** — still no
   threat detected. Tracing this down revealed that a mission's only leg was already
   in-progress (`progressOnLeg() > 0`) by the time the debris event fired, and Phase 7's own
   `remainingLegStartIndex()` correctly treats an in-progress leg as committed —
   un-invalidating delta-v already being spent is not a decision this system is meant to make.
   This revealed a genuine, if narrow, semantic: **a single-leg mission is effectively
   debris-immune once departed.** Documented, not treated as a defect — it is the correct
   consequence of an already-deliberate Phase 7 design choice. Fixed by using a genuinely
   multi-leg scenario instead (the same-period-lock technique above).
3. **A two-leg scenario with only one possible intermediate correctly excluded its only route
   and the mission aborted** — correct, since no alternative topology existed.
4. **A second intermediate (MID-B) was added as a genuine alternative, and the mission
   *still* aborted.** This was the real bug: `DynamicReplanner`'s Pareto search only retains
   *non-dominated* routes in `front.solutions`; the MID-B path, while physically feasible, was
   dominated by the (now-excluded) MID-A path on every objective, so it had never been placed
   in `front.solutions` at all — it existed only in `front.dominated` (kept for
   visualization, never previously used as a fallback pool). Fixed by extending
   `excludeSolutionsUsingEdge()`: if filtering `front.solutions` leaves it empty, the same
   filter is additionally applied to `front.dominated`, and any survivors are promoted into
   the result with `ideal`/`nadir` recomputed over them — not fabricating a solution, since
   every dominated route was already independently confirmed feasible by the same search that
   produced the original front. Re-running the scenario after this fix produced the intended
   `REPLAN` outcome. Full regression confirmed no breakage elsewhere.

## 17. Known Limitations

- **Debris proximity is a semi-major-axis threshold heuristic**, not conjunction analysis —
  explicitly documented as a simplification in `SimulationEngineConfig`'s own comment, not a
  claim of physical accuracy.
- **Single-leg missions cannot be rerouted by an in-flight environment change** — §16.2's
  finding. This is Phase 7's own deliberate "a leg in progress represents delta-v already
  committed" semantic, inherited rather than introduced here, but worth restating: only a
  mission with at least one *not-yet-departed* leg can benefit from `invalidatedEdge`
  filtering.
- **`MissionState::Failed` is never actually reached by anything in this phase.** Every path
  that ends a mission early goes through `Abort`, matching Phase 7's own established
  reasoning that `Mission::emergency()` has no setter, so a "failure" always resolves to
  freeing the vehicle and returning the emergency to `Pending` rather than retargeting.
  `Failed` remains a valid, tested transition target in `Mission.hpp`'s state table for a
  future phase that might, for example, model a vehicle physically destroyed mid-transfer.
- **No spatial index for debris/threat queries** — `findThreatenedEdge()` is a linear scan
  over a mission's remaining route, adequate at this phase's scenario sizes (per-mission
  route lengths of a handful of legs), not a claim that it would scale to a very large graph
  without revisiting.
- **Emergency severity/failure statistics in `EmergencyGenerator`'s random mode are a
  uniform-distribution placeholder**, explicitly documented in `RandomFailureConfig`'s own
  comment as "not a calibrated reliability model."

## 18. Readiness for Frontend Integration (Phase 9 preview)

`SimulationState` already carries everything a future snapshot API would need to expose for
rendering: positions are always derivable from each entity's orbit and the current
`simulationTimeSec`; every mission's route, current leg, and progress are present; every
emergency's state and every vehicle's status/remaining delta-v are present; `SimulationMetrics`
gives the summary counters a dashboard would want. No visualization code exists in this phase
(by design, and by the brief's explicit prohibition) — Phase 9's job is a pull-based snapshot
serializer and a Node bridge, reading this state, never computing anything the engine has not
already computed. This document does not specify Phase 9's shape further than that; it is
recorded here only as a statement of what Phase 8 leaves ready to build on.
