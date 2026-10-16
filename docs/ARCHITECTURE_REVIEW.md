# ORCA — Architecture Review

Version 1.0 · Phase 0 · 2026-09-02

An adversarial review of the architecture defined in the preceding fifteen documents,
conducted **before** implementation, while changes are still cheap.

Each question gets a verdict: ✅ pass · ⚠️ pass with a recorded concern · ❌ fail requiring
change. Findings that changed the architecture are marked **[CHANGED]** and the change is
described.

---

## 1. Verdict Summary

| # | Question | Verdict |
|---|---|---|
| 1 | Is C++ genuinely the computational core? | ✅ |
| 2 | Is the frontend doing backend logic? | ⚠️ one bounded exception |
| 3 | Is the Node layer too complex? | ✅ |
| 4 | Are there unnecessary technologies? | ✅ |
| 5 | Are there unnecessary algorithms? | ⚠️ two conditional |
| 6 | Is the communication mechanism maintainable? | ✅ |
| 7 | Is the simulation model explainable? | ✅ |
| 8 | Can the student defend the design? | ✅ |
| 9 | Can the system run offline? | ✅ |
| 10 | Can benchmark results be reproduced? | ⚠️ counters yes, timings no |
| 11 | Can individual algorithms be unit-tested? | ✅ |
| 12 | Can the frontend be replaced without rewriting the engine? | ✅ **demonstrated** |

No ❌. Four ⚠️, each with a recorded mitigation. Six findings changed the architecture.

---

## 2. Is C++ genuinely the computational core? ✅

**Test applied:** list every computed value in the product and name its owner.

| Value | Owner |
|---|---|
| Orbital positions, simulation time | C++ |
| Graph construction, edge costs | C++ |
| All search (Dijkstra, A*, label-setting) | C++ |
| Dominance, front construction, normalisation, scoring | C++ |
| Emergency priority, feasibility, assignment | C++ |
| Mission state, fuel, risk, ETA | C++ |
| Trajectory polylines | C++ |
| Graph layout coordinates | C++ |
| Algorithm traces and metrics | C++ |
| Explanations | C++ |
| **Marker interpolation between snapshots** | **frontend** — see §3 |
| Camera, selection, sliders, theme | frontend |
| Process lifecycle, socket sessions | Node |

Nothing computational is outside C++ except the one interpolation exception. The
`cpp-no-node` CI job (`TEST_PLAN.md` §9) enforces this mechanically rather than by convention.

**Finding [CHANGED].** Graph layout was initially unassigned, and a force-directed layout in
the frontend would have been the natural default. That would have put a real algorithm in
React. Layout coordinates are now engine-computed and shipped in `graphData`
(`API_CONTRACT.md` §4.6).

---

## 3. Is the frontend doing backend logic? ⚠️

One exception exists and is deliberate: **positional interpolation between snapshots**, so a
10 Hz stream renders smoothly at 60 fps.

**Adversarial case against it.** Interpolation is a computation on engine data. Today it moves
a marker; the same code could plausibly grow into "predict where the vehicle will be", which is
propagation — engine work.

**Why it survives review.** The alternatives are worse: raising the snapshot rate to 60 Hz
multiplies bandwidth sixfold to solve a rendering problem, and rendering at 10 fps is
unacceptable. Interpolation is standard practice for exactly this reason in networked
rendering.

**Bounded by three rules**, all in `ARCHITECTURE.md` §6.1:
1. It never feeds back into engine state.
2. It is discarded on every snapshot.
3. **No displayed number is ever derived from it** — interpolated positions move pixels only.

Rule 3 is what keeps it from becoming a second source of truth. `DisplayedValuesMatchFixtureExactly`
(`TEST_PLAN.md` §7) enforces it: any displayed value must equal fixture data exactly.

**Verdict:** acceptable, bounded, tested. Recorded as the single known exception so it cannot
quietly expand.

**Finding [CHANGED].** `remainingWindowSec` was originally to be computed client-side as
`deadline − simTime` — obviously correct, and exactly the kind of "harmless" derivation that
erodes the rule. It is now engine-computed and shipped.

---

## 4. Is the Node layer too complex? ✅

Node's complete responsibilities:

1. Spawn and supervise the engine child process.
2. Read NDJSON from stdout, write NDJSON to stdin.
3. Coalesce snapshots (latest-wins), buffer events (lossless), correlate `reqId`.
4. Serve REST and WebSocket to the browser.
5. Serve the built frontend as static files.

That is a **relay with a supervisor** — a few hundred lines. Node contains no domain types, no
algorithms, no mission logic.

