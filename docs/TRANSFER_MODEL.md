# ORCA — Transfer Model

Version 1.0 · Phase 2 · 2026-09-02

> **Scope statement.** This is a **simplified transfer-cost model** built to support a
> planning problem. It is not an astrodynamics package, and no result it produces should be
> described as a mission-grade transfer solution. Every assumption is listed in §14.

---

## 1. Purpose

Answer one question, for one ordered pair of orbits, at one instant:

> Given a source orbital state, a target orbital state and a departure time, what transfer
> is available, and what does it cost in time, delta-v, risk and distance?

That is the whole job. The subsystem builds no graphs, runs no searches, schedules nothing
and knows nothing about missions. It exists because **the graph layer cannot be written
until edge weights can be computed**, and ORCA's edge weights are functions of departure
time:

```
w(edge, t_departure)  →  { time, fuel, risk, distance }
```

### 1.1 Where it sits

```
Orbit  →  Transfer Model  →  Feasibility  →  Time-dependent edge cost
                                                      ↓
                                              Mission Graph (Phase 3)
                                                      ↓
                                              Dijkstra / A*  →  Pareto planning
```

---

## 2. Inputs

```cpp
TransferResult evaluate(const Orbit& source,
                        const Orbit& target,
                        Seconds departureTimeSec,
                        const TransferConstraints& constraints) const;
```

| Input | Meaning |
|---|---|
| `source`, `target` | Circular orbits: radius, inclination, RAAN, phase at epoch |
| `departureTimeSec` | Absolute simulation time at which the vehicle is free to leave |
| `constraints` | Vehicle capability **and** planning policy — see §11 |

Orbits are passed **by value**. `Orbit` is four doubles; copying is cheaper than the
lifetime reasoning references would demand across the graph builder's `O(V²)` sweep.

---

## 3. Outputs

```cpp
struct TransferResult {
    TransferGeometry geometry;      // what the physics requires
    bool feasible;
    TransferFailureReason failureReason;
    Fraction fuelCost;              // planning objective
    double risk;                    // [0,1] simulation risk
    ObjectiveVector objectives() const;   // the bridge to Phase 3
};
```

Two rules govern this type:

**No sentinel values.** Failure is carried by `feasible` and `failureReason`. Numeric
fields always mean what they say — there is no `-1` or `999999` standing in for "unknown".

**Infeasible results keep their numbers.** A rejected transfer still reports the delta-v it
would have required, so the UI can say *"requires 2.91 km/s; RV-02 has 2.16 km/s"* rather
than merely *"no"*. This is what makes `bindingConstraint` in `API_CONTRACT.md` §4.4
possible.

### 3.1 Failure reasons

| Reason | When |
|---|---|
| `Feasible` | every constraint satisfied |
| `InvalidOrbit` | non-finite or physically impossible input |
| `PhasingUnavailable` | no rendezvous window, or none within the wait horizon |
| `DeltaVExceeded` | required delta-v beyond the vehicle's remaining budget |
| `FuelInsufficient` | required fuel fraction beyond what remains |
| `DeadlineExceeded` | arrival would fall after the caller's latest acceptable time |
| `DistanceExceeded` | transfer arc longer than policy permits |
| `RiskExceeded` | estimated risk above the ceiling |

**An infeasible transfer is a legitimate model result, not an application error.** Under
ORCA's hard-constraint design (`OPTIMIZATION_MODEL.md` §9) infeasibility is frequent and
expected; it is how the planner learns the shape of what is possible.

---

## 4. Orbital assumptions

Inherited from `SimplifiedOrbitalModel` (Phase 1) and extended here:

- circular source orbit
- circular target orbit
- coplanar for the base transfer; inclination handled by a separate plane-change term
- two-body motion — no J2, drag, solar radiation pressure or third-body effects
- impulsive burns — instantaneous, no finite-burn losses, no burn duration modelled
- `μ = 398600.4418 km³/s²`, `R⊕ = 6378.137 km` (WGS-84)

---

