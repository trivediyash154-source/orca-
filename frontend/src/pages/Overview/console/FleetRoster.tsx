import { formatClock } from '../../../components/charts/primitives';
import type { SimulationSnapshot } from '../../../types/engine';

/**
 * The rescue fleet as a roster: what each vehicle is doing, how much Δv it has left, how healthy
 * it is, and when it arrives. The tank is drawn in ten segments because propellant is read as
 * "how many burns are left", not as a percentage to two decimals.
 */

const STATUS_TOKEN: Readonly<Record<string, string>> = {
  IDLE: '--orca-success',
  ASSIGNED: '--orca-warning',
  TRANSFERRING: '--orca-mission-active',
  ON_STATION: '--orca-primary',
  RETURNING: '--orca-primary',
  DISABLED: '--orca-critical',
};

function healthToken(value: number): string {
  if (value >= 0.8) return '--orca-success';
  if (value >= 0.6) return '--orca-warning';
  return '--orca-critical';
}

export function FleetRoster({
  snapshot,
  onSelect,
}: {
  readonly snapshot: SimulationSnapshot;
  readonly onSelect: (vehicleId: string) => void;
}) {
  return (
    <ul className="cx-roster">
      {snapshot.rescueVehicles.map((v) => {
        const fraction = v.deltaVBudgetKms > 0 ? v.deltaVRemainingKms / v.deltaVBudgetKms : 0;
        const segments = Math.round(fraction * 10);
        const mission = snapshot.missions.find((m) => m.id === v.activeMissionId);
        const emergency = mission ? snapshot.emergencies.find((e) => e.id === mission.emergencyId) : undefined;
        const target = emergency ? snapshot.satellites.find((s) => s.id === emergency.subjectId) : undefined;
        const eta = mission?.legs.at(-1)?.arrivalTimeSec ?? null;
        const token = STATUS_TOKEN[v.status] ?? '--orca-neutral';
        const tankToken = fraction < 0.25 ? '--orca-critical' : fraction < 0.5 ? '--orca-warning' : '--orca-primary';

        return (
          <li key={v.id} className="cx-roster__row">
            <button type="button" className="cx-roster__name" onClick={() => onSelect(v.id)}>
              <span>{v.name}</span>
              <small>{v.id}</small>
            </button>
            <span className="cx-chip" style={{ color: `var(${token})`, borderColor: `var(${token})` }}>
              {v.status.replace(/_/g, ' ')}
            </span>
            <div className="cx-tank" aria-label={`Δv ${v.deltaVRemainingKms.toFixed(2)} of ${v.deltaVBudgetKms.toFixed(2)} km/s`}>
              <div className="cx-tank__cells" aria-hidden="true">
                {Array.from({ length: 10 }, (_, i) => (
                  <span
                    key={i}
                    className="cx-tank__cell"
                    style={i < segments ? { background: `var(${tankToken})` } : undefined}
                  />
                ))}
              </div>
              <span className="cx-tank__value">
                {v.deltaVRemainingKms.toFixed(2)}<small>/{v.deltaVBudgetKms.toFixed(2)} km/s</small>
              </span>
            </div>
            <div className="cx-pips" aria-label="Subsystem health">
              {(
                [
                  ['P', 'Power', v.health.power],
                  ['Pr', 'Propulsion', v.health.propulsion],
                  ['C', 'Comms', v.health.comms],
                  ['T', 'Thermal', v.health.thermal],
                ] as const
              ).map(([short, name, value]) => (
                <span key={short} className="cx-pip" title={`${name} ${Math.round(value * 100)}%`} style={{ borderColor: `var(${healthToken(value)})` }}>
                  <i style={{ background: `var(${healthToken(value)})`, opacity: 0.25 + value * 0.75 }} />
                  {short}
                </span>
              ))}
            </div>
            <div className="cx-roster__task">
              {mission && target ? (
                <>
                  <span>→ {target.name}</span>
                  <small>{mission.id}{eta !== null ? ` · ETA T+${formatClock(eta)}` : ''}</small>
                </>
              ) : (
                <span className="cx-roster__standby">Standing by</span>
              )}
            </div>
          </li>
        );
      })}
    </ul>
  );
}