**Adversarial probe: could Node be removed entirely?** In principle the browser could talk to
the engine over WebSocket directly — but the engine would then need a C++ WebSocket library
(Boost.Beast or uWebSockets), a heavy dependency dragging HTTP framing, upgrade handshakes and
socket lifecycle into a codebase whose entire value is that it is pure computation. Node buys
that separation for a few hundred lines of glue in a runtime **already installed on the
machine** (audit §3).

**Verdict:** justified and minimal.

**Finding [CHANGED].** The restart policy was initially "restart on crash". That is a crash
loop: a deterministic bug crashes the engine, Node restarts it, it crashes at the same tick,
forever — burning CPU and hiding the bug. Now capped at 3 attempts with backoff, and the
`stderr` tail is surfaced to the UI (`ARCHITECTURE.md` §11).

---

## 5. Are there unnecessary technologies? ✅

Each technology tested against: *what breaks if we remove it?*

| Technology | Remove it and… | Verdict |
|---|---|---|
| C++20 | no project | keep |
| CMake | no reproducible build, no FetchContent | keep |
| GoogleTest | hand-rolled assertions | keep |
| React | hand-rolled state for 9 screens | keep |
| **TypeScript** | schema drift becomes a runtime blank panel | keep — see below |
| Vite | slower builds, more config | keep |
| Three.js | raw WebGL, weeks of boilerplate | keep |
| react-three-fiber | imperative/declarative lifecycle bugs | keep |
| Node + Express | C++ WebSocket dependency (§4) | keep |
| `ws` | — | keep |
| Recharts | *provisional* — Pareto scatter is the only demanding chart | **decide at Phase 12** |
| Electron | nothing — deferred | not adopted |
| **Python** | nothing | **excluded** |
| **Java** | nothing | **excluded** |

**TypeScript deserves the scrutiny it gets**, since it is the one place added complexity could
be questioned. The justification is specific: ORCA has a **cross-language data contract**.
The engine emits JSON; React consumes it. Without types, a renamed field produces a blank
panel at runtime with no error. With types, it is a compile failure. The discriminated union on
`EngineMessage` also makes an unhandled message type a build error. TypeScript pays for itself
precisely *because* of the boundary — it would be far weaker justification in a single-language
app.

**Python and Java.** The brief permits both and asks they not be added for their own sake.
Applying that honestly: neither earns a place. Statistical analysis is Python's strongest case,
and even that is deferred — ORCA exports CSV, and what a researcher uses on those CSVs later is
not part of ORCA. Adding a Python module to compute a mean the engine already aggregates would
create a second source of truth.

**One place this is worth revisiting**, recorded rather than hidden: benchmark *figure
generation* (`BENCHMARK_PLAN.md` §10). matplotlib is genuinely good at this. The current
decision is to generate figures from the frontend's charting stack; if that proves clumsy, an
offline plotting script is a defensible exception, because it consumes exported CSV and is not
part of the product.

---

## 6. Are there unnecessary algorithms? ⚠️

| Algorithm | Justification | Verdict |
|---|---|---|
| Adjacency-list graph | core representation | ✅ |
| Binary heap | 4 distinct uses | ✅ |
| Dijkstra | single-objective baseline + benchmark control | ✅ |
| A* | fewer expansions; **must be measured** | ✅ conditional |
| Label-setting | the product | ✅ |
| Dominance | the pruning rule | ✅ |
| Greedy assignment | the strawman being measured | ✅ |
| Hungarian | optimal comparator; counterexample proves it can matter | ✅ |
| **Spatial index** | needed at 500–1000 objects, not below | ⚠️ **conditional** |
| **Union-Find** | weakest justification in the project | ⚠️ **conditional** |

**Union-Find is the honest problem case.** The question it answers — "is the casualty
reachable?" — is answered by the search that must run anyway. It pays off only when many
casualties are queried against one graph. `ALGORITHMS.md` §10.3 already states this and makes
implementation conditional on a *measured* benefit.

**Verdict:** correct as written. A DSA project is tempted to include structures for their own
sake; making two of them conditional on measurement is the right response, and the decision
rule is recorded so it cannot be quietly ignored.

**A\* is also conditional, in a softer sense.** If `ExpandsNoMoreThanDijkstra`
(`TEST_PLAN.md` §4.3) showed no advantage, A* would be dead weight and the project should say
so. The test exists precisely to force that judgement.

---

## 7. Is the communication mechanism maintainable? ✅