## 5. Hohmann model

The minimum-energy two-impulse transfer between coplanar circular orbits.

```
a_t  = (r₁ + r₂) / 2                          transfer semi-major axis
v_c(r)   = √(μ / r)                           circular speed
v_t(r)   = √(μ · (2/r − 1/a_t))               speed on the transfer ellipse (vis-viva)
Δv₁  = |v_t(r₁) − v_c(r₁)|                    departure burn (coplanar)
Δv₂  = |v_t(r₂) − v_c(r₂)|                    arrival burn (coplanar)
coast = π · √(a_t³ / μ)                        half the ellipse period
```

### 5.1 Validation

| Case | Model | Independent reference | |
|---|---|---|---|
| 500 → 800 km, Δv | 0.160758528 km/s | 0.160758528 | ✅ |
| 500 → 800 km, coast | 2931.847068 s | 2931.847068 | ✅ |
| 300 km → GEO, Δv | 3.892554387 km/s | ≈3.9 km/s (textbook) | ✅ |
| 300 km → GEO, coast | 18990.131738 s (5.28 h) | ≈5.25 h (textbook) | ✅ |

Also asserted as **identities**, which constrain the model's shape rather than a sampled
value: descent costs exactly what ascent costs with the burns exchanged; the coast equals
`π√(a³/μ)`; transfer speed exceeds circular at periapsis and falls below it at apoapsis;
for `r₁ = r₂` the coast is exactly half an orbital period.

---

## 6. Plane-change model

```
pure:      Δv = 2 · v · sin(Δi / 2)
combined:  Δv = √(v₁² + v₂² − 2·v₁·v₂·cos Δi)
```

The combined form is the law of cosines on the velocity triangle: the burn is the vector
difference between the incoming and desired outgoing velocities, separated by `Δi`.

### 6.1 Inclination difference does not wrap

`Δi = |i₁ − i₂|`, with no angular normalisation. Inclinations of 10° and 170° differ by
**160°**, not 20° — they are near-opposite orbital planes. Wrapping would make retrograde
transfers look cheap, which is the opposite of the truth.

### 6.2 Why this term dominates ORCA's cost structure

| Manoeuvre | Δv |
|---|---|
| 1° plane change @ 500 km | 0.132863 km/s |
| Hohmann 500 → 800 km | 0.160759 km/s |
| **ratio** | **0.826478** |
| **break-even** | **1.209961°** |

One degree of inclination costs **83% of a 300 km altitude raise**. Two consequences run
through the whole system:

1. In LEO, any realistic inclination spread swamps any realistic altitude spread.
2. Euclidean distance is a *physically wrong* proxy for cost — which is exactly why ORCA
   keeps `distance` as an objective separate from `fuel` (§9).

Both figures are re-derived from this implementation by
`Hohmann.InclinationDominatesAltitude` and `Hohmann.BreakEvenInclinationIsAboutOnePointTwoDegrees`,
not copied from Phase 0.

---

## 7. Combined manoeuvre — the choice made, and why

**ORCA combines the plane change with one of the two Hohmann burns.** It does not model it
as a separate third burn.

The combination happens at whichever end has the **larger radius**, because that is where
the spacecraft is slowest and rotating a velocity vector costs in proportion to its
magnitude. For a raise that is the arrival burn; for a descent, the departure burn.

### 7.1 Justification

It is what a real mission would fly, and the saving is material rather than marginal:

| 500 → 800 km, Δi | Combined | Sequential | Saving |
|---|---|---|---|
| 0° | 0.160759 | 0.160759 | 0.00% |
| 5° | 0.732324 | 0.810847 | **9.68%** |
| 15° | 2.017314 | 2.106077 | 4.22% |
| 30° | 3.918244 | 4.018110 | 2.49% |

A planner using sequential burns would not merely be pessimistic — it would systematically
overprice inclined transfers and therefore make **different routing decisions**. Since
routing decisions are the entire product, that matters.

### 7.2 What makes it trustworthy

