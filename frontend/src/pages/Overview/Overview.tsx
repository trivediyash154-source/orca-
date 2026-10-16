/**
 * The workspace's opening view: one screen that answers "what is the state of the fleet, what
 * is the system doing about it, and is the planner behaving?" without the reader navigating
 * anywhere.
 *
 * Everything on this page is read from the current `SimulationSnapshot`, or from the short
 * client-side window `useSnapshotHistory` keeps of past snapshots (see that module for why a
 * trend needs one and what it is forbidden from inventing). No panel computes a planning
 * result: the Pareto scatter plots the front the engine returned, the algorithm table reports
 * the counters the engine emitted, and where a value genuinely does not exist the panel says
 * so rather than showing a zero.
 */

import { useMemo } from 'react';

import {
  BarChart,
  ChartPanel,
  DonutChart,
  GaugeRing,
  HeatGrid,
  LineChart,
  ScatterChart,
  StatTile,
  formatClock,
  formatCompact,
} from '../../components/charts/primitives';
import type { BarDatum, HeatCell, ScatterDatum, Series } from '../../components/charts/primitives';
import { useSnapshotHistory } from '../../hooks/useSnapshotHistory';
import { useEngine } from '../../state/EngineContext';
import { useUI } from '../../state/UIContext';
import type { PageId } from '../../state/UIContext';
import type { SimulationSnapshot } from '../../types/engine';

/** Severity -> token, in one place. Used by the queue, the donut and the urgency chart, which
 * must agree: a CRITICAL row that is amber in one panel and red in another is a bug a reader
 * will read as a data difference. */
const SEVERITY_TOKEN: Readonly<Record<string, string>> = {
  CRITICAL: '--orca-critical',
  HIGH: '--orca-warning',
  MODERATE: '--orca-primary',
  LOW: '--orca-success',
};

/** Emergency states that no longer need a responder, per the engine's `EmergencyState`. */
const CLOSED_STATES = new Set(['RESOLVED', 'EXPIRED', 'UNREACHABLE']);

const ALGORITHM_TOKEN: Readonly<Record<string, string>> = {
  DIJKSTRA: '--orca-primary',
  ASTAR: '--orca-mission-active',
  PARETO: '--orca-band-meo',
  HUNGARIAN: '--orca-warning',
  GREEDY: '--orca-neutral',
};

