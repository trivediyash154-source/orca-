# ORCA — Final Benchmark Report

Version 1.0 · Phase 11 · 2026-09-03

This is the methodology/environment/raw-results companion to `docs/RESEARCH_EVALUATION.md`
(which analyzes the algorithmic questions in depth) and covers everything
`RESEARCH_EVALUATION.md` does not: the benchmark harness itself, the environment it ran in,
the real generation/construction timings at scale, the instanced-rendering measurement, and
two real bugs found along the way. Every number below was either read directly from
`benchmarks/results/*.csv` (real `orca_engine --bench` output) or produced by a standalone
probe compiled and run during this phase, shown alongside its own command.

---

## 1. What Was Built

- `cpp/analytics/Statistics.hpp/.cpp` — `computeStatistics()`: mean, median, min, max,
  population standard deviation. Verified against Wikipedia's own worked standard-deviation
  example (`tests/analytics/statistics_test.cpp`).
- `cpp/analytics/BenchmarkRunner.hpp/.cpp` — `runSearchComparisonBenchmark()`: builds one
  `MissionGraph`, runs Dijkstra/A\*/Pareto search from the first vehicle to the first
  satellite, `warmupRuns + repetitions` times each.
- `cpp/analytics/AssignmentBenchmark.hpp/.cpp` — `runAssignmentBenchmark()`: builds one
  candidate-cost matrix (`generateAllCandidateFronts()` → `scoreCandidates()` →
  `buildAssignmentCosts()` → `buildAssignmentMatrix()`), times that generation phase
  SEPARATELY from Greedy's and Hungarian's own solve time (kept apart per the brief's
  explicit requirement, and because Phase 6 already showed candidate generation dominates —
  §4 below re-confirms this at larger scale).
- `cpp/analytics/CsvWriter.hpp/.cpp` — append-mode CSV writer; header written once; fields
  that do not apply to a given algorithm are empty, never a fabricated zero.
- `orca_engine --bench` (`cpp/app/main.cpp`'s `runBenchmarkSuite()`) — runs the default
  suite and writes both CSVs.
- `orca_engine --serve <family>:<size>[:<seed>]` — new in Phase 11, boots the NDJSON protocol
  loop from a generated scenario instead of the small fixed demo one; built specifically to
  make §5's real-browser rendering measurement possible (there was previously no way to
  serve a large scenario live).

## 2. Environment

| | |
|---|---|
| Machine | Apple M4, 10 cores |
| Memory | 16 GB |
| OS | Darwin 24.6.0 (macOS), arm64 |
| Compiler | Apple clang 17.0.0 (`Apple clang version 17.0.0 (clang-1700.3.19.1)`) |
| Build flags | `-std=c++20 -O2 -DNDEBUG` plus the project's full warning set (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wcast-align -Wdouble-promotion -Werror`) |
| Date | 2026-09-03 |

Single machine, single build — see `RESEARCH_EVALUATION.md` §8's threats-to-validity note on
why absolute timings here are not portable across hardware.

## 3. The Default `--bench` Suite (as actually run)

- Search: families `{sparse, normal, dense, fuelConstrained, highDebris, tradeoffRich}` ×
  sizes `{25, 50, 100}` × seeds `{1, 2, 3}`, 1 warm-up + 5 repetitions per (family, size,
  seed, algorithm) triple across Dijkstra/A\*/Pareto → **972 rows**,
  `benchmarks/results/phase11_search_comparison.csv`.
- Assignment: matrix sizes `{2, 5, 10, 20}` × seeds `{1, 2, 3}`, 1 warm-up + 5 repetitions ×
  Greedy/Hungarian → **144 rows**, `benchmarks/results/phase11_assignment_comparison.csv`.
- Wall-clock time for the full default suite: **7.2 seconds**, verified by timing the actual
  `--bench` run.

## 4. A Real Scaling Limitation, Found Empirically

The FIRST attempt at this suite used the brief's fuller requested matrix: all 8 scenario
families, sizes up to 500, and assignment matrix sizes up to 50. That run was still going
after **36+ minutes** and had to be killed (`kill -9`) before completing. Reading the partial
output showed the search benchmark (sizes up to 500) actually finished in reasonable time —
the assignment benchmark stalled around `matrixSize=50`.