`sequentialDeltaVKms` exists solely as a test oracle. Two properties are asserted across
altitudes and inclinations:

- **`combined ≤ sequential` always** — the triangle inequality, checked over 25 radius
  pairs × 13 inclinations.
- **Exact degeneracy at `r₁ = r₂`** — the combined formula must reduce *precisely* to
  `2·v·sin(Δi/2)`. This is the single best check that it is wired to the right speeds; a
  transcription error breaks it immediately.

---

## 8. Phasing model — where the FIFO guarantee comes from

### 8.1 The problem

A Hohmann transfer arrives 180° around from where it departed, `coast` seconds later. The
target must be *at that point when we arrive*. Usually it is not. Useful departures recur
with the **synodic period** — 25.4 hours for a 500 km and an 800 km orbit — against
survival windows of tens of minutes.

### 8.2 The model

Wait for the window, then fly the optimal transfer.

```
α        = transferArc − ω_target · coast          required lead angle at departure
δ(t)     = θ_target(t) − θ_source(t)               current relative phase
gap(t)   = normalise(α − δ(t))  or  normalise(δ(t) − α)   per drift direction
wait(t)  = gap(t) / |ω_target − ω_source|
transfer(t) = wait(t) + coast
arrival(t)  = t + transfer(t)
```

`transferArc` is `π` for a Hohmann transfer and `0` for a pure plane change, which unifies
the two cases.

### 8.3 Why *this* model and not the one Phase 0 specified

`SIMULATION_MODEL.md` §4.5 originally specified a **Δv penalty for departing at a bad
phase** — depart immediately, pay for the awkward geometry. It produces an attractive
continuous trade-off.

**It breaks FIFO.** If time cost varies with phase error, a departure one minute later can
arrive *earlier*, because the phase improved by more than the minute cost. Measured across
4000 randomly sampled orbit pairs, that model violated FIFO in **827** of them,
concentrated in LEO→MEO/GEO transfers — precisely ORCA's multi-band scenarios.

The condition turns out to be:

```
FIFO requires   k < 2 / (coast · |Δω|)
```

| Transfer | `k_max` | Verdict at `k = 0.5` |
|---|---|---|
| 500 → 800 km | 9.94 | holds |
| 500 → 2000 km | 2.13 | holds |
| **500 → 20000 km** | **0.195** | **violated** |
| **500 → 35786 km** | **0.101** | **violated** |
| **300 km → GEO** | **0.097** | **violated** |

Large altitude differences comfortably breach it, so the penalty model is unsafe **exactly
where ORCA needs it most**. `ALGORITHMS.md` §3.4 and `SIMULATION_MODEL.md` §4.5 have been
corrected accordingly.

### 8.4 What is lost, and where the trade-off went

The penalty model's appeal was a continuous *within-edge* trade-off: "go now and pay Δv"
versus "wait and pay time". The wait model does not have that — an edge has one delta-v.

**The trade-off has not disappeared; it has moved from within an edge to between routes.**
A direct transfer to a distant target may have a low Δv and a 25-hour wait, while a
multi-leg route through an intermediate orbit costs more Δv and much less waiting, because
synodic periods differ per pair. That is still a genuine time/fuel Pareto tension, and it
is arguably a *better* one for this project: it is discovered by graph search, which is
what makes graph search necessary.

What is genuinely lost is within-pair time variation from anything other than waiting. A
faster-than-Hohmann transfer needs a Lambert solver (§15).

### 8.5 Degenerate cases

| Case | Behaviour | Why |
|---|---|---|
| Same orbit, same phase | wait 0, Δv 0, coast 0 | already co-located |
| Same orbit, different phase | `PhasingUnavailable` | no relative drift ⇒ the gap never closes. Catching up requires changing orbit, which this model does not offer. Physically correct |
| Near-identical altitudes | wait diverges → rejected by `maxWaitSec` | 100 m of separation implies a synodic period of years |
| Same altitude, different inclination | pure plane change: coast 0, arc 0 | a single burn at a node |

---

## 9. Distance

