# ORCA — Research Extensions

Version 1.0 · Phase 0 · 2026-09-02

> **Nothing in this document is built, and nothing here should be built during Phase 1.**
> These are directions the architecture deliberately leaves open. Listing them serves two
> purposes: it shows the limitations in `SIMULATION_MODEL.md` §8 are *understood* rather than
> merely present, and it records which seams exist so a future extension does not require
> rebuilding the system.

Each entry states: what it is, what it fixes, what it costs, and **which seam it plugs into**.

---

## 1. Ranking

| # | Extension | Value | Effort | Seam exists? |
|---|---|---|---|---|
| 1 | Planning under uncertainty | ★★★★★ | High | Partial |
| 2 | Lambert transfers | ★★★★☆ | Medium | ✅ `TransferModel` |
| 3 | Real orbital data (TLE/SGP4) | ★★★★☆ | Medium | ✅ `ScenarioLoader` |
| 4 | Evolutionary MOO (NSGA-II) | ★★★☆☆ | Medium | ✅ `PathSearch` |
| 5 | Constraint-based selection | ★★★☆☆ | Low | ✅ `Profile` |
| 6 | Monte Carlo outcome analysis | ★★★☆☆ | Low | ✅ `ExperimentRunner` |
| 7 | Orbital perturbations (J2, drag) | ★★☆☆☆ | Medium | ✅ `OrbitalPropagator` |
| 8 | Multi-agent / decentralised planning | ★★☆☆☆ | High | ✗ |
| 9 | Reinforcement learning | ★★☆☆☆ | High | ✗ |
| 10 | Real debris catalogues | ★★☆☆☆ | Low | ✅ `ScenarioLoader` |

The ranking is by *research value per unit of risk to the existing system*, not by how
impressive each sounds.

---

## 2. Planning Under Uncertainty — *the most valuable extension*

### What it is

ORCA currently treats every cost as a known scalar and replans **reactively** after a change
is detected (`ALGORITHMS.md` §11.5). A stronger planner would treat costs as **distributions**
and choose plans that remain good across possible futures.

### What it fixes

The deepest limitation in the current design. Today ORCA is surprised by every debris alert.
It has no notion that a route *might* become dangerous, so it cannot prefer a route that is
slightly worse now but robust later. Real mission planning is fundamentally about that
preference.

### Approach

- Model risk and transfer cost as distributions rather than point estimates.
- Replace scalar dominance with **stochastic dominance**, or optimise a risk measure such as
  Conditional Value-at-Risk (CVaR) over the objective distribution.
- Add a *robustness* objective: expected cost under perturbation.
- Compare **reactive** replanning (current) against **anticipatory** planning on the same
  seeded scenarios.

### Research question

> Does anticipatory planning under uncertainty outperform reactive replanning when hazard
> detection is delayed, and at what computational cost?

This is publishable-shaped, and ORCA's determinism plus paired-seed benchmarking
(`BENCHMARK_PLAN.md` §7.4) is exactly the harness needed to answer it.

### Cost and seam

High. `ObjectiveVector` becomes a distribution type, and `Dominance` needs a stochastic
variant. The dimension-generic dominance interface helps; the scalar assumption in
`ObjectiveVector` does not. **Partial seam** — this is the one extension that would require
touching the core types, which is precisely why it is worth flagging now rather than
discovering later.

---

## 3. Lambert Transfers

### What it is

Replace ORCA's patched-Hohmann-plus-phasing-penalty model (`SIMULATION_MODEL.md` §4.5) with a
**Lambert solver**: given two position vectors and a time of flight, compute the exact
two-body transfer orbit connecting them.

### What it fixes

The phasing penalty is a *designed heuristic* with a tunable parameter (`kPhasePenalty`), and
it is the least physically grounded part of the transfer model. Lambert replaces it with the
actual answer.

### Approach