| Property | Assessment |
|---|---|
| Debuggable by hand | ✅ `echo '{"cmd":...}' \| ./orca_engine --serve` |
| Testable | ✅ golden-file: recorded stdin → diffed stdout |
| Crash isolation | ✅ child process; server survives |
| Portable | ✅ pipes identical on macOS and Linux |
| Zero dependencies | ✅ engine needs only a JSON writer |
| Evolvable | ✅ additive fields; `schemaVersion` for breaks |
| Bandwidth | ⚠️ see below |

**Concern: snapshot size at 1000 objects.** A full snapshot with 1000 entities at 10 Hz is
substantial JSON. Mitigations already in the design: `speedMultiplier` decoupled from snapshot
rate; latest-wins coalescing bounding memory; and the benchmark path being headless, so the
1000-object case never streams at all.

**If it becomes a real problem**, the escalation path is: (1) delta snapshots — send only
changed entities; (2) binary framing. Both are contained changes behind `io/`. Not doing either
now, because optimising an unmeasured cost is exactly what `ARCHITECTURE.md` P7 forbids.

**Finding [CHANGED].** Originally all engine output shared one channel. A stray `std::cout`
in debugging code would then inject a non-JSON line into the protocol stream and desynchronise
the codec — a nasty, intermittent bug class. Now: **`stdout` is protocol, `stderr` is logs**,
which makes that failure structurally impossible rather than merely unlikely.

**Finding [CHANGED].** Snapshots and events originally shared one discipline. They need
different ones: snapshots are full state and safe to drop when stale; events are a causal
narrative and a gap corrupts the timeline. Now three explicit disciplines
(`API_CONTRACT.md` §7).

---

## 8. Is the simulation model explainable? ✅

**Test:** can every model choice be justified in two sentences without hand-waving?

| Choice | Justification |
|---|---|
| Circular orbits | Closed-form, exact for the model, `O(1)` per tick, deterministic |
| Analytic propagation | No integration drift; `θ(t)` computed from `t` directly |
| Hohmann + phasing penalty | Validated against textbook values; penalty is a documented parameter |
| Risk as designed heuristic | Explicitly labelled a simulation model; factors and weights published |
| Fuel as Δv fraction | No mass modelling, so no rocket equation, so no kilograms claimed |
| Independent risk composition | Bounded, monotonic, order-independent — required by dominance pruning |
| Seeded RNG with stream splitting | Determinism, plus insulation between subsystems |

**Strongest point.** The core trade-off is *derived*, not assumed. The synodic period
(25.4 hours for 500/800 km) was computed and checked; survival windows are tens of minutes;
therefore the planner cannot wait for ideal phasing. The Pareto tension follows from that.

**Second strongest.** The plane-change dominance (1° ≈ 83% of a 300 km raise) was measured,
and it justifies keeping `distance` as a separate objective rather than as a proxy for fuel.

**Finding [CHANGED].** The inherited Phase 0 document asserted a 1° plane change costs *more*
than the 500→800 km raise. It does not: 0.1329 < 0.1608. Re-measured, corrected, and pinned
by the test `InclinationDominatesAltitude`. Had this gone uncorrected it would have propagated
into two model documents and every viva answer.

**Concern.** The model is approximate, and every claim depends on saying so. Mitigated by a
scope statement at the top of `SIMULATION_MODEL.md`, a 14-item limitation table, and Q17 of the
viva guide devoted to answering it.

---

## 9. Can the student defend the design? ✅

Every significant decision has a documented rationale **and a stated alternative that was
rejected**:

| Decision | Rejected alternative |
|---|---|
| Adjacency list | Matrix — better only at an operation we never perform |
| Binary heap | Fibonacci heap — wins on dense graphs; ours is sparse |
| Heap rebuild for emergency queue | Indexed decrease-key heap — over-engineering for `n < 10` |
| Two-stage optimization | Scalarise-then-search — cannot reach concave front regions |
| Label-setting | Weighted-sum sweep — same limitation |
| NDJSON over pipes | N-API binding — worse crash isolation, harder debugging |
| TypeScript | Plain JS — the cross-language boundary is where types pay |
| Uniform grid | k-d tree — debris moves every tick, so rebuild cost dominates |
| Hard constraints | Penalty terms — a penalty can rank infeasible above feasible |
| Engine-computed layout | Force-directed in React — that is an algorithm in the UI |

**The hardest question is Q8 in the viva guide** — "time-dependent weights break Dijkstra". The
answer is prepared, correct, and backed by a test that would fail if the property broke. That
is the right shape for a defence: not "we thought about it" but "here is the property, here is
where it is enforced, and here is what fails if it stops holding".