`pathLengthKm` is the **arc length of the transfer trajectory flown between the two burns**.

The transfer ellipse has `a = (r₁+r₂)/2` and `b = √(r₁·r₂)`. Its perimeter has no closed
form, so ORCA uses **Ramanujan's second approximation**:

```
P ≈ π · [ 3(a+b) − √((3a+b)(a+3b)) ]
```

and takes half of it — the half-ellipse actually flown.

### 9.1 What this does and does not mean

**It is** the length of the path swept during the transfer arc.

**It is not** "total distance travelled", and it deliberately excludes the loiter. While
waiting for a window the vehicle continues orbiting, covering considerable ground that has
nothing to do with the transfer. Including it would make `distance` a proxy for `time`, and
the objective would stop carrying independent information.

### 9.2 Why it earns its place as a fourth objective

**Arc length depends only on the two radii — not on inclination.** Delta-v depends heavily
on inclination. So `distance` and `fuel` genuinely disagree, and they disagree precisely in
the case §6.2 identifies as dominant. That disagreement is the objective's entire
justification (`OPTIMIZATION_MODEL.md` §2.1), and it is asserted directly by
`ArcLength.IsIndependentOfInclination`.

### 9.3 Accuracy

Ramanujan II is accurate to better than `1e-5` relative at the eccentricities ORCA
encounters, and **exact for a circle** — which gives a closed-form oracle: for `r₁ = r₂`
the half-perimeter must equal `π·r` exactly. Also bounded structurally: the arc must lie
between `π·min(r₁,r₂)` and `π·max(r₁,r₂)`.

---

## 10. Fuel-cost abstraction

Two distinct quantities, deliberately never conflated:

| Quantity | Definition | Nature |
|---|---|---|
| `geometry.deltaVKms` | what the manoeuvre requires | **physical approximation**, km/s |
| `fuelCost` | `deltaVKms / vehicle's delta-v budget` | **planning objective**, `[0,1]` |

They live in separate fields with separate names because they answer different questions:
delta-v is what the manoeuvre costs the universe; fuel cost is what it costs *this vehicle*.

Consistent with Phase 1's definition (`DATA_MODEL.md` §3.2): `fuelCost` is a fraction of
**that vehicle's own** budget, and is therefore **not comparable across vehicles with
different budgets**. The same manoeuvre costs a smaller fraction of a larger tank — asserted
by `TransferModel.FuelCostScalesWithTheVehiclesOwnBudget`.

ORCA does not model spacecraft mass, so the rocket equation is not applied and propellant
mass in kilograms is never reported.

---

## 11. Risk abstraction

> A **simulation risk model**: a plausible, monotonic, tunable hazard score in `[0,1]`. Not
> a collision probability, not calibrated against any conjunction-assessment method.

Three factors, all computable from transfer geometry alone:

| Factor | Definition | Rationale | Default weight |
|---|---|---|---|
| Manoeuvre severity | `totalΔv / budget` | bigger burns, more to go wrong | 0.45 |
| Plane-change share | `planeChangeΔv / totalΔv` | plane changes are the least forgiving part | 0.35 |
| Arrival fuel margin | `1 − (remaining−spent)/budget` | arriving nearly dry is dangerous | 0.20 |

The second is what makes an inclined route score worse than an equally expensive coplanar
one — a distinction the delta-v number alone cannot express.

**Debris proximity is deliberately absent.** It needs the spatial index and a populated
environment (Phase 9). Rather than ship a dead zero-weighted term now, the fourth factor
will be *added* then and the weights renormalised. A weight of zero in the code would look
like a feature and behave like nothing.

Weights are modelling choices, not physical facts; they carry defaults in code and move to
`configs/risk.json` in Phase 14, following Phase 1's `HealthThresholds` pattern.

The model is fully deterministic — a pure function of geometry and budget, with no
randomness — which is what lets risk participate in a reproducible objective vector.

---

## 12. Feasibility

