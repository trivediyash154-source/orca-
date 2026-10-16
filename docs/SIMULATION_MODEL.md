# ORCA — Simulation Model

Version 1.2 · updated after Phase 8 · 2026-09-03

> **Scope statement, stated once and meant throughout.** This is a **simulation model**
> designed to produce a physically *plausible* and internally *consistent* optimization
> problem. It is **not** a validated astrodynamics tool and must never be described as one.
> Every simplification is listed in §8.

---

## 1. Constants

```cpp
inline constexpr double kMuEarth      = 398600.4418;  // km³/s²
inline constexpr double kEarthRadius  = 6378.137;     // km (equatorial)
inline constexpr double kPi           = 3.14159265358979323846;
```

`μ` and `R⊕` are WGS-84 values. They are the only physical constants; every other number in
this document is a **model parameter**, tunable in `configs/`, and labelled as such.

---

## 2. Simulation Clock

Fixed-step, deterministic:

```
t ← 0
each step:  t ← t + Δt          Δt = timeStepSec, default 1.0 s
```

| Control | Effect |
|---|---|
| `speedMultiplier` | Steps executed per wall-clock second. **Does not change `Δt`.** |
| `pause` / `resume` | Halts stepping |
| `step(n)` | Executes exactly `n` steps then pauses |

**The critical property:** `speedMultiplier` changes how fast the simulation is *watched*,
never how it is *computed*. A run at 1× and the same run at 16× produce **identical state
histories**. Wall-clock time never enters simulation logic. This is what makes FR-N5
(determinism) achievable at all.

> **Status as of Phase 8: built as `simulation/SimulationClock.{hpp,cpp}`
> (`SIMULATION_ENGINE.md` §3), matching this section closely.** One correction: there is no
> `step(n)` — the built API is `step()` (exactly one tick) and `SimulationEngine::advance
> (seconds)` (N whole ticks, looped rather than jumped, so no due event is silently
> skipped — `SIMULATION_ENGINE.md` §4). The determinism property above is not verified by a
> state-hash comparison test; it holds structurally, since `SimulationClock` never reads
> `speedMultiplier()` internally at all (confirmed by code inspection — no
> `std::chrono` appears in the file), and is exercised end-to-end by
> `SimulationEngine.SameSeedAndScriptProduceIdenticalOutcomes`.

---

## 3. Orbital Propagation

### 3.1 Model

Circular, two-body, analytically propagated. No numerical integration.

```
T(a) = 2π √(a³ / μ)                    orbital period, s
ω(a) = 2π / T(a) = √(μ / a³)           mean motion, rad/s
θ(t + Δt) = (θ(t) + ω·Δt) mod 2π       phase advance
v(a) = √(μ / a)                        circular speed, km/s
```

Position in ECI from orbital elements `(a, i, Ω, θ)`:

```
x = a·(cos Ω·cos θ − sin Ω·sin θ·cos i)
y = a·(sin Ω·cos θ + cos Ω·sin θ·cos i)
z = a·(sin θ·sin i)
```

### 3.2 Validation

Compiled and run against textbook references (`REPOSITORY_AUDIT.md` §4):

| Quantity | Model | Reference | |
|---|---|---|---|
| `v` at 500 km | 7.6126 km/s | ≈7.61 km/s | ✅ |
| `T` at 500 km | 94.62 min | ≈94.6 min | ✅ |
| `T` at 800 km | 100.87 min | ≈100.9 min | ✅ |

### 3.3 Why analytic propagation

- **Exact for the model.** Circular two-body motion has a closed form; integrating it would
  add error, not remove it.
- **Deterministic.** No accumulated floating-point drift from step-wise integration — `θ(t)`
  is computed from `t` directly, so the state at `t = 5000` is identical whether reached in
  one step or five thousand.
- **`O(1)` per object per tick.** Essential at 1000 objects.
- **Reversible.** Time can be scrubbed backwards, which the presentation mode uses.

---

## 4. Transfer Model

### 4.1 Hohmann transfer

Between circular coplanar orbits `r₁ → r₂`:

```
a_t  = (r₁ + r₂)/2                              transfer semi-major axis
Δv₁  = √(μ/r₁) · ( √(2r₂/(r₁+r₂)) − 1 )         departure burn
Δv₂  = √(μ/r₂) · ( 1 − √(2r₁/(r₁+r₂)) )         arrival burn
Δv   = |Δv₁| + |Δv₂|
t_t  = π √(a_t³ / μ)                            half the transfer ellipse period
```

Validated: LEO(300 km) → GEO gives **Δv = 3.8926 km/s**, **t = 316.5 min (5.28 h)** against a
textbook ≈3.9 km/s and ≈5.25 h.

### 4.2 Plane change

```
Δv_plane = 2 · v · sin(Δi / 2)
```

Validated: 1° at 500 km ⇒ **0.1329 km/s (132.9 m/s)**.

### 4.3 The finding that shapes the whole cost structure

| Manoeuvre | Δv |
|---|---|
| Plane change 1° @ 500 km | 0.1329 km/s |
| Hohmann 500 km → 800 km (both burns) | 0.1608 km/s |
| **Ratio** | **0.83** |
| **Break-even** | **≈ 1.21°** |

**One degree of inclination change costs 83% of a 300 km altitude raise.** About 1.2° of
inclination *equals* it.

Consequences that propagate through the whole design:

1. **Inclination dominates.** In LEO, any realistic inclination spread (tens of degrees)
   swamps any realistic altitude spread. A rescue vehicle in a different plane is effectively
   unreachable regardless of how close it looks.
2. **Euclidean distance is a physically wrong proxy for cost.** Two satellites 100 km apart in
   different planes cost far more to connect than two 500 km apart in the same plane. This is
   why `distance` is a *separate, minor* objective in ORCA rather than a stand-in for fuel —
   and it is why a nearest-neighbour heuristic has something real to lose in the benchmarks.
3. **The 4th objective earns its place.** `distance` exists to expose this discrepancy, not to
   pad the objective count.

> **Correction of record.** An earlier Phase 0 document stated that a 1° plane change costs
> *more* than the 500→800 km raise. That is false: 0.1329 < 0.1608. The corrected figures
> above are authoritative. See `REPOSITORY_AUDIT.md` §4.1.

### 4.4 Combined manoeuvre

Doing the plane change *at apoapsis of the transfer*, combined with the circularisation burn,
is cheaper than doing it separately — vector addition beats scalar addition:

```
Δv_combined = √( v₁² + v₂² − 2·v₁·v₂·cos(Δi) )
```

ORCA models `COMBINED_PLANE_CHANGE` edges with this formula and takes
`min(separate, combined)`, because a planner that ignored the combined manoeuvre would
systematically overprice inclined transfers.

### 4.5 Phasing — where ORCA's central trade-off comes from

A Hohmann transfer only works if the target is at the right place on arrival. It rarely is.

Two orbits return to the same relative geometry once per **synodic period**:

```
T_syn = 1 / |1/T₁ − 1/T₂|
```

Measured for 500 km vs 800 km: **1525.3 min ≈ 25.4 hours.**

**A survival window is tens of minutes. The ideal launch window recurs about once a day.**
So the planner essentially never has the option of waiting for perfect alignment. It must
choose between:

| Option | Cost |
|---|---|
| **Wait** for a better phase | pays `time` — often more than the casualty has |
| **Go now** | pays `fuel` — a non-optimal transfer |

> ### ⚠ Superseded by Phase 2 — the phase-penalty model breaks FIFO
>
> This section previously specified pricing an off-phase departure with a Δv penalty
> `kPhasePenalty · (1 − cos φ) / 2`, letting the vehicle leave immediately and pay for the
> awkward geometry.
>
> **That model violates the FIFO property** required by §4.6 and `ALGORITHMS.md` §3.4. If
> time cost varies with phase error, a departure one minute later can arrive *earlier*,
> because the phase improved by more than the minute cost. Measured across 4000 randomly
> sampled orbit pairs, it violated FIFO in **827** of them, concentrated in LEO→MEO/GEO
> transfers — exactly ORCA's multi-band scenarios. The condition is
> `k < 2 / (coast · |Δω|)`, which large altitude differences comfortably breach.
>
> Without FIFO, Dijkstra's optimality claim is void, so the model was replaced rather than
> the requirement relaxed.

