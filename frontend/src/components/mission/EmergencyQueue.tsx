/**
 * The emergency queue — what needs rescue, how urgently, and whether anything is going.
 *
 * This is the panel that makes ORCA an orbital RESCUE system rather than a satellite viewer.
 * Everything in it is engine state: emergencies come from the C++ `EmergencyGenerator`'s seeded
 * failure schedule, their urgency and deadline are the engine's own, and whether a mission is
 * responding is read from `snapshot.missions` rather than inferred.
 *
 * ## Ordering is the whole point
 *
 * A queue sorted by arrival tells an operator nothing. This one sorts by the engine's own
 * `urgency` (which rises as the survival window closes — `OPTIMIZATION_MODEL.md` §7.1's
 * "priority is dynamic"), then by severity. The object at the top is the one to look at, and
 * that is a claim the engine is making, not one this component invents.
 *
 * ## What is deliberately NOT shown
 *
 * No success probability, and no risk score for an emergency that has no mission. The engine
 * computes neither, and inventing them here to fill the row would be exactly the fabrication
 * `docs/ARCHITECTURE.md` §6.1 forbids.
 */

import { useMemo } from 'react';

import { useEngine } from '../../state/EngineContext';
import type { EmergencyView, MissionView } from '../../types/engine';

/** Severity ordering for the tiebreak, worst first. Mirrors the engine's own enum order. */
const SEVERITY_RANK: Readonly<Record<string, number>> = {
  CRITICAL: 0,
  HIGH: 1,
  MODERATE: 2,
  LOW: 3,
};

/** Which token colours a row's severity chip. */
function severityClass(severity: string): string {
  switch (severity) {
    case 'CRITICAL':
      return 'sev-critical';
    case 'HIGH':
      return 'sev-high';
    case 'MODERATE':
      return 'sev-moderate';
    default:
      return 'sev-low';
  }
}

/** `1:23:45` — the survival window as an operator reads it. */
function formatWindow(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds <= 0) return 'elapsed';
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = Math.floor(seconds % 60);
  const pad = (n: number) => String(n).padStart(2, '0');
  return h > 0 ? `${h}:${pad(m)}:${pad(s)}` : `${m}:${pad(s)}`;
}

/** Human phrasing for the engine's failure kinds; unknown kinds pass through unchanged. */
function describeKind(kind: string): string {
  return kind
    .split('_')
    .map((word) => word.charAt(0) + word.slice(1).toLowerCase())
    .join(' ');
}

export interface EmergencyQueueProps {
  /** Narrows the queue; omitted means every emergency the engine currently holds. */
  readonly maxRows?: number;
}

export function EmergencyQueue({ maxRows }: EmergencyQueueProps) {
  const { snapshot, select, selected } = useEngine();

  const rows = useMemo(() => {
    if (!snapshot) return [];
    // One pass to index missions by the emergency they serve, so the "responding" lookup below
    // is not O(emergencies x missions).
    const missionByEmergency = new Map<string, MissionView>();
    for (const mission of snapshot.missions) {
      if (mission.emergencyId) missionByEmergency.set(mission.emergencyId, mission);
    }

    const ordered = [...snapshot.emergencies].sort((a, b) => {
      // Resolved emergencies sink: they are history, and history has its own screen.
      const aDone = a.state === 'RESOLVED' || a.state === 'EXPIRED';
      const bDone = b.state === 'RESOLVED' || b.state === 'EXPIRED';
      if (aDone !== bDone) return aDone ? 1 : -1;
      if (b.urgency !== a.urgency) return b.urgency - a.urgency;
      return (SEVERITY_RANK[a.severity] ?? 9) - (SEVERITY_RANK[b.severity] ?? 9);
    });

    const limited = maxRows === undefined ? ordered : ordered.slice(0, maxRows);
    return limited.map((emergency: EmergencyView) => ({
      emergency,
      mission: missionByEmergency.get(emergency.id) ?? null,
    }));
  }, [snapshot, maxRows]);

  if (!snapshot) {
    return (
      <section className="emergency-queue" aria-label="Emergency queue">
        <h2>Emergency queue</h2>
        <p className="muted">Waiting for the first engine snapshot.</p>
      </section>
    );
  }

  if (rows.length === 0) {
    return (
      <section className="emergency-queue" aria-label="Emergency queue">
        <h2>Emergency queue</h2>
        <p className="muted">
          No emergencies. The scenario&apos;s failure schedule is seeded and time-based, so
          advance the simulation for failures to occur.
        </p>
      </section>
    );
  }

  const unresolved = rows.filter(
    (r) => r.emergency.state !== 'RESOLVED' && r.emergency.state !== 'EXPIRED',
  ).length;

  return (
    <section className="emergency-queue" aria-label="Emergency queue">
      <h2>
        Emergency queue
        <span className="emergency-queue-count">
          {unresolved} active / {rows.length}
        </span>
      </h2>

      <ul className="emergency-list">
        {rows.map(({ emergency, mission }) => {
          const isSelected =
            selected?.kind === 'satellite' && selected.id === emergency.subjectId;
          const resolved = emergency.state === 'RESOLVED';
          return (
            <li key={emergency.id}>
              <button
                type="button"
                className={`emergency-row${isSelected ? ' is-selected' : ''}${resolved ? ' is-resolved' : ''}`}
                // Selecting the emergency selects its SUBJECT, so the 3D scene, the inspector
                // and this queue all address the same object rather than three ideas of it.
                onClick={() => select({ kind: 'satellite', id: emergency.subjectId })}
                aria-pressed={isSelected}
              >
                <span className={`emergency-severity ${severityClass(emergency.severity)}`}>
                  {emergency.severity}
                </span>

                <span className="emergency-main">
                  <span className="emergency-subject">{emergency.subjectId}</span>
                  <span className="emergency-kind">{describeKind(emergency.kind)}</span>
                </span>

                <span className="emergency-window">
                  <span className="emergency-window-label">
                    {resolved ? 'outcome' : 'window'}
                  </span>
                  <span className="emergency-window-value">
                    {resolved ? 'Rescued' : formatWindow(emergency.remainingWindowSec)}
                  </span>
                </span>

                <span className="emergency-response">
                  {mission === null ? (
                    <span className="emergency-unassigned">Unassigned</span>
                  ) : (
                    <>
                      <span className="emergency-responder">{mission.vehicleId}</span>
                      <span className="emergency-mission-state">{mission.state}</span>
                    </>
                  )}
                </span>
              </button>
            </li>
          );
        })}
      </ul>

      <p className="emergency-queue-note">
        Ordered by the engine&apos;s own urgency, which rises as a survival window closes.
        Severity breaks ties.
      </p>
    </section>
  );
}
