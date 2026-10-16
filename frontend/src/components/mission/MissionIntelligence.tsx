import { useEngine } from '../../state/EngineContext';
import { formatMinutes, formatPercent } from '../../utils/format';
import type { MissionView, SimulationSnapshot } from '../../types/engine';

/** Resolves the mission a given selection is "about," per Mission Control's own priority
 * (brief §24): the selected mission directly; else the active mission of a selected
 * satellite/vehicle; else, if exactly one mission is active, that one (so the panel is
 * useful without requiring a click during a demo). Returns `null`, never a guess, when
 * none of these apply. */
function resolveRelevantMission(snapshot: SimulationSnapshot | null): MissionView | null {
  if (!snapshot) return null;
  const active = snapshot.missions.filter((m) => m.state === 'IN_TRANSIT' || m.state === 'RENDEZVOUS');
  if (active.length === 1) return active[0]!;
  if (snapshot.missions.length === 1) return snapshot.missions[0]!;
  return null;
}

/** Picks which mission (if any) `MissionIntelligence` should describe, honoring a direct
 * mission selection, a satellite/vehicle selection with an associated mission, or falling
 * back to the sole active mission when nothing is selected. */
export function useRelevantMission(): MissionView | null {
  const { snapshot, selected } = useEngine();
  if (!snapshot) return null;

  if (selected?.kind === 'mission') {
    return snapshot.missions.find((m) => m.id === selected.id) ?? null;
  }
  if (selected?.kind === 'satellite') {
    return snapshot.missions.find((m) => m.emergencyId && snapshot.emergencies.some((e) => e.id === m.emergencyId && e.subjectId === selected.id)) ?? null;
  }
  if (selected?.kind === 'vehicle') {
    return snapshot.missions.find((m) => m.vehicleId === selected.id) ?? null;
  }
  return resolveRelevantMission(snapshot);
}

function planStatusLabel(state: string): string {
  switch (state) {
    case 'IN_TRANSIT':
    case 'RENDEZVOUS':
      return 'ACTIVE';
    case 'SUCCESS':
      return 'COMPLETE';
    case 'ABORTED':
    case 'FAILED':
      return 'TERMINATED';
    default:
      return state;
  }
}

/** Brief §24-26, §33: mission intelligence, plan status, and the deterministic "why this
 * plan" explanation -- every value real, `N/A` where the engine genuinely does not expose
 * one (never fabricated -- brief §24/§61-62's central rule). */
export function MissionIntelligence() {
  const { snapshot } = useEngine();
  const mission = useRelevantMission();

  if (!mission || !snapshot) {
    return (
      <section className="mission-intelligence">
        <h2>Mission Intelligence</h2>
        <p className="muted">No active mission.</p>
      </section>
    );
  }

  const emergency = snapshot.emergencies.find((e) => e.id === mission.emergencyId);
  const vehicle = snapshot.rescueVehicles.find((v) => v.id === mission.vehicleId);
  const finalLeg = mission.legs[mission.legs.length - 1];
  const etaSec = finalLeg ? Math.max(0, finalLeg.arrivalTimeSec - snapshot.simulationTimeSec) : null;
  const totalDeltaVKms = mission.legs.reduce((sum, leg) => sum + leg.deltaVKms, 0);
  const fuelFraction = vehicle && vehicle.deltaVBudgetKms > 0 ? totalDeltaVKms / vehicle.deltaVBudgetKms : null;
  const recommendedPoint = mission.paretoFront.find((p) => p.recommended);
  const complete = mission.state === 'SUCCESS';

  return (
    <section className="mission-intelligence">
      <h2>Mission Intelligence</h2>

      <div className="mi-headline">
        <span className="mi-subject">{emergency?.subjectId ?? mission.emergencyId}</span>
        <span className={`mi-severity mi-severity-${(emergency?.severity ?? '').toLowerCase()}`}>
          {emergency ? `${emergency.severity} Emergency` : ''}
        </span>
      </div>

      <dl className="mi-fields">
        <dt>Responder</dt>
        <dd>{mission.vehicleId}</dd>

        <dt>ETA</dt>
        <dd>{complete ? 'Arrived' : etaSec !== null ? formatMinutes(etaSec) : 'N/A'}</dd>

        <dt>Fuel Cost</dt>
        <dd>{fuelFraction !== null ? formatPercent(fuelFraction) : 'N/A'}</dd>

        <dt>Risk</dt>
        <dd>{recommendedPoint ? formatPercent(recommendedPoint.cost.risk) : 'N/A'}</dd>

        <dt>Success Probability</dt>
        <dd>N/A — not computed by the engine</dd>
      </dl>

      <div className="mi-plan-status">
        <span className="mi-plan-label">CURRENT PLAN</span>
        <span className="mi-plan-id">{recommendedPoint?.planId ?? mission.id}</span>
        <span className={`mi-plan-badge mi-plan-badge-${planStatusLabel(mission.state).toLowerCase()}`}>
          {planStatusLabel(mission.state)}
        </span>
      </div>

      <div className="mi-explanation">
        <h3>Why This Plan?</h3>
        {mission.explanation ? (
          <pre className="mi-explanation-text">{mission.explanation}</pre>
        ) : (
          <p className="muted">
            No replan has occurred yet — this reflects the mission's original assignment.
          </p>
        )}
      </div>
    </section>
  );
}