---

## 10. Can the system run offline? ✅

No runtime network calls. Textures, fonts, scenarios and configs are local. No cloud AI, no
external APIs, no telemetry, no CDN.

Network is required only at *setup*: `npm install`, and CMake `FetchContent` for GoogleTest.
Both are one-time and neither affects the running product.

**Concern.** Earth textures could be large. Constrained by the visual specification itself — a
dark, low-contrast, restrained Earth needs modest resolution, and the design forbids the
photorealistic treatment that would demand 8K assets.

---

## 11. Can benchmark results be reproduced? ⚠️

**Split verdict, and the split is the important part.**

✅ **Algorithmic counters reproduce exactly.** `nodesExpanded`, `edgesRelaxed`,
`labelsGenerated`, `labelsPruned`, `frontSize` are deterministic functions of seed and config.
Anyone with the commit and the seed list gets identical numbers on any machine.

⚠️ **Wall-clock timings do not reproduce across hardware.** They cannot — that is physics, not
a design flaw.

**Mitigation, and it is a genuine strength rather than an excuse:** `BENCHMARK_PLAN.md` §3.2
makes counters the **primary** evidence and timings secondary. A reviewer who cannot reproduce
our milliseconds *can* reproduce our node counts exactly. That is the stronger claim, and
leading with it is more honest than leading with timings.

Every export records full environment, build flags, git commit, seed list and protocol.

**Finding [CHANGED].** The benchmark environment block originally contained plausible-looking
placeholder values. Those are now explicit zeros and `"not-yet-run"`, so an un-run benchmark
can never be mistaken for a completed one. The same principle removed every invented result
from `BENCHMARK_PLAN.md` — it defines protocol only.

---

## 12. Can individual algorithms be unit-tested? ✅

**Structural guarantee:** `algorithms/` depends only on `core/` and `graph/`. It knows nothing
of missions, emergencies, simulation or IO. So every algorithm can be tested on a hand-built
toy graph with no scenario machinery.

**Verification method matters more than coverage**, and each algorithm has an
*independent* oracle:

| Algorithm | Oracle |
|---|---|
| Dijkstra | brute-force path enumeration, `V ≤ 8` |
| A* | Dijkstra's cost (must match exactly) |
| Label-setting | brute-force enumerate + dominance filter, `V ≤ 7` |
| Dominance | mathematical properties, 10 000 random triples |
| Hungarian | all `n!` permutations, `n ≤ 8` |
| Greedy | the constructed counterexample |
| Spatial index | brute-force radius scan |
| Propagation | closed-form textbook values |
| Incremental patch | full rebuild (must be identical) |

**No expected value is generated by the implementation it tests.** That rule is what
distinguishes these tests from change-detectors.

---

## 13. Can the frontend be replaced without rewriting the engine? ✅ — *demonstrated*

**This is the only question in the review with empirical rather than argued evidence.**

The UI technology **already changed once during Phase 0**. An earlier pass selected a Qt 6
desktop application and created `cpp/ui/{screens,widgets}` and `cpp/visualization/`. The current
brief specifies React + Three.js.

**Changes required to the computational core: zero.** `cpp/{core,domain,graph,algorithms,`
`optimization,simulation,planning,analytics}` were unaffected. Only the two UI-owning
directories were removed, and they were empty.

That is not a thought experiment — it is the property the architecture was designed to have,
demonstrated by an actual technology change. It is the strongest single piece of evidence in
this review, and it is why `ARCHITECTURE.md` P5 says to assume the UI can change again.

Any future frontend consuming the NDJSON/WebSocket contract works without engine changes. The
engine cannot tell the difference.

---

## 14. Additional Findings

### 14.1 Explanation generation — origin check ✅

Explanations are template-filled from engine data. **No LLM, no external API.** This matters
for three reasons: offline operation, determinism (an LLM would break reproducibility), and
auditability (every number traces to a specific front comparison).

### 14.2 Infeasibility is a result, not an error ✅ **[CHANGED]**

Originally "no feasible path" returned an error. Wrong: it is a **correct answer to a
well-formed question**, and often the most important answer the system gives. It now returns
`planResult` with `feasible:false`, a named `bindingConstraint`, and per-reason rejection
counts — HTTP 200, not 4xx.

This changed the UI too: the "no feasible plan" state shows *which* constraint bound and by how
much (*"exceeds RV-02's Δv budget by 0.75 km/s"*), which is actionable rather than merely
negative.

### 14.3 The `fuel %` ambiguity ⚠️