Universal-variable Lambert (Vallado's formulation) or Izzo's algorithm. Given departure time
`t₁` and arrival time `t₂`, solve for the transfer orbit, and Δv is the difference between
transfer-orbit velocity and the bodies' velocities at each end.

**Important consequence:** with Lambert, arrival time becomes a *free variable* per edge rather
than a derived one. That turns each edge into a continuous optimisation (a porkchop plot) and
substantially enriches the time/fuel trade-off — the front would come from real transfer
physics rather than from a penalty function.

**Also important:** it may break the FIFO property (`ALGORITHMS.md` §3.4). Lambert admits
transfers where departing later arrives earlier. If so, Dijkstra's optimality no longer holds
and a time-dependent-shortest-path variant is required. This is exactly the kind of
consequence worth knowing before adopting the extension.

### Cost and seam

Medium — a well-documented algorithm, ~200 lines. **Seam exists:** `TransferModel` is already
an interface behind `GraphBuilder` (`ARCHITECTURE.md` §15). Swapping implementations requires
no changes to the graph, the search, or the optimizer.

---

## 4. Real Orbital Data — TLE and SGP4

### What it is

Load Two-Line Element sets from a public catalogue and propagate with **SGP4** instead of
circular two-body motion.

### What it fixes

Simplifications 1, 2, 8 and 14 in `SIMULATION_MODEL.md` §8 at once: real eccentricity, real
inclination distributions, real orbital decay, real perturbations, and a realistic spatial
distribution of objects.

### Approach

Parse TLE; use a reference SGP4 implementation (the algorithm is standard and published);
`OrbitalPropagator` becomes an interface with `CircularPropagator` and `Sgp4Propagator`
implementations.

**Caveats that must be stated if this is done:**
- SGP4 is accurate to roughly kilometres over days, not metres. It does not make ORCA a flight
  tool.
- TLE data is a network dependency, violating `ARCHITECTURE.md` §14's offline commitment
  unless catalogues are snapshotted to disk. **Snapshot them.**
- Real catalogues are large (tens of thousands of objects), so the spatial index
  (`ALGORITHMS.md` §9) moves from optional to mandatory.

### Cost and seam

Medium. **Seam exists:** `ScenarioLoader` and `OrbitalPropagator` are both interfaces. Domain
types are unchanged — `Orbit` would gain eccentricity and argument of perigee, which is
additive.

---

## 5. Evolutionary Multi-Objective Optimisation

### What it is

Add **NSGA-II** and/or **MOEA/D** as alternative front generators alongside exact
label-setting.

### What it fixes

Nothing about correctness — label-setting is exact. It addresses **scale**: MOSP is NP-hard
(`ALGORITHMS.md` §5.7), so at very large graph sizes the exact method will eventually become
impractical, and a population-based approximation is the standard answer.

### Why this is an unusually good comparison

Most NSGA-II papers cannot compare against the true Pareto front, because computing it is
intractable. **ORCA can** — at the sizes where label-setting still terminates, the exact front
is available as ground truth. That permits measuring approximation quality directly:

- **Hypervolume ratio** vs the exact front.
- **Generational distance** and **inverted generational distance**.
- The size at which NSGA-II becomes faster than exact search, and what it costs in quality
  there.

### Research question

> At what problem size does population-based approximation become preferable to exact
> label-setting, and how much front quality is lost at the crossover?

### Cost and seam

Medium. **Seam exists:** `PathSearch` is a strategy interface; NSGA-II registers alongside
Dijkstra, A* and label-setting, and the benchmark matrix (`BENCHMARK_PLAN.md` §4.2) already
supports adding a strategy as a configuration entry.

---

## 6. Constraint-Based and Lexicographic Selection

### What it is

Richer Stage-2 selection than a weighted sum. Examples:

```
minimise time  subject to  risk < 0.10  and  fuel < 0.20
lexicographic: minimise risk; among ties minimise time; among ties minimise fuel
reference point: find the front member closest to a target vector
```

### What it fixes

`OPTIMIZATION_MODEL.md` §13's last limitation: the *slider* parameterisation cannot express
every preference structure. An operator who thinks "as fast as possible, but never above 10%
risk" cannot say that with weights — and that is a natural, common way to think.

### Cost and seam

**Low, and this is the cheapest real win in the document.** The front is already computed;
these are alternative selection rules over `|front|` points, each a few dozen lines.
**Seam exists:** `Profile` is already a data-driven selection strategy.

---

## 7. Monte Carlo Outcome Analysis

### What it is

Run many seeds per configuration and analyse outcome *distributions* rather than means.

### What it fixes

`BENCHMARK_PLAN.md` reports medians and CIs, which is sound but summary-level. Distributional
analysis asks better questions: what is the *worst-case* outcome of each strategy? Does Pareto
planning reduce **variance** in success rate even when it does not improve the mean?

That last question is genuinely interesting and is invisible to mean-comparison. A strategy
that is equally good on average but far more consistent is meaningfully better for a rescue
system.

### Cost and seam

Low. **Seam exists:** `ExperimentRunner` already runs the seed matrix; this is analysis over
existing exports. Mostly a question of asking the right question of data ORCA already produces.

---

## 8. Orbital Perturbations

Add **J2** (Earth oblateness — causes nodal precession of several °/day in LEO) and
atmospheric drag.

J2 is the interesting one for ORCA specifically: it makes RAAN drift at different rates for
different altitudes and inclinations, so **the plane-change cost between two satellites changes
over the course of a scenario**. Given that inclination dominates the cost structure
(`SIMULATION_MODEL.md` §4.3), this would add a genuine long-timescale dynamic — patient
planning could exploit precession to reduce plane-change cost.

Cost: medium. **Seam exists:** `OrbitalPropagator`. Analytic J2 secular rates are closed-form,
so the `O(1)`-per-tick property survives.

---

## 9. Multi-Agent and Decentralised Planning

Currently a single central planner decides everything. A decentralised variant would have
vehicles negotiate assignments themselves — market-based/auction protocols (Contract Net),
consensus-based bundle algorithms.

Research question: how much optimality is lost to decentralisation, and how much robustness to
central-planner failure is gained?

**No seam exists.** This would restructure `planning/` substantially. High effort, and the
honest assessment is that it is a different project sharing ORCA's simulation layer rather than
an extension of ORCA's planner.

---

## 10. Reinforcement Learning

An RL agent learning a dispatch policy from simulated experience, compared against ORCA's
optimisation-based planner.

Stated plainly: **this is the lowest-value item that sounds highest-value.** ORCA's planner is
already near-optimal for the modelled problem, so an RL agent's realistic best case is
*matching* it with less interpretability. RL becomes genuinely interesting only *after*
extension 2 (uncertainty), where the optimal policy is not computable and learning has
something to contribute.

**No seam exists**, and it would introduce a Python/ML dependency that `ARCHITECTURE.md` §3.1
currently and correctly excludes. Listed for completeness and explicitly not recommended as a
next step.

---

## 11. Real Debris Catalogues

Replace synthetic debris with a snapshot of a public conjunction/debris dataset.

Cost: low. **Seam exists:** `ScenarioLoader`. Main consequences are scale (the spatial index
becomes mandatory) and honesty — real data would make the risk model's *inputs* real while its
*formulation* stays a designed heuristic (`SIMULATION_MODEL.md` §5). That distinction would
need stating clearly, since real inputs make a model look more validated than it is.

---

## 12. What ORCA Should *Not* Become

Recorded so scope creep has something to argue against.

| Not this | Why |
|---|---|
| A flight-dynamics tool | Would require validation ORCA cannot provide, and invite operational misuse |
| A real-time operational system | No safety case, no verification, no certification |
| A general MOO framework | ORCA is a *demonstration* of MOO on a concrete problem; generality would dilute it |
| A cloud/multi-user platform | `ARCHITECTURE.md` §16 — no requirement supports it |
| An LLM-explained planner | Explanations are template-driven from real data, and should stay auditable |

---

## 13. The Seam Audit

The recurring theme is that **most extensions are cheap because interfaces already exist**:

| Interface | Enables |
|---|---|
| `TransferModel` | Lambert (§3), alternative cost models |
| `OrbitalPropagator` | SGP4 (§4), J2 and drag (§8) |
| `ScenarioLoader` | TLE (§4), real debris (§11) |
| `PathSearch` | NSGA-II (§5), any future search |
| `AssignmentSolver` | alternative assignment algorithms |
| `Profile` | constraint-based selection (§6) |
| `ExperimentRunner` | Monte Carlo analysis (§7) |

Only §2 (uncertainty), §9 (multi-agent) and §10 (RL) lack a seam — and each of those is a
genuine architectural change rather than a plug-in, which is an honest reason they rank as high
effort rather than a defect in the design.

**The one to watch is §2.** It is the highest-value extension and the only high-value one
without a clean seam, because `ObjectiveVector` assumes scalar costs. If distributional costs
ever become a goal, that assumption is the thing to revisit first.
