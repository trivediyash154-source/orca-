# ORCA — Repository Audit

**Date:** 2026-09-02
**Scope:** Full repository contents, inherited design work, build toolchain, model pre-validation.
**Status:** Supersedes the previous audit of the same name (see §2).

---

## 1. What Was Found

The repository contained **a directory skeleton and exactly one file**:

```
docs/REPOSITORY_AUDIT.md      (207 lines)
```

Every other directory was empty. There is no source code, no prototype, no prior orbital
visualisation, and no version control (`git rev-parse` → *not a git repository*).

Inherited directory skeleton:

```
assets/{earth,fonts,icons,textures}   benchmarks/   cmake/   configs/
cpp/{app,core,domain,graph,algorithms,optimization,simulation,planning,analytics}
cpp/ui/{screens,widgets}   cpp/visualization/
scenarios/   tests/{algorithms,graph,optimization,simulation,planning,integration}
```

So the honest summary is: **there is no code to reuse, but there *is* prior design work to
audit** — and that prior design work is the substantive finding of this pass.

---

## 2. The Inherited Document Is Architecturally Superseded

The previous `REPOSITORY_AUDIT.md` documents a Phase 0 pass that selected a **Qt 6 desktop
application** as ORCA's presentation layer. Evidence, both in the document and in the
directory skeleton it created:

- §2 audits Qt 6 as a required dependency and reports it absent.
- §2.2 specifies a bootstrap path installing Qt via Homebrew.
- §2.3 defines a "fallback if Qt never becomes available".
- It created `cpp/ui/{screens,widgets}` and `cpp/visualization/` — a **C++-owned UI layer**.

The current Phase 0 brief specifies a fundamentally different presentation stack: **React +
Three.js frontend, Node/Express bridge, C++ engine**. These are not reconcilable. A C++ Qt
UI and a React UI are mutually exclusive answers to the same question.

### Disposition

| Inherited item | Disposition | Reason |
|---|---|---|
| Qt 6 as UI layer | **Rejected** | Superseded by React/Three.js brief |
| `cpp/ui/`, `cpp/visualization/` | **Removed** | C++ owns no UI under the new architecture. Verified empty (0 files) before removal — no functionality lost |
| Qt absence listed as project risk | **Retired** | No longer a dependency; risk deleted rather than carried |
| `cpp/{core,domain,graph,algorithms,optimization,simulation,planning,analytics}` | **Kept** | Stack-independent. These are the computational core and survive the UI change untouched |
| `tests/` subdivision | **Kept** | Mirrors `cpp/` layout; still correct |
| `assets/`, `configs/`, `scenarios/`, `benchmarks/`, `cmake/` | **Kept** | Stack-independent |
| Toolchain findings (§2) | **Re-verified**, not trusted | See §3 |
| Orbital model validation (§4) | **Re-verified and one claim corrected** | See §4 |

**This is the strongest argument for the layering rule in `ARCHITECTURE.md`.** The UI
technology changed completely between two Phase 0 passes, and *nothing in the computational
core needed to change*. That is not a coincidence — it is the property the architecture is
designed to have, and it has now been demonstrated once for real rather than merely asserted.

### Structure created by this pass

```
cpp/io/                                       # engine ⇄ outside world (NDJSON, scenarios)
frontend/src/{components,pages,layouts,hooks,services,state,types,charts,styles}
frontend/src/visualization/{scene,earth,orbit,satellites,trajectories,graph}
frontend/public/
backend/src/{api,services,engine,ipc,config}
scripts/
```

Directories are intentionally empty apart from `docs/`. Phase 0 does not create placeholder
source files: empty stubs are indistinguishable from unimplemented work and rot immediately.

---

## 3. Toolchain Audit

Every row was produced by executing a command in this session on this machine.