**Root cause** (confirmed by reading `AssignmentBenchmark.cpp`, not assumed):
`generateAllCandidateFronts()` runs one Pareto search per `(vehicle, emergency)` pair —
`O(vehicles × emergencies)` Pareto searches per repetition. At `matrixSize=50` (50 vehicles,
50 emergencies), that is 2,500 Pareto searches per repetition × 6 repetitions (1 warm-up + 5)
= **15,000 Pareto searches** for a single `(matrixSize, seed)` cell, and
`RESEARCH_EVALUATION.md` §5.2 independently shows a single Pareto search already costs 46×
more time at 100 objects than at 25 — the two findings corroborate each other from different
angles: candidate generation is expensive both because it runs *many* Pareto searches and
because each individual search itself scales badly.

This is a real, valuable, and previously-undocumented-at-this-scale confirmation of Phase 6's
own "candidate generation dominates" finding, now extended to larger assignment-matrix sizes.
**The default `--bench` suite's scope (§3) was reduced in response to this finding** — not to
hide it, but because a suite that takes 36+ minutes to run is not a suite anyone will actually
run. The underlying capability (`runAssignmentBenchmark()`) still accepts any `matrixSize`;
only the CLI's *default* invocation was scoped down.

## 5. Scenario Generation and Engine Construction at Scale

Standalone probes (not part of `--bench`; compiled and run directly against
`ScenarioGenerator`/`SimulationEngine`, `-O2 -DNDEBUG`, `dense` family, seed 1):

| Object count | `generateScenario()` | `validateGeneratedScenario()` | `SimulationEngine` construction (incl. graph build) | Graph nodes | Graph edges |
|---|---|---|---|---|---|
| 500 | ~1.0–1.1 ms (first run: 2.8ms, cold cache) | ~0.3–0.9 ms | ~102–131 ms | 562 | 315,282 |
| 1000 | ~1.9 ms | ~0.6 ms | ~402–409 ms | 1,125 | 1,264,500 |

Three consecutive runs each; ranges shown rather than a single point estimate, since the
first run of a process is consistently slower (cold instruction/data cache) — reported
honestly rather than picking the most flattering number. Edge count grows roughly with the
square of node count (315,282 → 1,264,500 for a ~2× node-count increase is a ~4× edge-count
increase), consistent with the mission graph's all-pairs-candidate structure
(`ALGORITHMS.md`'s own `b ≈ 2` prediction for graph construction, §7.1 of
`BENCHMARK_PLAN.md`).

A live 500-satellite/62-vehicle/33-debris scenario was also served over the real NDJSON
protocol (`orca_engine --serve dense:500:1`) and confirmed to produce a correct snapshot with
exactly those object counts — real, not simulated (§6 reuses this same served scenario).

## 6. Instanced Rendering — A Real, Measured Draw-Call Comparison

Phase 11 brief §32-35 asked for measured visualization-scaling work, not a theoretical claim.
Satellite and vehicle markers were converted from one Three.js mesh per object
(`EntityMarker.tsx`, Phase 10) to one `THREE.InstancedMesh` per kind
(`EntityMarkerField.tsx`, Phase 11) — see `docs/VISUAL_DESIGN.md` §6-7 for the visual/design
detail. This section reports how the improvement was actually measured.

**Method**: a real backend (`backend/`) was pointed (via `ORCA_ENGINE_PATH`) at a wrapper
script invoking `orca_engine --serve dense:500:1`, and a real Vite dev server was pointed at
that backend. Headless Chromium (Playwright) loaded the real frontend, clicked the real
"Start" button, waited for a real snapshot to render, then measured actual WebGL draw calls
by wrapping `WebGLRenderingContext.prototype.drawArrays/drawElements` (and their instanced
variants) at the browser API level — a hardware/three.js-version-independent measurement of
what the GPU was actually asked to draw, not an internal three.js counter.

**Result**, at the identical 500-satellite/62-vehicle scenario, same browser session type,
reproducible across repeated runs (identical numbers on repeat):

| | Draw calls / frame |
|---|---|
| Phase 10 (one mesh per marker) | **1,128** |
| Phase 11 (one `InstancedMesh` per kind) | **568** |
| Reduction | 560 draw calls (49.6%) |

The math checks out exactly: 562 individual markers (500 satellites + 62 vehicles) replaced
by 2 `InstancedMesh` draw calls (one per kind) saves exactly 560 draw calls (562 − 2 = 560),
matching the measured 1,128 − 568 = 560 difference precisely — the remaining ~566 draw calls
in both configurations come from the scene's other elements (Earth, starfield, and
critically, orbit rings).