**ORCA instead waits for the rendezvous window**, then flies the optimal transfer:

```
α       = transferArc − ω_target · coast     required lead angle at departure
wait(t) = angular gap to α, divided by the relative drift rate
arrival(t) = t + wait(t) + coast
```

Because the absolute window time is non-decreasing in `t` and `coast` is constant,
**arrival is non-decreasing by construction** — FIFO holds unconditionally rather than
conditionally. Full derivation and the 143 991-comparison verification:
`TRANSFER_MODEL.md` §8 and §13.

### 4.5.1 Where the time/fuel tension went

The penalty model's appeal was a continuous trade-off *within a single edge*. The wait
model has one Δv per edge, so that particular framing is gone.

**The tension itself is not.** It moved from within an edge to between routes: a direct
transfer may cost little Δv and 25 hours of waiting, while a multi-leg route through an
intermediate orbit costs more Δv and far less waiting, because synodic periods differ per
orbit pair. That is still a genuine time/fuel Pareto tension, still derived from the
measured synodic period rather than asserted — and it is arguably the better version,
because it is **discovered by graph search**, which is what makes graph search necessary.

### 4.6 FIFO guarantee

`ALGORITHMS.md` §3.4 requires that departing later never arrives earlier. ORCA enforces this
structurally: waiting is modelled as **explicit wait time on the edge**, so

```
arrival(t) = t + wait(t) + coast
```

is non-decreasing in `t` by construction. The transfer model exposes no manoeuvre that
"overtakes" an earlier departure.

**Status after Phase 2: established.** 143 991 ordered comparisons, 0 violations, worst
backwards excursion 8.1e-15 relative (a few ulp of rounding on a mathematically flat
staircase). Proof and method: `TRANSFER_MODEL.md` §13. A failure would invalidate
Dijkstra's optimality, so it is treated as a correctness bug in the model, never as a
tolerance to loosen.

### 4.7 The stronger property FIFO does not cover

Because all phase dependence is absorbed into the wait, **Δv, fuel, risk and distance are
independent of departure time — only time varies.**

This matters more than FIFO for the multi-objective search. Dominance pruning is sound only
if extending a dominating label is never worse (`ALGORITHMS.md` §5.6). Had fuel also varied
with departure time, an earlier-but-cheaper label could extend to a *more expensive* edge,
and the Pareto search would silently discard optimal routes while appearing to work. See
`TRANSFER_MODEL.md` §13.4.

---

## 5. Risk Model

> **This is a simulation risk model.** It produces a plausible, monotonic, tunable hazard
> score in `[0,1]`. It is **not** a collision-probability estimate and is not calibrated
> against any real conjunction-assessment method.

### 5.1 Factors

```
risk_edge = clamp( w_d·debris + w_c·complexity + w_f·fuelMargin
                 + w_m·comms + w_h·health , 0, 1 )
```

| Factor | Definition | Rationale |
|---|---|---|
| `debris` | Σ over debris near the corridor of `density · f(distance)`, `f` decaying to 0 at the hazard radius | Proximity to tracked debris |
| `complexity` | normalised `(Δv_total + plane-change share)` | Harder manoeuvres have more ways to fail |
| `fuelMargin` | `1 − (Δv_remaining_after / Δv_budget)` | Arriving nearly dry is dangerous |
| `comms` | `1 − commsHealth` of the participants | Degraded comms complicates rendezvous |
| `health` | `1 − min(subsystem health)` of the vehicle | A degraded vehicle is a riskier vehicle |

Weights `w_*` live in `configs/risk.json`, sum to 1, and are documented as tunable.

### 5.2 Debris proximity

```
f(d) = 0                       if d ≥ R_hazard
f(d) = (1 − d/R_hazard)²       otherwise
```
Quadratic falloff: chosen so risk rises sharply only in genuine near-misses rather than
linearly across the whole neighbourhood. Evaluated via the spatial index (`ALGORITHMS.md` §9).

### 5.3 Composition along a path

```
risk_path = 1 − Π (1 − risk_edgeᵢ)
```