`checkFeasibility(geometry, fuelCost, risk, constraints)` is a **pure function** with no
orbital mechanics in it. Keeping policy out of the physics means a later planner can reuse
the approximation under different rules, and the check can be unit-tested against
hand-built geometries.

### 12.1 Constraint ordering

Checked most-fundamental first, so the reported reason is the most informative one:

1. `InvalidOrbit` — nothing below is meaningful without valid input
2. `PhasingUnavailable` — **before affordability**: no amount of fuel fixes a rendezvous
   that never occurs
3. `DeltaVExceeded` / `FuelInsufficient` — capability
4. `DeadlineExceeded` — timing against the casualty's survival window
5. `DistanceExceeded`, `RiskExceeded` — policy limits

### 12.2 Constraint sources

`TransferConstraints::forVehicle()` reads delta-v capability from an existing
`RescueVehicle` rather than restating it. **No Phase 1 field was duplicated and no Phase 1
type required modification.** Policy fields (`maxWaitSec`, `maxRisk`, `maxPathLengthKm`,
`latestArrivalSec`) are not vehicle properties and carry defaults here.

`maxWaitSec` defaults to 6 hours. That default is what makes ORCA's central tension bite:
an unbounded horizon would let the planner wait 25 hours for a fuel-optimal departure while
the casualty dies.

---

## 13. Time dependence and FIFO

### 13.1 The requirement

Edges are weighted `w(e, t)`. Dijkstra's greedy commitment — once popped, a label is final
— is sound on such a graph only if the model is **FIFO**:

```
for t₁ ≤ t₂ :   t₁ + w(e, t₁)  ≤  t₂ + w(e, t₂)
```

Departing later must never arrive earlier.

### 13.2 The proof

Let `A(t) = min { s ≥ t : the geometry is correct at s }`, and let `coast` be constant.

```
arrival(t) = A(t) + coast
```

`A` is non-decreasing: for `t₁ ≤ t₂`, the set `{s ≥ t₂}` is a subset of `{s ≥ t₁}`, so its
minimum cannot be smaller. Adding a constant preserves that. **Therefore arrival is
non-decreasing. FIFO holds by construction, unconditionally.** ∎

Concretely, `wait(t)` decreases at exactly rate 1 as `t` advances, so arrival is a
**staircase**: flat between windows, jumping by one synodic period at each.

### 13.3 The verification

| | |
|---|---|
| Ordered comparisons performed | **143 991** |
| Violations | **0** |
| Worst backwards excursion | **8.1e-15 relative** (a few ulp) |
| Tolerance used | `1e-12` relative — ~120× the rounding floor, ~1e12 below any real violation |

Tested on **geometry**, not on feasible transfers only: FIFO is a property of the model, and
restricting to affordable transfers would leave most of the domain unchecked.

`Fifo.ArrivalIsAStaircaseNotAMonotoneLine` exists to confirm the tests have teeth — if
arrival simply tracked departure, FIFO would hold trivially and the other tests would be
vacuous.

### 13.4 The stronger property, and why it matters more than FIFO

Because *all* phase dependence is absorbed into the wait:

> **Delta-v, fuel, risk and path length do not depend on departure time. Only time does.**

This is stronger than FIFO and easy to overlook. Multi-objective label-setting prunes a
label when another dominates it, and that pruning is sound only if extending the dominating
label is never worse (`ALGORITHMS.md` §5.6). On a graph where *fuel* also varied with
departure time, an earlier-but-cheaper label could extend to a **more expensive** edge, and
the Pareto search would silently discard optimal routes while appearing to work perfectly.

Asserted by `Fifo.NonTimeObjectivesAreIndependentOfDepartureTime` and
`Fifo.FuelAndRiskAreIndependentOfDepartureTime` across 700 randomised orbit pairs at six
departure times each.

---

## 14. Numerical tolerances

