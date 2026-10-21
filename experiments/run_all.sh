#!/usr/bin/env bash
# Reproduces every experiment in the paper with the exact parameters used.
# Approximate single-core run times on a 2.1 GHz Xeon: E0 25 min (exhaustive
# enumeration), E1 2 min, E2 10 min, E3 8 min, E4 dispatch 25 min, E4 replanning 50 min.
# E2 measures time: run it on an otherwise idle machine.
set -euo pipefail
cd "$(dirname "$0")"
make -j"$(nproc)"
mkdir -p results/e0 results/e1 results/e2 results/e3 results/e4
B=build/bin
$B/e0_verify 10 "" 5000000 > results/e0/e0_verify.csv
$B/e1_feasibility 30 results/e1/queries.csv results/e1/fronts.csv 25 50 100 200
$B/e2_scaling 10 5 results/e2/q_small.csv results/e2/f_small.csv 25 50 100 200
$B/e2_scaling 5 3 results/e2/q_300.csv results/e2/f_300.csv 300
$B/e2_scaling 3 1 results/e2/q_500.csv results/e2/f_500.csv 500
$B/e3_assignment 30 results/e3/instances.csv results/e3/cells.csv 2 5 10 20 30
$B/e4_dispatch dispatch 30 results/e4/dispatch_50.csv 50 0
$B/e4_dispatch dispatch 30 results/e4/dispatch_100.csv 100 0
$B/e4_dispatch hysteresis 30 results/e4/hyst_50.csv 50 6
$B/e4_dispatch hysteresis 30 results/e4/hyst_100.csv 100 6
python3 analysis/merge_e2.py
python3 analysis/export_numbers.py > /dev/null
echo "All numbers quoted in the paper: results/numbers.json"