Treats leg hazards as independent. Two 10% legs give **19%**, not 20%.

Three properties matter, and all three are required by the search:

1. **Bounded** in `[0,1]` — cannot exceed 1 however many legs are added.
2. **Monotonically non-decreasing** — adding a leg never reduces path risk. *This is what
   `ALGORITHMS.md` §5.6 needs for dominance pruning to be sound.*
3. **Order-independent** — the product commutes, so risk does not depend on leg ordering.

Independence is a simplification: real hazards correlate (a debris cloud threatens
consecutive legs). Listed in §8.

---

## 6. Fuel Model

> **Relative Δv budget fraction. Not propellant mass.** ORCA does not model spacecraft mass,
> so the rocket equation is not applied and kilograms are never quoted.

```
fuelCost_edge     = Δv_edge / vehicle.deltaVBudget          ∈ [0,1]
fuelCost_path     = Σ fuelCost_edgeᵢ
deltaVRemaining  ← deltaVRemaining − Δv_edge                 monotonically decreasing
```

A displayed "13.6% fuel cost" means **13.6% of that vehicle's own total Δv capability**. It is
**not comparable across vehicles with different budgets** — RV-02's 13.6% and RV-03's 13.6%
are different quantities of Δv. This is the easiest number in the product to misread, so the
UI always shows it attached to a named vehicle.

Feasibility is a hard constraint, never a penalty term:

```
feasible(plan) ⟺ Σ Δv_edge ≤ vehicle.deltaVRemaining
```

An infeasible plan is **excluded from the graph**, not scored badly. A scalar penalty could
let an infeasible plan outrank a feasible one; exclusion cannot.

---

## 7. Deterministic Random Generation

### 7.1 Requirement

Same seed + same config ⇒ identical run (FR-N5). This is required for tests, benchmarks,
research reproducibility, and debugging.

### 7.2 Mechanism

```cpp
class RandomGenerator {
    std::mt19937_64 engine_;
public:
    explicit RandomGenerator(Seed s) : engine_(s) {}
    RandomGenerator forStream(std::uint64_t streamId) const;   // splitting
};
```

**Stream splitting.** Each subsystem draws from its own derived generator:

```
seed_orbits      = hash(masterSeed, 0x01)
seed_failures    = hash(masterSeed, 0x02)
seed_debris      = hash(masterSeed, 0x03)
seed_severity    = hash(masterSeed, 0x04)
```

This is what makes the design robust rather than merely deterministic: adding a debris object
does not shift the failure schedule, because they draw from independent streams. Without
splitting, any change to one subsystem's draw count would silently perturb every later draw in
the whole run and destroy cross-run comparability.

### 7.3 Rules

- **`std::random_device` is never used.** Anywhere.
- Wall-clock time never enters simulation logic.
- Iteration over unordered containers never affects results — ordered containers or explicit
  sorts are used wherever iteration order is observable.
- Floating-point operation order is fixed; no reliance on parallel reduction order.
- `std::mt19937_64` is specified by the standard, so it produces identical sequences across
  compilers and platforms — unlike the distribution objects, which are *not* specified.
  **ORCA therefore implements its own uniform/normal transforms** rather than using
  `std::uniform_real_distribution`, whose output is implementation-defined.

That last point is subtle and is the difference between "deterministic on my laptop" and
"deterministic anywhere".

### 7.4 Verification

`TEST_PLAN.md` §5: run the same seed 10 times, hash the full state at every 100th tick, assert
all hashes identical. Then run at speed 1× and 16×, assert identical.

---

## 8. Simplifications and Limitations

Complete and deliberate. Each states what is assumed and what it costs.