| Comparison | Tolerance | Why |
|---|---|---|
| FIFO arrival ordering | `1e-12` **relative** | arrival times span 1e3–1e9 s; a fixed absolute epsilon would be useless at one end and spurious at the other |
| Δv against reference values | `1e-8` absolute | values are `O(1)` km/s; 1e-8 is ~8 significant figures |
| Coast time against reference | `1e-4` s | values are `O(10³–10⁴)` s |
| Arc length against reference | `1e-3` km | values are `O(10⁴)` km |
| Algebraic identities | `1e-12` | pure arithmetic; no model error to absorb |
| Degeneracy checks | `1e-9`–`1e-10` | one transcendental evaluation of slack |
| Feasibility boundaries | `kEpsilon = 1e-9` | a transfer needing exactly the remaining budget must be accepted; two independently computed burn magnitudes will not land exactly on it |

**Exact equality is used only where the operation is exactly representable** — for example
asserting that risk is deterministic across repeated calls with identical inputs.

---

## 15. Limitations

| # | Limitation | Consequence |
|---|---|---|
| 1 | Circular orbits only | No elliptical parking or transfer orbits |
| 2 | Two-body only | No J2, drag, SRP, third-body. Orbits never decay or precess |
| 3 | Impulsive burns | No finite-burn losses, no low-thrust |
| 4 | Hohmann family only | No faster-than-Hohmann transfers, so **within-pair time varies only by waiting** |
| 5 | **RAAN not enforced in phasing** | Rendezvous requires meeting at the line of nodes; ORCA treats phase alignment alone as sufficient. **Under-constrains non-coplanar rendezvous.** Exact for coplanar, equal-RAAN pairs |
| 6 | No mass modelling | Fuel is a Δv fraction; no propellant mass |
| 7 | Risk is a designed heuristic | Self-consistent within ORCA; meaningless outside it |
| 8 | No debris term yet | Arrives in Phase 9 with renormalised weights |
| 9 | Arc length is Ramanujan-approximate | ~1e-5 relative; exact only for circles |
| 10 | No phasing manoeuvres | A vehicle cannot spend Δv to shorten a wait (§8.4) |
| 11 | Instantaneous rendezvous | No proximity operations or docking |

**Limitation 5 is the most significant** and is the one to fix first if fidelity becomes a
goal. It makes inclined transfers appear more available than they are.

### 15.1 What these limitations do not undermine

They constrain the *physical* claims. They do not weaken the *algorithmic* ones: FIFO holds
exactly, the non-time objectives are provably time-independent, and both properties hold
regardless of how accurate the underlying delta-v numbers are. **ORCA solves a real
optimization problem over an approximate physical model**, and Phase 2's job was to make
that approximation defensible and its correctness properties explicit.

---

## 16. Future replacement with higher-fidelity models

The `TransferModel` interface is the seam. Phase 3 and above consume `TransferResult` and
`ObjectiveVector` and know nothing of Hohmann transfers, synodic periods or combined burns,
so the internals can be replaced without touching the graph, the search or the optimizer.

| Extension | Fixes | Consequence to check first |
|---|---|---|
| **Lambert solver** | limitations 1, 4 | Arrival time becomes a free variable per edge, producing a genuine within-pair time/fuel trade-off. **May break FIFO** — Lambert admits transfers where departing later arrives earlier. If adopted, §13's proof must be redone and the search may need a time-dependent variant |
| **Node-crossing constraint** | limitation 5 | Adds a second alignment condition; wait becomes the later of two windows. FIFO survives — the maximum of two non-decreasing functions is non-decreasing |
| **Phasing manoeuvres** | limitation 10 | Restores the within-edge time/fuel trade-off, but makes **fuel time-dependent**, which breaks §13.4. Would require re-examining dominance-pruning soundness, or modelling phasing options as parallel edges instead |
| **SGP4 + real elements** | limitations 1, 2 | Orbits gain eccentricity and precession; `Orbit` extends additively |
| **Finite burns** | limitation 3 | Adds burn duration as a fourth time component |

The third row is worth emphasising: the obvious "make it richer" extension is precisely the
one that would reintroduce the problem Phase 2 solved. Anyone attempting it should read
§13.4 first.
