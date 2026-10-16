import { formatClock } from '../../../components/charts/primitives';
import type { SimulationSnapshot } from '../../../types/engine';

/**
 * The event stream as a mission log: fixed-width timestamps, a coloured event code, and the
 * engine's own one-line detail, newest at the top. A log is how operations staff read events;
 * a table of the same rows hides the sequence that matters.
 */

const KIND_TONE: Readonly<Record<string, string>> = {
  SATELLITE_FAILURE: '--orca-critical',
  PROPULSION_FAILURE: '--orca-critical',
  COMMUNICATION_LOSS: '--orca-critical',
  FUEL_CRITICAL: '--orca-critical',
  EMERGENCY_ESCALATION: '--orca-warning',
  DEBRIS_ALERT: '--orca-warning',
  MISSION_REPLAN: '--orca-warning',
  MISSION_START: '--orca-mission-active',
  RESCUE_START: '--orca-mission-active',
  RENDEZVOUS: '--orca-primary',
  RESCUE_COMPLETE: '--orca-success',
  EMERGENCY_RESOLVED: '--orca-success',
  MISSION_FAILURE: '--orca-critical',
};

export function MissionLog({ snapshot }: { readonly snapshot: SimulationSnapshot }) {
  const events = snapshot.recentEvents.slice(0, 22);
  return (
    <div className="cx-log" role="log" aria-label="Mission log">
      {events.length === 0 ? <div className="cx-log__line cx-log__line--dim">No events yet.</div> : null}
      {events.map((event) => (
        <div key={event.id} className="cx-log__line">
          <span className="cx-log__time">T+{formatClock(event.timestampSec)}</span>
          <span className="cx-log__kind" style={{ color: `var(${KIND_TONE[event.kind] ?? '--orca-muted'})` }}>
            {event.kind.padEnd(18, ' ')}
          </span>
          <span className="cx-log__detail">{event.detail}</span>
        </div>
      ))}
      <div className="cx-log__line cx-log__cursor" aria-hidden="true">
        <span className="cx-log__time">T+{formatClock(snapshot.simulationTimeSec)}</span>
        <span className="cx-log__caret">▍</span>
      </div>
    </div>
  );
}
