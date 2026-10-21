# ORCA evaluation harnesses

Everything needed to reproduce the numbers, tables and figures in the paper
"ORCA: Exact Multi-Objective Planning for Earth-Orbit Rescue". Every number the paper
quotes is written by `analysis/export_numbers.py` to `results/numbers.json`.

## Build and run

```
cd experiments
./run_all.sh          # builds, runs E0 to E4 with the paper's parameters, exports numbers
```

Requirements: Clang 18 or newer (the Makefile defaults to `clang++`; GCC 13 rejects an
out-of-order designated initializer in upstream `cpp/io/ViewProjection.cpp`),
Python 3 with `numpy`, `scipy` and `moocore` (exact hypervolume).

Approximate single-core run times on a 2.1 GHz Xeon: E0 25 min, E1 2 min, E2 10 min,
E3 8 min, E4 dispatch 25 min, E4 replanning 50 min. E2 measures wall-clock time, so run
it on an otherwise idle machine.

## Experiments

| Id | Question | Harness | Output |
|----|----------|---------|--------|
| E0 | Correctness: NAMOA* equals Martins; Martins equals exhaustive enumeration; A* equals Dijkstra | `e0_verify.cpp` | `results/e0/` |
| E1 | What earliest-arrival search and weighted sums miss (budget violations, unsupported plans) | `e1_feasibility.cpp` | `results/e1/` |
| E2 | Cost and scaling of Dijkstra, A*, Martins, NAMOA* and epsilon-dominance, 25 to 500 objects | `e2_scaling.cpp` | `results/e2/` |
| E3 | Hungarian against greedy assignment on 450 distinct matrices | `e3_assignment.cpp` | `results/e3/` |
| E4 | End-to-end dispatch (solver x dispatch cycle x survival window) and replanning margin sweep | `e4_dispatch.cpp` | `results/e4/` |

The `highDebris` scenario family is not used: debris objects do not enter graph
construction or edge costs, so its search graphs are identical to `normal`.

## Engine changes these experiments rely on

* `cpp/algorithms/NamoaStar.{hpp,cpp}`: NAMOA* with the same admissible time bound as A*.
* `SimulationEngineConfig` gains three evaluation options, all off by default:
  `dispatchSolver` (Hungarian or Greedy), `dispatchCycleSec` (batch dispatch) and
  `singleSortie` (retire a vehicle after one rescue).
* Replanning commitment fix (`MissionValidity`, `DynamicReplanner`, `EventDispatcher`,
  `Mission::planDepartureSec()`): a replan now starts from the end of the leg in progress,
  at that leg's arrival time, with its delta-v committed, and the new plan keeps that leg.
  Before the fix a mid-leg replan skipped the rest of the leg and never charged its
  delta-v. Covered by `tests/planning/replan_commitment_test.cpp`; the expected delta-v in
  `tests/simulation/end_to_end_demo_test.cpp` changes from 8.9910 to 8.5548 km/s
  accordingly.

## Archived file

`results/e4/prefix_replanning.csv` was produced by the same harness on the engine as it was
before the commitment fix (upstream commit a2c7aac plus the default-off evaluation
options). It documents the defect quantitatively and cannot be regenerated from the fixed
engine.