`fuelFraction` is relative to **each vehicle's own** Δv budget, so 13.6% for RV-02 and 13.6%
for RV-03 are different quantities of Δv. This is the easiest number in the product to
misread.

Mitigations: stated in `DATA_MODEL.md` §3.2 and `SIMULATION_MODEL.md` §6; the UI always shows
the figure attached to a named vehicle; a tooltip states it explicitly. **Not fully solved** —
a reader who ignores all three could still misread it. Recorded as a live concern.

### 14.4 Front-relative normalisation across problems ⚠️

Normalising against each front's own ideal/nadir means scores are not comparable across
planning problems — which matters in the assignment cost matrix, where scores from different
emergencies sit side by side.

Mitigation: comparisons are made **within** a row (same emergency, same front), and the
priority factor rather than raw score weights across rows. `OPTIMIZATION_MODEL.md` §8.1
documents this rather than glossing it. It remains a genuine modelling compromise and appears
in the viva guide's "weakest part" answer.

### 14.5 Scope risk ⚠️

Nine screens plus a full engine plus benchmarks is a large scope. Mitigated by explicit tiering
(`PRD.md` §12): Tier 3 is cut before Tier 1 or 2 is compromised, and only Mission Control must
be excellent.

**The honest risk:** the visualisation is the most *visible* part and the least *essential*
part. Time spent perfecting Earth shading is time not spent on label-setting correctness. The
roadmap deliberately sequences the entire engine before any frontend work for this reason.

---

## 15. Changes Made as a Result of This Review

| # | Finding | Change |
|---|---|---|
| 1 | Graph layout would have been a frontend algorithm | Engine computes and ships layout coordinates |
| 2 | `remainingWindowSec` would have been client-derived | Engine computes and ships it |
| 3 | Unlimited restart would create a crash loop | Capped at 3 with backoff; `stderr` tail surfaced |
| 4 | Stray `std::cout` could corrupt the protocol | `stdout` = protocol, `stderr` = logs, permanently separated |
| 5 | Snapshots and events shared a discipline | Three explicit disciplines: coalesce / lossless / correlated |
| 6 | Infeasibility modelled as an error | Now a first-class result with a named binding constraint |
| 7 | Inherited plane-change claim was arithmetically false | Re-measured, corrected, pinned by a test |
| 8 | Benchmark placeholders looked like results | Replaced with explicit zeros and `"not-yet-run"` |

---

## 16. Residual Risks

| Risk | Severity | Mitigation | Owner |
|---|---|---|---|
| CMake absent | **High** | Install before Phase 1 — the only hard blocker | Setup |
| Label explosion at scale | Medium | ε-dominance, caps, measured in benchmarks; `truncated` always reported | Phase 8 |
| FIFO property violated by a future transfer model | Medium | Property test; failure voids optimality and must fix the model | Phase 5 |
| Snapshot bandwidth at 1000 objects | Low | Coalescing; escalation path to delta snapshots behind `io/` | Phase 13 |
| Scope creep in visualisation | Medium | Tiering; engine sequenced entirely before frontend | Phase 12+ |
| `fuel %` misread | Low | Three separate mitigations; not fully solved | Ongoing |
| Cross-problem score comparability | Low | Documented; priority factor used across rows | Phase 9 |
| Pareto shows no measured benefit over `weighted-single` | Medium | **Pre-registered:** report it and narrow the claim | Phase 16 |

The last row is the one worth re-reading. The project's headline claim is empirical, and the
protocol commits in advance to reporting a negative result. That commitment is what makes the
positive result meaningful if it arrives.

---

## 17. Overall Assessment

The architecture is sound for its purpose: a C++ DSA project with a serious research posture
and a legible interface.

**Strengths**

1. Layering demonstrated, not merely asserted — the UI technology already changed with zero
   core impact.
2. The central trade-off is derived from a validated physical measurement, not assumed.
3. Every algorithm has an independent oracle, not a self-generated expectation.
4. Determinism is designed in at four levels, including the subtle one (implementation-defined
   standard distributions).
5. Two data structures are made *conditional on measurement*, resisting the DSA-project urge to
   include structures for their own sake.
6. Limitations are enumerated rather than discovered.

**Weaknesses**

1. Reactive-only replanning — the deepest limitation, and blocked by `ObjectiveVector`'s scalar
   assumption.
2. Front-relative normalisation compromises cross-problem comparability.
3. Model designed by the same people evaluating it — partially, not fully, mitigated.
4. Large scope for the timeline.

**Verdict: proceed to Phase 1.** No blocking architectural defect. Install CMake first.