| # | Simplification | Consequence |
|---|---|---|
| 1 | **Circular orbits only** — eccentricity not modelled | No elliptical or transfer-orbit parking; real rescue orbits are often elliptical |
| 2 | **Two-body only** — no J2, drag, SRP, third-body | Orbits never decay or precess. Real LEO orbits precess several °/day, which would change plane-change costs over a long scenario |
| 3 | **Impulsive burns** — instantaneous Δv | No finite-burn losses; no low-thrust/electric propulsion |
| 4 | **Patched Hohmann + phasing penalty**, not Lambert | True optimal transfers between arbitrary states need a Lambert solver; ORCA approximates. See `RESEARCH_EXTENSION.md` §3 |
| 5 | **No mass modelling** — no rocket equation | Fuel is Δv fraction; propellant mass in kg cannot be reported |
| 6 | **Risk is a designed heuristic**, not a probability | Numbers are self-consistent and comparable *within* ORCA; they mean nothing outside it |
| 7 | **Hazards assumed independent** in risk composition | Real correlated hazards (one debris cloud, several legs) are underestimated |
| 8 | **Synthetic scenarios**, no TLE/real catalogue | Findings are about the *algorithms*, not about real orbital traffic |
| 9 | **Rendezvous is instantaneous** on arrival | No proximity operations, docking, or approach phase |
| 10 | **No refuelling** — `deltaVRemaining` only decreases | Multi-mission vehicle logistics not modelled |
| 11 | **Survival windows are deterministic** given severity | Real degradation is stochastic and uncertain |
| 12 | **Perfect information** (once detected) | Positions are known exactly; no sensor or state-estimation error |
| 13 | **Reactive replanning only** | No planning under uncertainty over possible futures |
| 14 | **Earth is a point mass** | No oblateness, no terrain, no atmosphere |

### 8.1 What these limitations do *not* undermine

The simplifications constrain the *physical* claims. They do **not** weaken the algorithmic
claims, which are the point of the project:

- The graph is genuinely sparse, directed and time-dependent.
- Objectives genuinely conflict — from a measured synodic period, not an assumption.
- Dominance, label-setting, Dijkstra, A* and Hungarian are exact and independently verifiable
  regardless of where the edge weights came from.
- Determinism and reproducibility are unaffected.

The correct summary is: **ORCA solves a real optimization problem over an approximate physical
model.** That sentence is defensible; "ORCA simulates orbital rescue" without qualification is
not.

---

## 9. Emergency Generation

### 9.1 Two mechanisms, both reproducible

**Scheduled** (demo, tests): explicit entries in the scenario file.
```json
{"atSec": 412.0, "target": "SAT-017", "kind": "PROPULSION_FAILURE", "severity": "CRITICAL"}
```

**Stochastic** (benchmarks, research): drawn from `seed_failures` with a configured hazard
rate. Poisson arrivals; target and kind sampled from configured weights.

Both are deterministic. Scheduled failures make the demo narratively controllable; stochastic
failures make the benchmark statistically meaningful. Neither is more "real" than the other.

> **Status as of Phase 8: both mechanisms are built** (`simulation/EmergencyGenerator.{hpp,
> cpp}`, `SIMULATION_ENGINE.md` §10), simpler than sketched above. Scripted failures are a
> plain in-code `std::vector<ScriptedFailure>` (no JSON scenario-file loader exists yet —
> `Scenario` itself has no `scheduledFailures` field, `DATA_MODEL.md` §8's correction).
> Stochastic mode draws `failureCount` *distinct* satellites uniformly at random (not a
> Poisson-arrival process) with a uniformly random time in `[0, durationSec]` and uniformly
> random kind/severity — `RandomFailureConfig`'s own comment states this plainly as "not a
> calibrated reliability model," matching this document's own simulation-not-astrodynamics
> scope statement at the top of the file. Both modes only *schedule* a `SatelliteFailure`
> event; nothing about §9.3's priority recomputation changed in Phase 8 (that remains Phase
> 6's `MissionScorer`/priority machinery, called when `EventDispatcher.cpp` constructs the
> resulting `Emergency`).

### 9.2 Survival window

```
window = baseWindow(kind) · severityFactor(severity) · (1 + jitter)
```
`jitter` is drawn from `seed_severity` and bounded (default ±10%). Base windows and severity
factors are in `configs/emergencies.json`.

### 9.3 Priority

Priority is **recomputed every tick**, because urgency is a function of the *remaining*
window, not the original one:

```
urgency    = 1 − clamp(remainingWindow / originalWindow, 0, 1)
priority   = w_s·severityScore + w_u·urgency + w_f·feasibilityScore
```

