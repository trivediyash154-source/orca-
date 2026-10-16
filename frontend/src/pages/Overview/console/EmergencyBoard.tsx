import { formatClock } from '../../../components/charts/primitives';
import type { SimulationSnapshot } from '../../../types/engine';

/**
 * Open emergencies as countdown dials, soonest deadline first.
 *
 * The dial is the share of the survival window still left; the slack line underneath compares
 * the responder's planned arrival with the deadline, which is the number an operator actually
 * acts on — a mission that arrives after the window closes is a failed rescue however good the
 * plan looked.
 */

const SEVERITY_TOKEN: Readonly<Record<string, string>> = {
  CRITICAL: '--orca-critical',
  HIGH: '--orca-warning',
  MODERATE: '--orca-primary',
  LOW: '--orca-success',
};

const KIND_LABEL: Readonly<Record<string, string>> = {
  POWER_FAILURE: 'Power failure',
  PROPULSION_FAILURE: 'Propulsion failure',
  COMMS_LOSS: 'Comms loss',
  FUEL_CRITICAL: 'Fuel critical',
  COLLISION_DAMAGE: 'Collision damage',
  THERMAL: 'Thermal fault',
};

const CLOSED = new Set(['RESOLVED', 'EXPIRED', 'UNREACHABLE']);

function Dial({ fraction, token }: { readonly fraction: number; readonly token: string }) {
  const r = 26;
  const c = 2 * Math.PI * r;
  const f = Math.min(1, Math.max(0, fraction));
  return (
    <svg className="cx-dial" viewBox="0 0 64 64" aria-hidden="true">
      <circle cx="32" cy="32" r={r} className="cx-dial__track" />
      <circle
        cx="32"
        cy="32"
        r={r}
        fill="none"
        stroke={`var(${token})`}
        strokeWidth="5"
        strokeLinecap="round"
        strokeDasharray={`${c * f} ${c}`}
        transform="rotate(-90 32 32)"
      />
      <text x="32" y="36" textAnchor="middle" className="cx-dial__pct">
        {Math.round(f * 100)}%
      </text>
    </svg>
  );
}

export function EmergencyBoard({
  snapshot,
  onSelect,
}: {
  readonly snapshot: SimulationSnapshot;
  readonly onSelect: (satelliteId: string) => void;
}) {
  const open = snapshot.emergencies
    .filter((e) => !CLOSED.has(e.state))
    .sort((a, b) => a.remainingWindowSec - b.remainingWindowSec);

  if (open.length === 0) {
    return (
      <div className="cx-empty">
        <span className="cx-empty__mark" aria-hidden="true">✓</span>
        No open emergencies. Every satellite is nominal or already rescued.
      </div>
    );
  }

  return (
    <ul className="cx-board">
      {open.slice(0, 6).map((e) => {
        const token = SEVERITY_TOKEN[e.severity] ?? '--orca-neutral';
        const subject = snapshot.satellites.find((s) => s.id === e.subjectId);
        const mission = snapshot.missions.find((m) => m.emergencyId === e.id);
        const arrival = mission?.legs.at(-1)?.arrivalTimeSec ?? null;
        const slack = arrival === null ? null : e.deadlineSec - arrival;
        const window = Math.max(1, e.deadlineSec - e.detectedAtSec);

        return (
          <li key={e.id} className="cx-board__item" style={{ borderColor: `var(${token})` }}>
            <Dial fraction={e.remainingWindowSec / window} token={token} />
            <div className="cx-board__main">
              <div className="cx-board__row">
                <button type="button" className="cx-board__subject" onClick={() => onSelect(e.subjectId)}>
                  {subject?.name ?? e.subjectId}
                </button>
                <span className="cx-chip" style={{ color: `var(${token})`, borderColor: `var(${token})` }}>
                  {e.severity}
                </span>
              </div>
              <div className="cx-board__kind">{KIND_LABEL[e.kind] ?? e.kind.replace(/_/g, ' ').toLowerCase()}</div>
              <div className="cx-board__countdown">
                <span className="cx-board__tminus">T−{formatClock(e.remainingWindowSec)}</span>
                <span className="cx-board__deadline">deadline T+{formatClock(e.deadlineSec)}</span>
              </div>
              <div className="cx-board__response">
                {mission ? (
                  <>
                    <span className="cx-board__vehicle">{mission.vehicleId}</span>
                    <span className="cx-board__state">{mission.state.replace(/_/g, ' ').toLowerCase()}</span>
                    {slack !== null ? (
                      <span className={`cx-board__slack${slack < 0 ? ' cx-board__slack--late' : ''}`}>
                        {slack >= 0 ? `arrives ${formatClock(slack)} early` : `arrives ${formatClock(-slack)} late`}
                      </span>
                    ) : null}
                  </>
                ) : (
                  <span className="cx-board__unassigned">Awaiting assignment</span>
                )}
              </div>
            </div>
          </li>
        );
      })}
    </ul>
  );
}
