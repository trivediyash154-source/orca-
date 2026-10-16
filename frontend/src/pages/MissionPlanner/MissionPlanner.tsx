import { useEngine } from '../../state/EngineContext';
import { formatSimTime } from '../../utils/format';
import type { MissionLegView, MissionView } from '../../types/engine';

/** Phase 12 brief §5: "do not leave core screens as dead placeholders." The engine has no
 * interactive/user-initiated planning command -- planning is automatic, driven by the
 * simulation's own event loop -- so this is not an interactive planner. It IS the one place
 * that shows every dispatched mission's full real route, leg by leg (`MissionIntelligence`
 * only ever shows the single currently-relevant mission's summary; `MissionHistory` lists
 * missions but not their legs) -- real data that already exists in the snapshot and had no
 * other home. */

function legStatusLabel(status: MissionLegView['status']): string {
  switch (status) {
    case 'completed':
      return 'DONE';
    case 'current':
      return 'ACTIVE';
    case 'upcoming':
      return 'PLANNED';
    default:
      return status;
  }
}

function MissionRouteCard({ mission }: { mission: MissionView }) {
  return (
    <div className="planner-mission-card">
      <div className="planner-mission-header">
        <span className="planner-mission-id">{mission.id}</span>
        <span className={`state-badge state-badge-${mission.state.toLowerCase()}`}>{mission.state}</span>
        <span className="planner-mission-meta">
          {mission.vehicleId} → {mission.emergencyId}
        </span>
        {mission.replanCount > 0 && (
          <span className="planner-mission-replans">{mission.replanCount} replan{mission.replanCount === 1 ? '' : 's'}</span>
        )}
      </div>

      {mission.legs.length === 0 ? (
        <p className="muted">This plan has no legs recorded.</p>
      ) : (
        <table className="data-table planner-leg-table">
          <thead>
            <tr>
              <th>Leg</th>
              <th>From</th>
              <th>To</th>
              <th>Departs</th>
              <th>Arrives</th>
              <th>Δv (km/s)</th>
              <th>Status</th>
            </tr>
          </thead>
          <tbody>
            {mission.legs.map((leg, i) => (
              <tr key={`${mission.id}-leg-${i}`} className={leg.status === 'current' ? 'planner-leg-current' : undefined}>
                <td>{i + 1}</td>
                <td>{leg.fromEntityId}</td>
                <td>{leg.toEntityId}</td>
                <td>{formatSimTime(leg.departureTimeSec)}</td>
                <td>{formatSimTime(leg.arrivalTimeSec)}</td>
                <td>{leg.deltaVKms.toFixed(3)}</td>
                <td>
                  <span className={`leg-status-badge leg-status-badge-${leg.status}`}>{legStatusLabel(leg.status)}</span>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </div>
  );
}

export function MissionPlannerPage() {
  const { snapshot } = useEngine();

  if (!snapshot) {
    return <div className="page-placeholder">No snapshot yet.</div>;
  }

  if (snapshot.missions.length === 0) {
    return (
      <div className="mission-planner-page">
        <p className="muted">
          No missions have been dispatched yet this run — a plan appears here the moment ORCA's
          event loop dispatches one. Planning itself is automatic; there is no user-initiated
          "propose a plan" interaction in this build.
        </p>
      </div>
    );
  }

  return (
    <div className="mission-planner-page">
      <p className="muted planner-intro">
        Every dispatched mission's real route, leg by leg — planning is automatic (driven by the
        simulation's event loop), so this is a plan inspector, not an interactive planner.
      </p>
      {[...snapshot.missions].reverse().map((mission) => (
        <MissionRouteCard key={mission.id} mission={mission} />
      ))}
    </div>
  );
}