**Orbit rings are the real remaining bottleneck, documented honestly, not hidden**: with
markers now instanced, orbit rings (one `drei` `Line` per satellite/vehicle, un-instanced)
account for ~566 of the scene's ~568 draw calls per frame — the *dominant* cost at this
scale. They were not instanced in this phase because, unlike markers, each ring's shape AND
orientation genuinely differ (not just position/scale), which `THREE.InstancedMesh`'s single
shared geometry does not accommodate without a custom per-instance-geometry shader —
triaged as RECOMMENDED future work (`docs/VISUAL_DESIGN.md` §6), not silently left unstated.

**A caveat stated honestly**: headless Chromium typically renders via a software rasterizer
(no GPU acceleration), so the ~1-1.5 fps observed during measurement is not representative of
real hardware-accelerated frame rates a user would see — but draw-call COUNT is a CPU-side,
API-level metric that does not depend on the GPU backend, so the 1,128→568 comparison itself
is real and portable even though the absolute fps number is not reported as a performance
claim here.

## 7. Two Real Bugs Found During This Measurement

Neither was anticipated; both were found because this phase's P4 work required actually
loading the app in a real browser for the first time in the project's history (every prior
phase's "browser" testing was either component-level jsdom tests, which do not enforce CORS
or run real WebGL, or REST integration tests using Node's own `fetch`, which also does not
enforce CORS).

1. **Missing CORS headers** (`backend/src/api/server.ts`): the Node bridge had no
   `Access-Control-Allow-Origin` header at all. A real browser on the Vite dev server's
   origin (a different port than the backend) was silently blocked from every request.
   Fixed with a minimal permissive CORS middleware (justified: this is a local
   demo/simulation tool with no auth or sensitive data, not a public API). Regression test:
   `backend/test/server.test.ts`'s `responds with a permissive CORS header...`.
2. **Bootstrap deadlock** (`backend/src/index.ts`): `/snapshot` returned 503 until SOME
   client sent a command, but the frontend's Start button — the only UI control that sends a
   command — was disabled until `connectionStatus === 'Connected'`, which itself required a
   successful `/snapshot` fetch. A fresh page load could never recover from this: the one
   button that would break the deadlock was permanently disabled by the deadlock itself.
   Fixed by sending one `getSnapshot` command right after the engine announces `ready`, in
   `index.ts`'s own bootstrap — a no-mutation command that already existed, not a new one.
   Regression test: `backend/test/server.test.ts`'s
   `GET /snapshot succeeds immediately at boot, before any client has sent a command`.

Both are documented here rather than only in commit history because they are genuinely
significant: without them, the shipped app was unusable from a real browser on a fresh load,
despite every existing automated test passing. This is the clearest evidence in this whole
phase for why `README.md`/`docs/VIVA_GUIDE.md` should tell a future maintainer to actually
open the app in a browser occasionally, not rely on the test suite alone.

## 8. Interpretation — Do Not Oversell

- The instancing win (§6) is real but partial: it fixes the O(N) marker-draw-call problem
  completely, but the scene's total draw-call count is now dominated by a DIFFERENT O(N)
  cost (orbit rings) that this phase did not fix. Reporting "instanced rendering: done"
  without this caveat would be misleading.
- The A\*-vs-Dijkstra and Pareto-vs-single-objective scaling results
  (`RESEARCH_EVALUATION.md` §5.1-5.2) are real and internally consistent, but come from ONE
  seed-paired benchmark suite on ONE machine — they characterize ORCA's own graph/algorithm
  implementation, not general claims about A\*/Dijkstra/Pareto search in the abstract.
- The assignment-regret finding (`RESEARCH_EVALUATION.md` §5.3) is the strongest result in
  this report specifically because a wrong, more flattering-sounding initial reading ("greedy
  is cheaper") was caught and corrected before being reported — stated here again because it
  is the clearest evidence that this phase's numbers were independently checked, not taken at
  face value.
- Nothing in this report claims ORCA's benchmark suite matches `BENCHMARK_PLAN.md`'s full
  aspirational scope (Wilcoxon tests, hypervolume, memory profiling, JSON metadata export).
  `docs/BENCHMARK_PLAN.md`'s own Phase 11 correction note and `RESEARCH_EVALUATION.md` §7
  state precisely what is missing.
