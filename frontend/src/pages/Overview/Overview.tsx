/**
 * The workspace's opening view: an operations console that answers, in reading order, "what is
 * the condition of the operation, where is everything, what is in danger, what is the fleet
 * doing about it, and is the planner behaving?"
 *
 * Each instrument is a different picture because each question has a different shape: a polar
 * radar for phasing, a map for ground position and daylight, dials for countdowns, a roster for
 * the fleet, a Gantt for timing against deadlines, a log for sequence. A wall of identical
 * chart cards answered none of those at a glance, which is why the page was rebuilt.
 *
 * Everything is read from the current `SimulationSnapshot` or from the short client-side window
 * `useSnapshotHistory` keeps of past snapshots. No panel computes a planning result.
 */

import { useMemo } from 'react';

import { LineChart, ScatterChart, formatClock } from '../../components/charts/primitives';
import type { ScatterDatum, Series } from '../../components/charts/primitives';
import { useSnapshotHistory } from '../../hooks/useSnapshotHistory';
import { useEngine } from '../../state/EngineContext';
import { useUI } from '../../state/UIContext';
import type { SimulationSnapshot } from '../../types/engine';
import { ConditionBanner } from './console/ConditionBanner';
import { ConsolePanel, LiveMark } from './console/ConsolePanel';
import { EmergencyBoard } from './console/EmergencyBoard';
import { FleetRoster } from './console/FleetRoster';
import { GroundTrack } from './console/GroundTrack';
import { MissionLog } from './console/MissionLog';
import { MissionTimeline } from './console/MissionTimeline';
import { OrbitalRadar } from './console/OrbitalRadar';
import { SubsystemMatrix } from './console/SubsystemMatrix';

const ALGORITHM_TOKEN: Readonly<Record<string, string>> = {
  DIJKSTRA: '--orca-neutral',
  ASTAR: '--orca-primary',
  PARETO: '--orca-mission-active',
  HUNGARIAN: '--orca-warning',
  GREEDY: '--orca-neutral',
};

const COMPLEXITY: Readonly<Record<string, string>> = {
  DIJKSTRA: 'O((V+E) log V)',
  ASTAR: 'O((V+E) log V)',
  PARETO: 'O(L log L · (k+deg))',
  HUNGARIAN: 'O(n³)',
  GREEDY: 'O(mn · min(m,n))',
};

/**
 * The null-snapshot gate. It is a separate component from the body below so that the body can
 * take a non-null snapshot and call hooks unconditionally: an early `return` above a `useMemo`
 * would change the hook order between the first render and the second, which React forbids.
 */
export function OverviewPage() {
  const { snapshot, connectionStatus } = useEngine();

  if (snapshot === null) {
    return (
      <div className="dash-empty" role="status">
        <h2>Waiting for the first snapshot</h2>
        <p>
          The workspace renders engine state and holds nothing of its own, so there is nothing to
          show until a snapshot arrives. Connection status: {connectionStatus}.
        </p>
      </div>
    );
  }

  return <OverviewBody snapshot={snapshot} />;
}

