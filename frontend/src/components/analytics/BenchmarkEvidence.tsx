import { useEffect, useState } from 'react';

import { fetchBenchmarkCsv } from '../../services/engineService';
import { mean, numField, parseCsv } from '../../utils/csv';
import { useEngine } from '../../state/EngineContext';

interface SizeAlgoRow {
  readonly size: number;
  readonly algorithm: string;
  readonly meanMs: number;
  readonly samples: number;
}

/** Real, aggregated `mean(executionTimeMs)` grouped by `(objectCount, algorithm)`,
 * warm-up repetitions (`repetition < 0`, `cpp/analytics/BenchmarkTypes.hpp`'s own
 * convention) excluded -- computed here from the real parsed CSV, the same aggregation
 * `docs/RESEARCH_EVALUATION.md` §5.1 did once offline in Python, now live in the browser. */
function aggregateSearchCsv(csvText: string): SizeAlgoRow[] {
  const { rows } = parseCsv(csvText);
  const groups = new Map<string, number[]>();
  for (const row of rows) {
    const repetition = numField(row, 'repetition');
    if (repetition === undefined || repetition < 0) continue; // exclude warm-up
    const size = numField(row, 'objectCount');
    const ms = numField(row, 'executionTimeMs');
    const algorithm = row.algorithm;
    if (size === undefined || ms === undefined || !algorithm) continue;
    const key = `${size}::${algorithm}`;
    const list = groups.get(key) ?? [];
    list.push(ms);
    groups.set(key, list);
  }
  const result: SizeAlgoRow[] = [];
  for (const [key, values] of groups) {
    const [sizeText, algorithm] = key.split('::');
    result.push({ size: Number(sizeText), algorithm: algorithm!, meanMs: mean(values), samples: values.length });
  }
  return result.sort((a, b) => a.size - b.size || a.algorithm.localeCompare(b.algorithm));
}

interface MatrixRow {
  readonly matrixSize: number;
  readonly meanCandidateGenMs: number;
  readonly meanSolverMs: number;
  readonly samples: number;
}

/** Real per-`matrixSize` means of `candidateGenerationTimeMs`/`solverTimeMs` for HUNGARIAN
 * rows -- the exact split `docs/FINAL_BENCHMARK_REPORT.md` §4's "candidate generation
 * dominates" finding is built from, computed live here from the real CSV rather than only
 * quoted as a static number in a doc. */
function aggregateAssignmentCsv(csvText: string): MatrixRow[] {
  const { rows } = parseCsv(csvText);
  const groups = new Map<number, { gen: number[]; solve: number[] }>();
  for (const row of rows) {
    const repetition = numField(row, 'repetition');
    if (repetition === undefined || repetition < 0) continue;
    if (row.algorithm !== 'HUNGARIAN') continue;
    const dim = numField(row, 'solverMatrixDimension');
    const gen = numField(row, 'candidateGenerationTimeMs');
    const solve = numField(row, 'solverTimeMs');
    if (dim === undefined || gen === undefined || solve === undefined) continue;
    const entry = groups.get(dim) ?? { gen: [], solve: [] };
    entry.gen.push(gen);
    entry.solve.push(solve);
    groups.set(dim, entry);
  }
  return [...groups.entries()]
    .map(([matrixSize, { gen, solve }]) => ({
      matrixSize,
      meanCandidateGenMs: mean(gen),
      meanSolverMs: mean(solve),
      samples: gen.length,
    }))
    .sort((a, b) => a.matrixSize - b.matrixSize);
}

/** Phase 12 brief §17: real OFFLINE benchmark evidence, clearly separated from the LIVE
 * SESSION metrics `AnalyticsPage` shows above this component -- `orca_engine --bench`'s own
 * real output (`benchmarks/results/*.csv`), read from disk by the backend
 * (`GET /benchmarks/:name`) and aggregated here, never regenerated or invented for display. */
export function BenchmarkEvidence() {
  const { baseUrl } = useEngine();
  const [searchRows, setSearchRows] = useState<SizeAlgoRow[] | null>(null);
  const [assignmentRows, setAssignmentRows] = useState<MatrixRow[] | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    Promise.all([fetchBenchmarkCsv(baseUrl, 'search'), fetchBenchmarkCsv(baseUrl, 'assignment')])
      .then(([searchCsv, assignmentCsv]) => {
        if (cancelled) return;
        setSearchRows(aggregateSearchCsv(searchCsv));
        setAssignmentRows(aggregateAssignmentCsv(assignmentCsv));
      })
      .catch((err: unknown) => {
        if (!cancelled) setError(err instanceof Error ? err.message : String(err));
      });
    return () => {
      cancelled = true;
    };
  }, [baseUrl]);

  if (error) {
    return (
      <section className="analytics-breakdown">
        <h2>Offline Benchmark Evidence</h2>
        <p className="muted">
          Not available: {error}. Run <code>orca_engine --bench</code> to generate
          <code>benchmarks/results/*.csv</code>.
        </p>
      </section>
    );
  }

  if (!searchRows || !assignmentRows) {
    return (
      <section className="analytics-breakdown">
        <h2>Offline Benchmark Evidence</h2>
        <p className="muted">Loading real benchmark CSV data…</p>
      </section>
    );
  }

  const maxSearchMs = Math.max(0.001, ...searchRows.map((r) => r.meanMs));
  const maxMatrixMs = Math.max(0.001, ...assignmentRows.map((r) => r.meanCandidateGenMs));

  return (
    <>
      <section className="analytics-breakdown">
        <h2>Search Runtime by Scenario Size (offline, real, {searchRows[0]?.samples ?? 0}+ samples/cell)</h2>
        <p className="muted">
          Mean <code>executionTimeMs</code> from <code>benchmarks/results/phase11_search_comparison.csv</code>{' '}
          (real <code>orca_engine --bench</code> output, warm-up repetitions excluded).
        </p>
        <div className="analytics-bars">
          {searchRows.map((r) => (
            <div key={`${r.size}-${r.algorithm}`} className="analytics-bar-row">
              <span className="analytics-bar-label">
                {r.algorithm} @ {r.size}
              </span>
              <div className="analytics-bar-track">
                <div className="analytics-bar-fill analytics-bar-fill-mission" style={{ width: `${(r.meanMs / maxSearchMs) * 100}%` }} />
              </div>
              <span className="analytics-bar-count">{r.meanMs.toFixed(3)} ms</span>
            </div>
          ))}
        </div>
      </section>

      <section className="analytics-breakdown">
        <h2>Candidate Generation vs. Solver Time (offline, real, Hungarian)</h2>
        <p className="muted">
          From <code>benchmarks/results/phase11_assignment_comparison.csv</code> --
          the real evidence behind the "candidate generation dominates" finding
          (<code>docs/FINAL_BENCHMARK_REPORT.md</code> §4).
        </p>
        <div className="analytics-bars">
          {assignmentRows.map((r) => (
            <div key={r.matrixSize} className="analytics-bar-row">
              <span className="analytics-bar-label">matrix={r.matrixSize}</span>
              <div className="analytics-bar-track">
                <div
                  className="analytics-bar-fill analytics-bar-fill-emergency"
                  style={{ width: `${(r.meanCandidateGenMs / maxMatrixMs) * 100}%` }}
                />
              </div>
              <span className="analytics-bar-count">
                gen {r.meanCandidateGenMs.toFixed(2)}ms / solve {r.meanSolverMs.toFixed(4)}ms
              </span>
            </div>
          ))}
        </div>
      </section>
    </>
  );
}
