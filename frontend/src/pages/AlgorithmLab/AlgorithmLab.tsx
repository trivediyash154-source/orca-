import { Fragment, useState } from 'react';

import { useEngine } from '../../state/EngineContext';
import type { RunnableAlgorithm } from '../../services/engineService';
import type { AlgorithmRunView } from '../../types/engine';

const FIELD_LABELS: ReadonlyArray<[keyof AlgorithmRunView, string]> = [
  ['nodesExpanded', 'Nodes Expanded'],
  ['edgesEvaluated', 'Edges Evaluated'],
  ['relaxationsSucceeded', 'Relaxations Succeeded'],
  ['priorityQueuePushes', 'Queue Pushes'],
  ['priorityQueuePops', 'Queue Pops'],
  ['staleEntriesDiscarded', 'Stale Entries Discarded'],
  ['heuristicEvaluations', 'Heuristic Evaluations'],
  ['labelsGenerated', 'Labels Generated'],
  ['labelsInserted', 'Labels Inserted (non-dominated)'],
  ['labelsDominated', 'Labels Dominated'],
  ['labelsPruned', 'Labels Pruned'],
  ['dominanceComparisons', 'Dominance Comparisons'],
  ['maximumLabelsAtNode', 'Max Labels at a Node'],
  ['vehicleCount', 'Vehicles'],
  ['emergencyCount', 'Emergencies'],
  ['candidatePairsEvaluated', 'Candidate Pairs Evaluated'],
  ['feasibleCandidatePairs', 'Feasible Candidate Pairs'],
  ['assignedPairs', 'Assigned Pairs'],
  ['solverMatrixDimension', 'Matrix Dimension'],
  ['solverOperationCount', 'Solver Operations'],
];

const RUNNABLE: readonly RunnableAlgorithm[] = ['DIJKSTRA', 'ASTAR', 'PARETO', 'HUNGARIAN'];

function latestRunPerAlgorithm(runs: readonly AlgorithmRunView[]): AlgorithmRunView[] {
  const latest = new Map<string, AlgorithmRunView>();
  for (const run of runs) {
    latest.set(run.algorithm, run); // runs are chronological, so the last write wins
  }
  return [...latest.values()];
}

/** Brief §14-19/Phase 12 §16: real per-search telemetry. Two ways a run gets here, both
 * real: passively, whenever `EventDispatcher.cpp` dispatches a mission (Phase 11); or on
 * demand, via the RUN buttons below (`cpp/app/main.cpp`'s `applyRunAlgorithmCommand()`,
 * Phase 12) -- the same real Dijkstra, A-star, Pareto and Hungarian functions either way, so
 * this page cannot tell (and does not need to) which path produced a given card. */
export function AlgorithmLabPage() {
  const { snapshot, runAlgorithm } = useEngine();
  const runs = snapshot?.algorithmRuns ?? [];
  const [pending, setPending] = useState<RunnableAlgorithm | null>(null);
  const [runError, setRunError] = useState<string | null>(null);

  const handleRun = async (algorithm: RunnableAlgorithm) => {
    setPending(algorithm);
    setRunError(null);
    const ok = await runAlgorithm(algorithm);
    if (!ok) {
      setRunError(`${algorithm} did not run -- see message below.`);
    }
    setPending(null);
  };

  const latest = latestRunPerAlgorithm(runs);
  const latestByName = new Map(latest.map((r) => [r.algorithm, r]));

  return (
    <div className="algorithm-lab-page">
      <div className="algorithm-run-controls">
        {RUNNABLE.map((algo) => (
          <button key={algo} onClick={() => void handleRun(algo)} disabled={pending !== null}>
            {pending === algo ? `Running ${algo}…` : `Run ${algo}`}
          </button>
        ))}
      </div>
      {runError && <p className="lab-error">{runError}</p>}

      {runs.length === 0 ? (
        <p className="muted">
          No algorithm has run yet this session — click a button above to run one now, or wait
          for the first real mission dispatch (a satellite failure, in the canonical demo).
        </p>
      ) : (
        <>
          {latest.map((run) => (
            <div key={run.algorithm} className="algorithm-card">
              <h3>{run.algorithm}</h3>
              <dl className="algorithm-metrics">
                <dt>Runtime</dt>
                <dd>{run.executionTimeMs.toFixed(3)} ms</dd>
                {FIELD_LABELS.filter(([key]) => run[key] !== null && run[key] !== undefined).map(
                  ([key, label]) => (
                    <Fragment key={key}>
                      <dt>{label}</dt>
                      <dd>{String(run[key])}</dd>
                    </Fragment>
                  ),
                )}
              </dl>
            </div>
          ))}
          {RUNNABLE.filter((algo) => !latestByName.has(algo)).map((algo) => (
            <div key={algo} className="algorithm-card algorithm-card-empty">
              <h3>{algo}</h3>
              <p className="muted">Not run yet this session.</p>
            </div>
          ))}
        </>
      )}

      <p className="muted algorithm-lab-footnote">
        Dijkstra/A*/Pareto run against a real vehicle/target pair (the one selected in Mission
        Control if any, otherwise a real fallback the engine picks); Hungarian runs against
        every currently available vehicle and pending emergency, and honestly reports when
        there is nothing to assign rather than fabricating a result. None of these on-demand
        runs affects any real dispatch decision. Greedy-vs-Hungarian comparison at controlled
        matrix sizes is available via the benchmark runner
        (<code>docs/FINAL_BENCHMARK_REPORT.md</code>), not captured live here.
      </p>
    </div>
  );
}