function mean(values: readonly number[]): number {
  if (values.length === 0) return 0;
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

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

  const activeEmergencies = snapshot.emergencies.filter((entry) => !CLOSED_STATES.has(entry.state));
  const criticalCount = activeEmergencies.filter((entry) => entry.severity === 'CRITICAL').length;
  const activeMissions = snapshot.missions.filter((entry) => entry.completedAtSec === null);
  const completedMissions = snapshot.missions.filter((entry) => entry.completedAtSec !== null);
  const standbyVehicles = snapshot.rescueVehicles.filter((v) => v.activeMissionId === null);

  const fleetDeltaVRemaining = snapshot.rescueVehicles.reduce(
    (sum, v) => sum + v.deltaVRemainingKms,
    0,
  );
  const fleetDeltaVBudget = snapshot.rescueVehicles.reduce((sum, v) => sum + v.deltaVBudgetKms, 0);

  const latest = history[history.length - 1];
  const earlier = history.length > 12 ? history[history.length - 12] : undefined;

  /* ------------------------------------------------------------------ trend series --------- */

  const emergencySeries = useMemo<Series[]>(
    () => [
      {
        id: 'active',
        label: 'Active emergencies',
        token: '--orca-warning',
        points: history.map((sample) => ({ x: sample.timeSec, y: sample.activeEmergencies })),
      },
      {
        id: 'critical',
        label: 'Critical',
        token: '--orca-critical',
        points: history.map((sample) => ({ x: sample.timeSec, y: sample.criticalEmergencies })),
      },
      {
        id: 'missions',
        label: 'Missions in flight',
        token: '--orca-mission-active',
        points: history.map((sample) => ({ x: sample.timeSec, y: sample.activeMissions })),
      },
    ],
    [history],
  );

  const deltaVSeries = useMemo<Series[]>(
    () => [
      {
        id: 'remaining',
        label: 'Mean delta-V remaining',
        token: '--orca-primary',
        points: history.map((sample) => ({ x: sample.timeSec, y: sample.deltaVRemainingMean })),
      },
    ],
    [history],
  );

  const latencySeries = useMemo<Series[]>(
    () => [
      {
        id: 'latency',
        label: 'Mean search time',
        token: '--orca-band-meo',
        points: history.map((sample) => ({ x: sample.timeSec, y: sample.searchLatencyMs })),
      },
    ],
    [history],
  );

  /* ------------------------------------------------------------------ distributions -------- */

  /** Altitude histogram over 50 km bins. Computed from the snapshot's own orbit blocks, which
   * is presentation bucketing of reported values rather than a derived orbital quantity. */
  const altitudeBins = useMemo<BarDatum[]>(() => {
    const bins = new Map<number, number>();
    for (const satellite of snapshot.satellites) {
      const bucket = Math.floor(satellite.orbit.altitudeKm / 50) * 50;
      bins.set(bucket, (bins.get(bucket) ?? 0) + 1);
    }
    return [...bins.entries()]
      .sort((a, b) => a[0] - b[0])
      .map(([bucket, count]) => ({
        label: `${bucket}–${bucket + 50}`,
        value: count,
        token: bucket < 600 ? '--orca-band-leo' : bucket < 720 ? '--orca-primary' : '--orca-band-meo',
      }));
  }, [snapshot.satellites]);

  const vehicleBars = useMemo<BarDatum[]>(
    () =>
      snapshot.rescueVehicles.map((vehicle) => ({
        label: vehicle.name,
        value: vehicle.deltaVRemainingKms,
        reference: vehicle.deltaVBudgetKms,
        token:
          vehicle.deltaVRemainingKms / vehicle.deltaVBudgetKms < 0.25
            ? '--orca-critical'
            : vehicle.deltaVRemainingKms / vehicle.deltaVBudgetKms < 0.55
              ? '--orca-warning'
              : '--orca-success',
      })),
    [snapshot.rescueVehicles],
  );

  /** One row per algorithm, carrying its most recent run. Older runs are summarised as a count
   * rather than averaged into the headline figure: a mean over a Dijkstra on 23 nodes and one
   * on 2 would describe neither. */
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

  const algorithmBars = useMemo<BarDatum[]>(
    () =>
      algorithmRows.map((row) => ({
        label: row.algorithm,
        value: row.latest,
        token: ALGORITHM_TOKEN[row.algorithm] ?? '--orca-primary',
        note: `${row.runs} run${row.runs === 1 ? '' : 's'}`,
      })),
    [algorithmRows],
  );

  /** The Pareto scatter draws every point of every mission's current front at once, so the
   * trade-off surface is visible across the whole operation rather than one mission at a time.
   * Dominated points stay on the plot -- removing them would hide what the filter is for. */
  const paretoPoints = useMemo<ScatterDatum[]>(
    () =>
      snapshot.missions.flatMap((mission) =>
        mission.paretoFront.map((point) => ({
          x: point.cost.time,
          y: point.cost.fuel,
          label:
            `${mission.id} · ${point.planId}: ${Math.round(point.cost.time)} s, ` +
            `${point.cost.fuel.toFixed(3)} km/s, risk ${point.cost.risk.toFixed(3)}` +
            (point.recommended ? ' (recommended)' : point.dominated ? ' (dominated)' : ''),
          token: point.recommended
            ? '--orca-success'
            : point.dominated
              ? '--orca-neutral'
              : '--orca-primary',
          emphasis: point.recommended,
          radius: point.dominated ? 3.5 : 5,
        })),
      ),
    [snapshot.missions],
  );

  const healthCells = useMemo<HeatCell[]>(() => {
    const cells: HeatCell[] = [];
    const entities = [
      ...snapshot.rescueVehicles.map((v) => ({ name: v.name, health: v.health })),
      ...snapshot.satellites
        .filter((s) => s.status !== 'NOMINAL')
        .slice(0, 8)
        .map((s) => ({ name: s.name, health: s.health })),
    ];
    for (const entity of entities) {
      cells.push({ row: entity.name, column: 'Power', value: entity.health.power });
      cells.push({ row: entity.name, column: 'Propulsion', value: entity.health.propulsion });
      cells.push({ row: entity.name, column: 'Comms', value: entity.health.comms });
      cells.push({ row: entity.name, column: 'Thermal', value: entity.health.thermal });
    }
    return cells;
  }, [snapshot.rescueVehicles, snapshot.satellites]);

  const healthRows = useMemo(
    () => [...new Set(healthCells.map((cell) => cell.row))],
    [healthCells],
  );

  const graphFeasible = snapshot.graph.edges.filter((edge) => edge.feasible).length;

  const deltaVTrend = history.map((sample) => sample.deltaVRemainingMean);
  const emergencyTrend = history.map((sample) => sample.activeEmergencies);
  const missionTrend = history.map((sample) => sample.completedMissions);

  function go(page: PageId): void {
    setPage(page);
  }

  return (
    <div className="dash">
      <header className="dash-head">
        <div>
          <h2 className="dash-head__title">Operations overview</h2>
          <p className="dash-head__sub">
            {snapshot.scenarioName} · T+{formatClock(snapshot.simulationTimeSec)} ·{' '}
            {snapshot.simulationStatus === 'RUNNING' ? 'running' : 'paused'} at{' '}
            {snapshot.speedMultiplier}×
          </p>
        </div>
        <div className="dash-head__tags">
          <span className={`dash-tag dash-tag--${dataSource}`}>
            {dataSource === 'engine' ? 'C++ engine' : 'Browser model'}
          </span>
          <span className="dash-tag">{snapshot.satellites.length} satellites</span>
          <span className="dash-tag">{snapshot.rescueVehicles.length} vehicles</span>
          <span className="dash-tag">{snapshot.debris.filter((d) => d.detected).length} tracked debris</span>
        </div>
      </header>

      {/* ---------------------------------------------------------------- KPI row ---------- */}
      <div className="dash-stats">
        <StatTile
          label="Active emergencies"
          value={String(activeEmergencies.length)}
          token={criticalCount > 0 ? '--orca-critical' : '--orca-warning'}
          trend={emergencyTrend}
          delta={
            latest && earlier
              ? {
                  direction:
                    latest.activeEmergencies > earlier.activeEmergencies
                      ? 'up'
                      : latest.activeEmergencies < earlier.activeEmergencies
                        ? 'down'
                        : 'flat',
                  text: `${criticalCount} critical now`,
                }
              : undefined
          }
          caption={
            activeEmergencies.length === 0
              ? 'Every subject nominal'
              : `Nearest deadline ${formatClock(
                  Math.min(...activeEmergencies.map((entry) => entry.remainingWindowSec)),
                )} away`
          }
        />
        <StatTile
          label="Missions in flight"
          value={String(activeMissions.length)}
          token="--orca-mission-active"
          trend={missionTrend}
          caption={`${completedMissions.length} completed · ${snapshot.metrics.replansTriggered} replans triggered`}
        />
        <StatTile
          label="Fleet delta-V remaining"
          value={fleetDeltaVRemaining.toFixed(2)}
          unit="km/s"
          token="--orca-primary"
          trend={deltaVTrend}
          caption={`${((fleetDeltaVRemaining / Math.max(fleetDeltaVBudget, 0.001)) * 100).toFixed(0)}% of a ${fleetDeltaVBudget.toFixed(2)} km/s budget`}
        />
        <StatTile
          label="Vehicles on standby"
          value={`${standbyVehicles.length}/${snapshot.rescueVehicles.length}`}
          token={standbyVehicles.length === 0 ? '--orca-critical' : '--orca-success'}
          caption={
            standbyVehicles.length === 0
              ? 'Fleet fully committed — a new emergency has no responder'
              : standbyVehicles.map((v) => v.name).join(', ')
          }
        />
        <StatTile
          label="Mission success rate"
          value={
            snapshot.metrics.missionSuccesses + snapshot.metrics.missionFailures === 0
              ? '—'
              : `${Math.round(
                  (snapshot.metrics.missionSuccesses /
                    (snapshot.metrics.missionSuccesses + snapshot.metrics.missionFailures)) *
                    100,
                )}%`
          }
          token="--orca-success"
          caption={`${snapshot.metrics.missionSuccesses} succeeded · ${snapshot.metrics.missionFailures} failed · ${snapshot.metrics.missionAborts} aborted`}
        />
        <StatTile
          label="Engine ticks"
          value={formatCompact(snapshot.metrics.ticks)}
          token="--orca-neutral"
          caption={`${formatCompact(snapshot.metrics.eventsProcessed)} events processed`}
        />
      </div>

      {/* ---------------------------------------------------------------- charts ----------- */}
      <div className="dash-grid">
        <ChartPanel
          title="Emergency load against response capacity"
          subtitle="Detections, criticals and missions in flight over the retained window"
          meta={<span className="mono">{history.length} samples</span>}
          wide
          footnote="A critical line that rises while the mission line stays flat is the signal that the fleet is saturated — the condition the assignment solver exists to delay."
        >
          {history.length > 1 ? (
            <LineChart
              series={emergencySeries}
              xLabel="simulation time"
              yLabel="count"
              formatX={formatClock}
              formatY={(value) => value.toFixed(0)}
              zeroBased
            />
          ) : (
            <p className="dash-collecting">Collecting samples. The first trend point needs two snapshots.</p>
          )}
        </ChartPanel>

        <ChartPanel
          title="Fleet readiness"
          subtitle="Delta-V remaining against budget, per vehicle"
          footnote="The faint backing bar is each vehicle's full budget. A plane change costs roughly 0.13 km/s per degree at these altitudes, so a vehicle below a quarter budget can reach only its own orbital plane."
        >
          <BarChart data={vehicleBars} unit=" km/s" format={(value) => value.toFixed(3)} />
          <div className="dash-gauges">
            <GaugeRing
              value={fleetDeltaVRemaining / Math.max(fleetDeltaVBudget, 0.001)}
              label="Fleet propellant"
              token="--orca-primary"
              caption={`${fleetDeltaVRemaining.toFixed(2)} / ${fleetDeltaVBudget.toFixed(2)} km/s`}
            />
            <GaugeRing
              value={mean(
                snapshot.rescueVehicles.flatMap((v) => [
                  v.health.power,
                  v.health.propulsion,
                  v.health.comms,
                  v.health.thermal,
                ]),
              )}
              label="Mean subsystem health"
              token="--orca-success"
            />
            <GaugeRing
              value={
                snapshot.rescueVehicles.length === 0
                  ? 0
                  : activeMissions.length / snapshot.rescueVehicles.length
              }
              label="Fleet committed"
              token={standbyVehicles.length === 0 ? '--orca-critical' : '--orca-warning'}
              caption={`${activeMissions.length} of ${snapshot.rescueVehicles.length}`}
            />
          </div>
        </ChartPanel>

        <ChartPanel
          title="Mean delta-V remaining"
          subtitle="Fleet average over the retained window"
          footnote="Monotonically decreasing by construction: there is no refuelling in this scenario, so the slope is the operation's burn rate."
        >
          {history.length > 1 ? (
            <LineChart
              series={deltaVSeries}
              xLabel="simulation time"
              yLabel="km/s"
              formatX={formatClock}
              formatY={(value) => value.toFixed(2)}
              area
            />
          ) : (
            <p className="dash-collecting">Collecting samples.</p>
          )}
        </ChartPanel>

        <ChartPanel
          title="Emergency severity mix"
          subtitle="Active queue by severity band"
        >
          <DonutChart
            slices={['CRITICAL', 'HIGH', 'MODERATE', 'LOW'].map((severity) => ({
              label: severity,
              value: activeEmergencies.filter((entry) => entry.severity === severity).length,
              token: SEVERITY_TOKEN[severity] ?? '--orca-neutral',
            }))}
            centreValue={String(activeEmergencies.length)}
            centreLabel="active"
          />
        </ChartPanel>

        <ChartPanel
          title="Pareto front — time against fuel"
          subtitle="Every plan on every mission's current front"
          meta={<span className="mono">{paretoPoints.length} plans</span>}
          wide
          footnote="Green rings are the plans the weighted profile recommended (time 0.45 / fuel 0.35 / risk 0.20). Grey points are dominated — some other plan is at least as good on every objective. The curve's convexity is the trade-off: a faster transfer costs propellant, and no single plan is best."
        >
          {paretoPoints.length > 0 ? (
            <ScatterChart
              data={paretoPoints}
              xLabel="transfer time (s)"
              yLabel="delta-V (km/s)"
              formatY={(value) => value.toFixed(2)}
              formatX={(value) => `${Math.round(value)}`}
            />
          ) : (
            <p className="dash-collecting">
              No mission currently holds a front. One is captured when a mission is dispatched.
            </p>
          )}
        </ChartPanel>

        <ChartPanel
          title="Search cost by algorithm"
          subtitle="Most recent run of each, in milliseconds"
          footnote="A* should sit below Dijkstra on the same query — that gap is the admissible heuristic doing its job. The multi-objective label-setting search is the expensive one, because it keeps a Pareto set at every node instead of a single best distance."
        >
          {algorithmBars.length > 0 ? (
            <BarChart data={algorithmBars} unit=" ms" format={(value) => value.toFixed(2)} />
          ) : (
            <p className="dash-collecting">No algorithm has run yet in this session.</p>
          )}
        </ChartPanel>

        <ChartPanel
          title="Search latency over time"
          subtitle="Mean execution time across retained runs"
        >
          {history.some((sample) => sample.searchLatencyMs !== null) ? (
            <LineChart
              series={latencySeries}
              xLabel="simulation time"
              yLabel="ms"
              formatX={formatClock}
              formatY={(value) => value.toFixed(1)}
              area
            />
          ) : (
            <p className="dash-collecting">No search has reported a timing yet.</p>
          )}
        </ChartPanel>

        <ChartPanel
          title="Constellation altitude distribution"
          subtitle="Tracked satellites per 50 km shell"
          footnote="Shell spacing is why the problem is hard: between a 500 km and an 800 km orbit the synodic period is about 25.4 hours, so a fuel-optimal departure phase recurs roughly once a day while a survival window lasts tens of minutes."
        >
          <BarChart data={altitudeBins} unit=" sats" format={(value) => value.toFixed(0)} />
        </ChartPanel>

        <ChartPanel
          title="Subsystem health"
          subtitle="Rescue fleet, plus every satellite off nominal"
          wide
          tall
          footnote="Readings are percentages as reported. An em dash means the entity reported no value for that subsystem — never zero, which would read as a dead subsystem."
        >
          {healthRows.length > 0 ? (
            <HeatGrid
              cells={healthCells}
              rows={healthRows}
              columns={['Power', 'Propulsion', 'Comms', 'Thermal']}
              lowLabel="0% — failed"
              highLabel="100% — nominal"
            />
          ) : (
            <p className="dash-collecting">No entity is reporting subsystem health.</p>
          )}
        </ChartPanel>

        <ChartPanel
          title="Mission graph"
          subtitle="Transfer feasibility across the constellation"
          footnote="An edge is infeasible when its delta-V exceeds what any vehicle in the fleet carries; it stays in the graph so the search can report why a route was rejected rather than silently omitting it."
        >
          <div className="dash-kv">
            <div>
              <dt>Nodes</dt>
              <dd className="mono">{snapshot.graph.nodes.length}</dd>
            </div>
            <div>
              <dt>Edges</dt>
              <dd className="mono">{snapshot.graph.edges.length}</dd>
            </div>
            <div>
              <dt>Feasible</dt>
              <dd className="mono">
                {graphFeasible}{' '}
                <span className="dash-kv__pct">
                  ({snapshot.graph.edges.length === 0
                    ? '—'
                    : `${Math.round((graphFeasible / snapshot.graph.edges.length) * 100)}%`}
                  )
                </span>
              </dd>
            </div>
            <div>
              <dt>Mean out-degree</dt>
              <dd className="mono">
                {snapshot.graph.nodes.length === 0
                  ? '—'
                  : (snapshot.graph.edges.length / snapshot.graph.nodes.length).toFixed(1)}
              </dd>
            </div>
          </div>
          <BarChart
            data={[
              {
                label: 'Feasible',
                value: graphFeasible,
                token: '--orca-success',
                reference: snapshot.graph.edges.length,
              },
              {
                label: 'Infeasible',
                value: snapshot.graph.edges.length - graphFeasible,
                token: '--orca-neutral',
                reference: snapshot.graph.edges.length,
              },
            ]}
            format={(value) => value.toFixed(0)}
            unit=" edges"
          />
          <button type="button" className="dash-link" onClick={() => go('orbital-network')}>
            Open the network view →
          </button>
        </ChartPanel>

        <ChartPanel
          title="Emergency queue"
          subtitle="Ordered by remaining window, soonest first"
          wide
          tall
          meta={
            <button type="button" className="dash-link" onClick={() => go('mission-control')}>
              Mission control →
            </button>
          }
        >
          {activeEmergencies.length === 0 ? (
            <p className="dash-collecting">The queue is empty. Every subject is reporting nominal.</p>
          ) : (
            <table className="dash-table">
              <thead>
                <tr>
                  <th scope="col">Emergency</th>
                  <th scope="col">Subject</th>
                  <th scope="col">Kind</th>
                  <th scope="col">Severity</th>
                  <th scope="col">Window left</th>
                  <th scope="col">Urgency</th>
                  <th scope="col">Responder</th>
                </tr>
              </thead>
              <tbody>
                {[...activeEmergencies]
                  .sort((a, b) => a.remainingWindowSec - b.remainingWindowSec)
                  .slice(0, 10)
                  .map((emergency) => {
                    const mission = snapshot.missions.find(
                      (entry) => entry.emergencyId === emergency.id,
                    );
                    const subject = snapshot.satellites.find((s) => s.id === emergency.subjectId);
                    return (
                      <tr key={emergency.id}>
                        <td className="mono">{emergency.id}</td>
                        <td>
                          <button
                            type="button"
                            className="dash-link dash-link--inline"
                            onClick={() => select({ kind: 'satellite', id: emergency.subjectId })}
                          >
                            {subject?.name ?? emergency.subjectId}
                          </button>
                        </td>
                        <td>{emergency.kind.replace(/_/g, ' ').toLowerCase()}</td>
                        <td>
                          <span
                            className="dash-pill"
                            style={{
                              color: `var(${SEVERITY_TOKEN[emergency.severity] ?? '--orca-neutral'})`,
                              borderColor: `var(${SEVERITY_TOKEN[emergency.severity] ?? '--orca-neutral'})`,
                            }}
                          >
                            {emergency.severity}
                          </span>
                        </td>
                        <td className="mono">{formatClock(emergency.remainingWindowSec)}</td>
                        <td>
                          <span className="dash-meter" aria-hidden="true">
                            <span
                              className="dash-meter__fill"
                              style={{
                                width: `${Math.round(emergency.urgency * 100)}%`,
                                background: `var(${SEVERITY_TOKEN[emergency.severity] ?? '--orca-neutral'})`,
                              }}
                            />
                          </span>
                          <span className="mono dash-meter__text">
                            {emergency.urgency.toFixed(2)}
                          </span>
                        </td>
                        <td className="mono">
                          {mission === undefined ? (
                            <span className="dash-unassigned">unassigned</span>
                          ) : (
                            mission.vehicleId
                          )}
                        </td>
                      </tr>
                    );
                  })}
              </tbody>
            </table>
          )}
        </ChartPanel>

        <ChartPanel
          title="Recent events"
          subtitle="Newest first, as reported by the engine"
          tall
          meta={
            <button type="button" className="dash-link" onClick={() => go('mission-history')}>
              Full history →
            </button>
          }
        >
          <ol className="dash-feed">
            {snapshot.recentEvents.slice(0, 14).map((event) => (
              <li key={event.id} className="dash-feed__item">
                <span className="dash-feed__time mono">{formatClock(event.timestampSec)}</span>
                <span className={`dash-feed__kind dash-feed__kind--${event.kind.toLowerCase()}`}>
                  {event.kind.replace(/_/g, ' ').toLowerCase()}
                </span>
                <span className="dash-feed__detail">{event.detail}</span>
              </li>
            ))}
            {snapshot.recentEvents.length === 0 ? (
              <li className="dash-collecting">No events yet.</li>
            ) : null}
          </ol>
        </ChartPanel>

        <ChartPanel
          title="Algorithm telemetry"
          subtitle="Counters from the most recent run of each search"
          wide
          meta={
            <button type="button" className="dash-link" onClick={() => go('algorithm-lab')}>
              Algorithm lab →
            </button>
          }
          footnote="Every column is a counter the implementation increments itself, not an estimate. A null is printed as an em dash because the counter does not apply to that algorithm — a priority queue has no dominance comparisons, and a label-setting search has no single relaxation count."
        >
          <table className="dash-table">
            <thead>
              <tr>
                <th scope="col">Algorithm</th>
                <th scope="col">Last run</th>
                <th scope="col">Expanded</th>
                <th scope="col">Runs retained</th>
                <th scope="col">Complexity</th>
              </tr>
            </thead>
            <tbody>
              {algorithmRows.length === 0 ? (
                <tr>
                  <td colSpan={5} className="dash-collecting">
                    No runs retained.
                  </td>
                </tr>
              ) : (
                algorithmRows.map((row) => (
                  <tr key={row.algorithm}>
                    <td>
                      <span
                        className="dash-swatch"
                        style={{ background: `var(${ALGORITHM_TOKEN[row.algorithm] ?? '--orca-primary'})` }}
                      />
                      {row.algorithm}
                    </td>
                    <td className="mono">{row.latest.toFixed(3)} ms</td>
                    <td className="mono">{row.expanded === null ? '—' : formatCompact(row.expanded)}</td>
                    <td className="mono">{row.runs}</td>
                    <td className="mono dash-dim">
                      {row.algorithm === 'DIJKSTRA'
                        ? 'O((V+E) log V)'
                        : row.algorithm === 'ASTAR'
                          ? 'O((V+E) log V)'
                          : row.algorithm === 'PARETO'
                            ? 'O(V · L log L)'
                            : row.algorithm === 'HUNGARIAN'
                              ? 'O(n³)'
                              : 'O(n log n)'}
                    </td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </ChartPanel>
      </div>
    </div>
  );
}