| Component | Status | Detail |
|---|---|---|
| C++ compiler | ✅ Present | Apple clang 17.0.0 (clang-1700.3.19.1), `arm64-apple-darwin24.6.0` |
| C++20 language | ✅ Full | `__cplusplus = 202002L` |
| `<concepts>` | ✅ | `__cpp_concepts = 202002` |
| `<ranges>` | ✅ | `__cpp_lib_ranges = 202110` |
| `<format>` | ✅ | `__cpp_lib_format = 202110` |
| `<span>` | ✅ | `__cpp_lib_span = 202002` |
| **Node.js** | ✅ Present | **v24.16.0** (`/usr/local/bin/node`) |
| **npm** | ✅ Present | **11.13.0** |
| git | ✅ Present | 2.50.1 — but **repo not initialised** |
| make | ✅ Present | GNU Make 3.81 (ancient, but CMake only needs *a* generator) |
| Python 3 | ✅ Present | 3.14.2 — *not used by ORCA*; noted only for completeness |
| **CMake** | ❌ **ABSENT** | `cmake: command not found` |
| Ninja | ❌ Absent | Optional; Make suffices |
| Homebrew | ❌ Absent | — |
| Qt 6 | ❌ Absent | **No longer relevant** — dependency removed |

Verified by compiling and running a feature-probe translation unit under `-std=c++20`.

### 3.1 Interpretation

Two findings change the plan materially versus the inherited audit:

1. **Node.js 24 is already installed.** The inherited audit never checked for it, because
   Qt needed no JavaScript runtime. The newly-selected stack's *entire* frontend and bridge
   toolchain is therefore already available. This is a real point in favour of the new stack
   on this machine: React + Node needs **zero** new system installs, whereas Qt needed
   Homebrew plus a multi-gigabyte SDK.

2. **CMake is the only hard blocker,** and it blocks only the C++ engine. It does not block
   Phase 0 (documents), and it does not block frontend or backend work, which use npm.

C++20 is fully available including `std::format` and ranges — the engine does not need to
restrict itself to C++17 idioms or vendor a formatting library.

### 3.2 Bootstrap path

1. **CMake** — required before Phase 1. Either the official macOS `.dmg` from cmake.org
   (then add `/Applications/CMake.app/Contents/bin` to `PATH`), or install Homebrew and
   `brew install cmake ninja`. Homebrew is the better long-term choice.
2. **Ninja** — optional. Faster incremental builds; Make works.
3. **GoogleTest** — *no action needed.* Acquired via CMake `FetchContent` pinned to a release
   tag. Needs network at configure time, no system install. Keeps the test dependency
   reproducible and removes a manual setup step.
4. **`git init`** — recommended. The roadmap assumes milestone-sized commits. **Not performed
   by this pass; left as the owner's call.**

### 3.3 Degradation behaviour

The architecture keeps three independently-runnable layers, so a missing dependency degrades
rather than blocks:

- No CMake → engine cannot build; frontend and backend still develop against recorded
  fixture snapshots (`API_CONTRACT.md` §9).
- No Node → engine still fully usable via `orca_engine` CLI batch mode; all benchmarks,
  experiments and tests run headless. **No algorithmic substance is lost.**
- No browser → same as above.

This is the same structural insurance the inherited audit bought with its no-Qt-below-`ui/`
rule, carried forward to the new stack.

---

## 4. Model Pre-Validation — and a Correction

Because there was no code to audit, the audit budget was redirected into de-risking the core
technical model *before* it was written into design documents. A throwaway reference
implementation of the intended orbital-transfer model was compiled and run
(`μ = 398600.4418 km³/s²`, `R⊕ = 6378.137 km`), and checked against published textbook values.

| Check | ORCA model output | Textbook reference | Verdict |
|---|---|---|---|
| Circular velocity @ 500 km | 7.6126 km/s | ≈7.61 km/s | ✅ |
| LEO(300 km)→GEO Hohmann Δv | 3.8926 km/s | ≈3.9 km/s | ✅ |
| LEO→GEO transfer time | 316.5 min (5.28 h) | ≈5.25 h | ✅ |
| Plane change 1° @ 500 km | 0.1329 km/s (132.9 m/s) | ≈133 m/s | ✅ |
| Orbital period @ 500 km | 94.62 min | ≈94.6 min | ✅ |

The formulae reproduce textbook values to the precision quoted. They are transcribed into
`SIMULATION_MODEL.md` §3.

### 4.1 Correction to the inherited audit