Consequence worth noting: a MODERATE emergency close to expiry can overtake a fresh CRITICAL
one. That is intended — a casualty about to be lost is a more urgent call on resources than
one with an hour of margin. `OPTIMIZATION_MODEL.md` §7 covers the weights.

---

## 10. Mission Execution

> **Status as of Phase 8: built as `simulation/MissionExecutor.{hpp,cpp}`
> (`SIMULATION_ENGINE.md` §8-9), and the actual behaviour corrects two real assumptions this
> section made before any of it was built.** Both corrections are stated below in place of
> the original sketch, not alongside it, since the original numbers were never implemented
> and would otherwise read as a second, contradictory description of the same mechanism.

Per tick, for each in-transit mission whose current leg's arrival time has passed:

```
leg          ← timings[currentLeg]              # precomputed once per call, via TransferModel
vehicle.consumeDeltaV(leg.deltaVKms)             # the WHOLE leg's Δv, at arrival — not accrued
mission.recordDeltaVSpent(leg.deltaVKms)
if isOnFinalLeg(): progressOnLeg ← 1; → Arrived (caller schedules Rendezvous next tick)
else:              currentLeg++; progressOnLeg ← 0
```

For a leg not yet arrived, `progressOnLeg` is set directly from elapsed-time fraction
(`(now − legDeparture) / legDuration`, clamped to `[0,1]`) — the same *value* §10's original
sketch described, computed by direct fraction rather than incremental per-tick accrual (both
are equivalent for a mission observed only at tick boundaries, which is the only way this
codebase ever observes one).

**Correction 1 — delta-v is consumed all at once, at leg arrival, not accrued linearly
across the leg.** The original sketch's `deltaVSpent += leg.deltaV · (Δt / leg.transferTime)`
was never built; `RescueVehicle::consumeDeltaV()`/`Mission::recordDeltaVSpent()` are each
called exactly once per leg, when that leg's arrival time is reached. This means a mission
aborted or replanned strictly *mid-leg* has spent **zero** delta-v on that leg, not "a
sensible fraction" — the leg's cost is only incurred on actually completing it. This is a
deliberate simplification (impulsive-burn accounting matches how `TransferModel::evaluate()`
already prices a leg — one delta-v number for the whole leg, not a rate), stated here as the
correction it is: real, and different from what this document originally described.

**Correction 2 — `MissionState::Failed` is never produced by anything in this codebase.**
Neither of the original sketch's `FAILED` rows (`t > emergency.deadline`,
`deltaVRemaining < 0`) exists as an executed check anywhere. Deadline/feasibility
reassessment is Phase 7's `MissionValidity`/`DynamicReplanner` machinery (invoked by
`EventDispatcher.cpp` on the triggers `SIMULATION_ENGINE.md` §11 lists), and every path that
ends a mission early resolves to `Abort`, not `Failed` — matching Phase 7's own established
reasoning that `Mission::emergency()` has no setter, so there is no "retarget," only "free
the vehicle and return the emergency to Pending." `deltaVRemaining < 0` cannot occur in
practice either: `consumeDeltaV()` throws on overspend rather than driving the value
negative, and a route is only ever adopted after `MissionValidity`/the search layer already
confirmed it affordable. `Failed` remains a legal, tested transition in `Mission.hpp`'s state
table (`SIMULATION_ENGINE.md` §17), reserved for a future phase, not removed.

---

## 11. Parameter Reference

Every tunable, with its home. **No magic numbers in code.**

| File | Contains |
|---|---|
| `configs/transfer.json` | max wait horizon, same-radius tolerance, combined-manoeuvre toggle. **No `kPhasePenalty`** — the penalty model was removed in Phase 2 (§4.5) |
| `configs/risk.json` | risk weights `w_*`, hazard radii, falloff exponent |
| `configs/emergencies.json` | base windows, severity factors, jitter bound, hazard rate |
| `configs/priority.json` | `w_s`, `w_u`, `w_f` |
| `configs/profiles.json` | the five optimization profiles |
| `configs/simulation.json` | `Δt`, snapshot rate, `switchMargin`, label caps, `ε` |

Only `kMuEarth`, `kEarthRadius` and `kPi` are compiled-in constants, because only they are
physical facts.