function OverviewBody({ snapshot }: { readonly snapshot: SimulationSnapshot }) {
  const { dataSource, select } = useEngine();
  const { setPage } = useUI();
  const history = useSnapshotHistory(snapshot);

  const loadSeries = useMemo<Series[]>(
    () => [
      { id: 'active', label: 'Open emergencies', token: '--orca-warning', points: history.map((h) => ({ x: h.timeSec, y: h.activeEmergencies })) },
      { id: 'critical', label: 'Critical', token: '--orca-critical', points: history.map((h) => ({ x: h.timeSec, y: h.criticalEmergencies })) },
      { id: 'missions', label: 'Missions in flight', token: '--orca-mission-active', points: history.map((h) => ({ x: h.timeSec, y: h.activeMissions })) },
    ],
    [history],
  );

  const paretoPoints = useMemo<ScatterDatum[]>(
    () =>
      snapshot.missions.flatMap((mission) =>
        mission.paretoFront.map((point) => ({
          x: point.cost.time,
          y: point.cost.fuel,
          label:
            `${mission.id} · ${point.planId}: ${Math.round(point.cost.time)} s, Δv ${(point.cost.fuel * 100).toFixed(1)}% of budget, risk ${point.cost.risk.toFixed(3)}` +
            (point.recommended ? ' (recommended)' : point.dominated ? ' (dominated)' : ''),
          token: point.recommended ? '--orca-success' : point.dominated ? '--orca-neutral' : '--orca-mission-active',
          emphasis: point.recommended,
          // Risk drawn as size: the third objective, readable without a third axis.
          radius: point.dominated ? 3 : 3.5 + point.cost.risk * 9,
        })),
      ),
    [snapshot.missions],
  );

  const algorithmRows = useMemo(() => {
    const byAlgorithm = new Map<string, { latest: number; runs: number; expanded: number | null }>();
    for (const run of snapshot.algorithmRuns) {
      const existing = byAlgorithm.get(run.algorithm);
      if (existing === undefined) {
        byAlgorithm.set(run.algorithm, {
          latest: run.executionTimeMs,
          runs: 1,
          expanded: run.nodesExpanded ?? run.labelsExpanded ?? run.candidatePairsEvaluated ?? null,
        });
      } else {
        byAlgorithm.set(run.algorithm, { ...existing, runs: existing.runs + 1 });
      }
    }
    return [...byAlgorithm.entries()].map(([algorithm, stats]) => ({ algorithm, ...stats }));
  }, [snapshot.algorithmRuns]);

  const slowestRun = Math.max(0, ...algorithmRows.map((row) => row.latest));

  const fronts = snapshot.missions.filter((m) => m.paretoFront.length > 0).length;

  return (
    <div className="cx">
      <ConditionBanner snapshot={snapshot} history={history} dataSource={dataSource} />

      <div className="cx-grid">
        <ConsolePanel
          code="01"
          title="Orbital situation"
          subtitle="Every object by right ascension and altitude, seen from above the North Pole"
          meta={<LiveMark />}
          className="cx-span-5"
          footnote="All orbital planes are overlaid. Each spiral is a transfer in flight, from rescuer to casualty — the angle between them is the phasing the planner must wait out or pay for in Δv."
        >
          <OrbitalRadar snapshot={snapshot} />
        </ConsolePanel>

        <ConsolePanel
          code="02"
          title="Ground track"
          subtitle="Sub-satellite points, night side and the next orbit of each rescuer and casualty"
          meta={<LiveMark />}
          className="cx-span-7"
          footnote="Tracks are predicted from each object's circular orbital elements; simulation time zero is anchored to when this page was opened, so the Sun and Greenwich are placed for a real date."
        >
          <GroundTrack snapshot={snapshot} />
        </ConsolePanel>

        <ConsolePanel
          code="03"
          title="Emergency board"
          subtitle="Open emergencies, soonest deadline first"
          meta={<span className="cx-count">{snapshot.emergencies.filter((e) => !['RESOLVED', 'EXPIRED', 'UNREACHABLE'].includes(e.state)).length} open</span>}
          className="cx-span-5"
        >
          <EmergencyBoard snapshot={snapshot} onSelect={(id) => select({ kind: 'satellite', id })} />
        </ConsolePanel>

        <ConsolePanel
          code="04"
          title="Rescue fleet"
          subtitle="Assignment, Δv reserve and subsystem health per vehicle"
          meta={
            <button type="button" className="cx-link" onClick={() => setPage('mission-control')}>
              Mission Control →
            </button>
          }
          className="cx-span-7"
        >
          <FleetRoster snapshot={snapshot} onSelect={(id) => select({ kind: 'vehicle', id })} />
        </ConsolePanel>

        <ConsolePanel
          code="05"
          title="Mission timeline"
          subtitle="Transfer legs against each emergency's survival deadline"
          meta={<span className="cx-count">−60 min · +90 min</span>}
          className="cx-span-12"
          footnote="The gap between a row's last bar and its diamond is the rescue's margin. A bar ending past its diamond is a rescue that will arrive too late."
        >
          <MissionTimeline snapshot={snapshot} />
        </ConsolePanel>

        <ConsolePanel
          code="06"
          title="Pareto trade-off"
          subtitle="Every plan on every live front — time against Δv spent, size is risk"
          meta={<span className="cx-count">{paretoPoints.length} plans · {fronts} fronts</span>}
          className="cx-span-7 cx-pareto"
          footnote="Green: the plan the operator profile recommended. Violet: other non-dominated plans. Grey: dominated — another plan is at least as good on every objective. No single plan wins on all three."
        >
          {paretoPoints.length > 0 ? (
            <ScatterChart
              data={paretoPoints}
              xLabel="transfer time (s) →"
              yLabel="↑ Δv, % of vehicle budget"
              formatY={(v) => `${Math.round(v * 100)}`}
              formatX={(v) => `${Math.round(v)}`}
            />
          ) : (
            <p className="dash-collecting">No mission currently holds a front.</p>
          )}
        </ConsolePanel>

        <ConsolePanel
          code="07"
          title="Mission log"
          subtitle="Events as the engine reported them, newest first"
          meta={<LiveMark label="Streaming" />}
          className="cx-span-5"
        >
          <MissionLog snapshot={snapshot} />
        </ConsolePanel>

        <ConsolePanel
          code="08"
          title="Load against capacity"
          subtitle="Open and critical emergencies against missions in flight"
          meta={<span className="cx-count">{history.length} samples</span>}
          className="cx-span-7"
        >
          {history.length > 1 ? (
            <LineChart series={loadSeries} xLabel="simulation time" yLabel="count" formatX={formatClock} formatY={(v) => v.toFixed(0)} zeroBased />
          ) : (
            <p className="dash-collecting">Collecting samples.</p>
          )}
        </ConsolePanel>

        <ConsolePanel
          code="09"
          title="Algorithm telemetry"
          subtitle="Most recent run of each search: cost, work done and bound"
          meta={
            <button type="button" className="cx-link" onClick={() => setPage('algorithm-lab')}>
              Algorithm Lab →
            </button>
          }
          className="cx-span-5"
          footnote="A* sits below Dijkstra on the same query because its heuristic is admissible; Pareto search is dearest because it keeps a whole set of labels at every node."
        >
          {algorithmRows.length > 0 ? (
            <table className="cx-table cx-algo">
              <thead>
                <tr>
                  <th scope="col">Search</th>
                  <th scope="col">Cost</th>
                  <th scope="col">Expanded</th>
                  <th scope="col">Bound</th>
                </tr>
              </thead>
              <tbody>
                {algorithmRows.map((row) => {
                  const token = ALGORITHM_TOKEN[row.algorithm] ?? '--orca-primary';
                  return (
                    <tr key={row.algorithm}>
                      <td>
                        <i className="cx-dot" style={{ background: `var(${token})` }} />
                        {row.algorithm === 'ASTAR' ? 'A*' : row.algorithm}
                      </td>
                      <td className="cx-algo__cost">
                        <span className="cx-algo__bar" aria-hidden="true">
                          <i style={{ width: `${slowestRun === 0 ? 0 : (row.latest / slowestRun) * 100}%`, background: `var(${token})` }} />
                        </span>
                        <span className="mono">{row.latest.toFixed(2)} ms</span>
                      </td>
                      <td className="mono">{row.expanded === null ? '—' : Math.round(row.expanded).toLocaleString()}</td>
                      <td className="mono cx-dim">{COMPLEXITY[row.algorithm] ?? '—'}</td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          ) : (
            <p className="dash-collecting">No algorithm has run yet.</p>
          )}
        </ConsolePanel>

        <ConsolePanel
          code="10"
          title="Subsystem health"
          subtitle="Rescue fleet, then every satellite off nominal — the weakest subsystem named underneath"
          className="cx-span-12"
        >
          <SubsystemMatrix snapshot={snapshot} />
        </ConsolePanel>
      </div>
    </div>
  );
}