The previous audit stated:

> "One degree of inclination change (0.133 km/s) costs *more* than an entire 500→800 km
> altitude raise (0.161 km/s for two burns)."

**This is arithmetically wrong: 0.133 < 0.161.** The claim as written is false and would
have been carried into `SIMULATION_MODEL.md` and `OPTIMIZATION_MODEL.md` as a design
premise. Re-measured values:

```
plane change 1° @ 500 km      = 0.1329 km/s
Hohmann 500 km → 800 km       = 0.1608 km/s   (two burns)
ratio                         = 0.83
break-even                    ≈ 1.21° of inclination
```

**The corrected claim — which is still striking, and still load-bearing —** is that one
degree of inclination change costs **83% of a 300 km altitude raise**, and roughly
**1.2° is equivalent to it**. Inclination is therefore not decorative: a single-digit
inclination difference dominates any plausible altitude difference in LEO. This preserves
the architectural consequence the inherited document was reaching for (a
nearest-neighbour-by-Euclidean-distance heuristic is *physically wrong*, so the benchmark
suite has a real effect to measure) while stating the magnitude correctly.

### 4.2 The finding that produces ORCA's central trade-off

```
synodic period, 500 km vs 800 km = 1525.3 min ≈ 25.4 hours
period @ 500 km = 94.62 min   period @ 800 km = 100.87 min
```

Ideal phase alignment between two nearby LEO orbits recurs only about **once a day**. A
rescue planner facing a survival window measured in tens of minutes **cannot wait for it**.

The transfer model must therefore price *suboptimal-phase* departures, producing a continuous
**"go now and pay Δv" vs "wait and pay time"** trade-off. This is the origin of ORCA's
time/fuel Pareto tension: it *emerges from the physics* rather than being asserted to justify
using a multi-objective algorithm. This is the single most defensible fact in the project and
is the backbone of `OPTIMIZATION_MODEL.md` §2 and `VIVA_GUIDE.md` Q7.

The scratch validation code is not part of the repository.

---

## 5. Findings Against the Requested Audit Categories

| Requested category | Finding |
|---|---|
| Existing structure | Directory skeleton only, 1 file. Partially reused, partially removed (§2) |
| Existing technologies | None in repo. Toolchain audited separately (§3) |
| Reusable components | **No code.** Reused: directory layout for the computational core, and the validated orbital formulae (§4) |
| Broken components | One **factually incorrect claim** in the inherited audit — corrected in §4.1 |
| Architectural problems | Inherited architecture selected Qt; superseded (§2) |
| Duplicate code | None |
| Missing components | Everything. Sequenced in `IMPLEMENTATION_ROADMAP.md` |
| Recommended migration path | Not applicable — greenfield. The "migration" is documentary only (§2) |
| Files that should remain | `cpp/{core,domain,graph,algorithms,optimization,simulation,planning,analytics}`, `tests/`, `assets/`, `configs/`, `scenarios/`, `benchmarks/`, `cmake/` |
| Files that should be refactored | `docs/REPOSITORY_AUDIT.md` — rewritten by this pass |
| Files that should be removed | `cpp/ui/`, `cpp/visualization/` — removed, verified empty first |

---

## 6. Recommendations

1. **Install CMake before Phase 1.** The only hard blocker on the critical path.
2. **`git init`.** Not yet under version control; the roadmap assumes commits.
3. **Do not install Python or Java packages for ORCA.** Python 3.14 is present on the
   machine but ORCA has no use for it in the core product (see `ARCHITECTURE.md` §3).
4. **Keep GoogleTest on `FetchContent`.** Reproducible, zero manual setup.
5. **Enforce the layering rule mechanically** (`TEST_PLAN.md` §9). The UI technology already
   changed once during Phase 0; assume it can change again.
6. **Carry the corrected inclination figure**, not the inherited one, into every downstream
   document and viva answer.

---

## 7. Audit Integrity Statement

Every fact in this document was produced by executing a command, or by compiling and running
code, in this session on this machine. No finding is inferred. Where a category had no
members, this document says so explicitly rather than filling space. Where the inherited
document was wrong, this document says so and shows the arithmetic.
