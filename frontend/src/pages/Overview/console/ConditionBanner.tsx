import { Sparkline, formatClock } from '../../../components/charts/primitives';
import type { HistorySample } from '../../../hooks/useSnapshotHistory';
import type { SimulationSnapshot } from '../../../types/engine';

/**
 * The console's headline: one condition word for the whole operation, the reason for it, and
 * the five readouts an operator checks first.
 *
 * The condition is a rule over snapshot values, stated in `conditionOf` so it can be read and
 * argued with, not a score: RED when any emergency is critical or one is waiting with no free
 * vehicle; AMBER when one is high-severity or most of the fleet is committed; GREEN otherwise.
 */

type Condition = 'GREEN' | 'AMBER' | 'RED';

const CLOSED = new Set(['RESOLVED', 'EXPIRED', 'UNREACHABLE']);

function conditionOf(snapshot: SimulationSnapshot): { level: Condition; reason: string } {
  const open = snapshot.emergencies.filter((e) => !CLOSED.has(e.state));
  const critical = open.filter((e) => e.severity === 'CRITICAL');
  const unassigned = open.filter((e) => e.state === 'PENDING');
  const idle = snapshot.rescueVehicles.filter((v) => v.activeMissionId === null).length;
  const committed = snapshot.rescueVehicles.length === 0 ? 0 : 1 - idle / snapshot.rescueVehicles.length;

  if (critical.length > 0) {
    return {
      level: 'RED',
      reason: `${critical.length} critical emergenc${critical.length === 1 ? 'y' : 'ies'} — nearest deadline ${formatClock(
        Math.min(...critical.map((e) => e.remainingWindowSec)),
      )} away`,
    };
  }
  if (unassigned.length > 0 && idle === 0) {
    return { level: 'RED', reason: `${unassigned.length} emergency waiting and no vehicle free to answer it` };
  }
  if (open.some((e) => e.severity === 'HIGH')) {
    return { level: 'AMBER', reason: 'High-severity emergency in progress — responder assigned' };
  }
  if (committed >= 0.75) {
    return { level: 'AMBER', reason: `${Math.round(committed * 100)}% of the fleet committed — little reserve for a new failure` };
  }
  return {
    level: 'GREEN',
    reason: open.length === 0 ? 'No open emergencies' : `${open.length} emergencies handled with reserve capacity`,
  };
}

interface ReadoutProps {
  readonly label: string;
  readonly value: string;
  readonly unit?: string;
  readonly trend?: readonly number[];
  readonly tone?: string;
}

function Readout({ label, value, unit, trend, tone = '--orca-primary' }: ReadoutProps) {
  return (
    <div className="cx-readout">
      <span className="cx-readout__label">{label}</span>
      <span className="cx-readout__value" style={{ color: `var(${tone})` }}>
        {value}
        {unit ? <small>{unit}</small> : null}
      </span>
      {trend && trend.length > 1 ? (
        <Sparkline values={trend} token={tone} ariaLabel={`${label} trend`} />
      ) : (
        <span className="cx-readout__spacer" />
      )}
    </div>
  );
}

export function ConditionBanner({
  snapshot,
  history,
  dataSource,
}: {
  readonly snapshot: SimulationSnapshot;
  readonly history: readonly HistorySample[];
  readonly dataSource: 'engine' | 'demo';
}) {
  const { level, reason } = conditionOf(snapshot);
  const open = snapshot.emergencies.filter((e) => !CLOSED.has(e.state));
  const inFlight = snapshot.missions.filter((m) => m.completedAtSec === null).length;
  const idle = snapshot.rescueVehicles.filter((v) => v.activeMissionId === null).length;
  const deltaV = snapshot.rescueVehicles.reduce((sum, v) => sum + v.deltaVRemainingKms, 0);
  const budget = snapshot.rescueVehicles.reduce((sum, v) => sum + v.deltaVBudgetKms, 0);
  const done = snapshot.metrics.missionSuccesses + snapshot.metrics.missionFailures;

  return (
    <section className={`cx-condition cx-condition--${level.toLowerCase()}`} aria-label="Operational condition">
      <div className="cx-condition__state">
        <span className="cx-condition__eyebrow">Operational condition</span>
        <span className="cx-condition__level" role="status">
          <span className="cx-condition__lamp" aria-hidden="true" />
          {level}
        </span>
        <span className="cx-condition__reason">{reason}</span>
      </div>

      <div className="cx-condition__clock">
        <span className="cx-condition__eyebrow">Mission elapsed time</span>
        <span className="cx-condition__met">T+{formatClock(snapshot.simulationTimeSec)}</span>
        <span className="cx-condition__sub">
          {snapshot.simulationStatus === 'RUNNING' ? 'Running' : 'Paused'} · {snapshot.speedMultiplier}× ·{' '}
          {dataSource === 'engine' ? 'C++ engine' : 'browser model'}
        </span>
      </div>

      <div className="cx-condition__readouts">
        <Readout
          label="Emergencies"
          value={String(open.length)}
          trend={history.map((h) => h.activeEmergencies)}
          tone={open.some((e) => e.severity === 'CRITICAL') ? '--orca-critical' : '--orca-warning'}
        />
        <Readout
          label="In flight"
          value={String(inFlight)}
          trend={history.map((h) => h.activeMissions)}
          tone="--orca-mission-active"
        />
        <Readout
          label="Fleet Δv km/s"
          value={deltaV.toFixed(1)}
          trend={history.map((h) => h.deltaVRemainingMean)}
          tone="--orca-primary"
        />
        <Readout
          label="Vehicles free"
          value={`${idle}/${snapshot.rescueVehicles.length}`}
          tone={idle === 0 ? '--orca-critical' : '--orca-success'}
        />
        <Readout
          label="Rescued"
          value={done === 0 ? '—' : `${snapshot.metrics.missionSuccesses}`}
          unit={done === 0 ? undefined : ` / ${done}`}
          trend={history.map((h) => h.completedMissions)}
          tone="--orca-success"
        />
        <Readout
          label="Δv reserve"
          value={budget === 0 ? '—' : `${Math.round((deltaV / budget) * 100)}`}
          unit={budget === 0 ? undefined : '%'}
          tone="--orca-primary"
        />
      </div>
    </section>
  );
}
