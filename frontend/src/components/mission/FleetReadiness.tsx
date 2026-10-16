/**
 * Fleet readiness — what can respond right now, and what cannot.
 *
 * The question an operator actually has when an emergency appears is not "how many vehicles do
 * we own" but "which of them can go, and why can't the others". So the summary counts by
 * DISPOSITION (available / committed / degraded) rather than listing a total, and each vehicle
 * row carries the two things that decide whether it can serve a target: remaining delta-v, and
 * subsystem health.
 *
 * Every value is engine state. `deltaVRemainingKms` against `deltaVBudgetKms` is the real
 * propellant margin the planner itself uses as a constraint, and the health fractions are the
 * engine's own subsystem model — not a battery percentage invented for the display.
 */

import { useMemo } from 'react';

import { useEngine } from '../../state/EngineContext';
import type { MissionView, VehicleView } from '../../types/engine';

/** Statuses the engine reports for a vehicle that is not committed to a mission. */
const AVAILABLE_STATUSES = new Set(['IDLE', 'AVAILABLE']);

/**
 * A vehicle is degraded when any subsystem has lost meaningful capability. The threshold is a
 * DISPLAY convention, stated here rather than hidden: the engine reports continuous health
 * fractions and does not itself publish a boolean "degraded", so this component draws the line
 * and says where.
 */
const DEGRADED_BELOW = 0.7;

function worstSubsystem(vehicle: VehicleView): { name: string; value: number } {
  const entries: Array<[string, number]> = [
    ['power', vehicle.health.power],
    ['propulsion', vehicle.health.propulsion],
    ['comms', vehicle.health.comms],
    ['thermal', vehicle.health.thermal],
  ];
  let worst: [string, number] = entries[0] ?? ['power', 1];
  for (const entry of entries) {
    if (entry[1] < worst[1]) worst = entry;
  }
  return { name: worst[0], value: worst[1] };
}

export function FleetReadiness() {
  const { snapshot, select, selected } = useEngine();

  const fleet = useMemo(() => {
    if (!snapshot) return null;

    const missionByVehicle = new Map<string, MissionView>();
    for (const mission of snapshot.missions) {
      // Only a mission still under way commits its vehicle; a completed one releases it.
      if (mission.state !== 'SUCCESS' && mission.state !== 'FAILED' && mission.state !== 'ABORTED') {
        missionByVehicle.set(mission.vehicleId, mission);
      }
    }

    const rows = snapshot.rescueVehicles.map((vehicle) => {
      const mission = missionByVehicle.get(vehicle.id) ?? null;
      const worst = worstSubsystem(vehicle);
      const degraded = worst.value < DEGRADED_BELOW;
      const disposition: 'committed' | 'degraded' | 'available' | 'unavailable' =
        mission !== null
          ? 'committed'
          : degraded
            ? 'degraded'
            : AVAILABLE_STATUSES.has(vehicle.status)
              ? 'available'
              : 'unavailable';
      return { vehicle, mission, worst, disposition };
    });

    // Most useful first: what can go, then what is already going, then what cannot.
    const order = { available: 0, committed: 1, degraded: 2, unavailable: 3 } as const;
    rows.sort((a, b) => {
      if (order[a.disposition] !== order[b.disposition]) {
        return order[a.disposition] - order[b.disposition];
      }
      return b.vehicle.deltaVRemainingKms - a.vehicle.deltaVRemainingKms;
    });

    const counts = { available: 0, committed: 0, degraded: 0, unavailable: 0 };
    for (const row of rows) counts[row.disposition]++;
    return { rows, counts };
  }, [snapshot]);

  if (!snapshot || fleet === null) {
    return (
      <section className="fleet-readiness" aria-label="Fleet readiness">
        <h2>Fleet</h2>
        <p className="muted">Waiting for the first engine snapshot.</p>
      </section>
    );
  }

  if (fleet.rows.length === 0) {
    return (
      <section className="fleet-readiness" aria-label="Fleet readiness">
        <h2>Fleet</h2>
        <p className="muted">This scenario contains no rescue vehicles.</p>
      </section>
    );
  }

  return (
    <section className="fleet-readiness" aria-label="Fleet readiness">
      <h2>
        Fleet
        <span className="fleet-summary">
          <span className="fleet-count fleet-count-available">{fleet.counts.available} ready</span>
          <span className="fleet-count fleet-count-committed">{fleet.counts.committed} committed</span>
          {fleet.counts.degraded > 0 && (
            <span className="fleet-count fleet-count-degraded">{fleet.counts.degraded} degraded</span>
          )}
        </span>
      </h2>

      <ul className="fleet-list">
        {fleet.rows.map(({ vehicle, mission, worst, disposition }) => {
          const isSelected = selected?.kind === 'vehicle' && selected.id === vehicle.id;
          const fuelPct = Math.round(vehicle.fuelFraction * 100);
          return (
            <li key={vehicle.id}>
              <button
                type="button"
                className={`fleet-row${isSelected ? ' is-selected' : ''}`}
                onClick={() => select({ kind: 'vehicle', id: vehicle.id })}
                aria-pressed={isSelected}
              >
                <span className={`fleet-dot fleet-dot-${disposition}`} aria-hidden="true" />

                <span className="fleet-identity">
                  <span className="fleet-name">{vehicle.name}</span>
                  <span className="fleet-disposition">
                    {mission !== null ? `${mission.state} · ${mission.id}` : disposition}
                  </span>
                </span>

                <span className="fleet-metric">
                  <span className="fleet-metric-label">Δv left</span>
                  <span className="fleet-metric-value">
                    {vehicle.deltaVRemainingKms.toFixed(2)}
                    <small> km/s</small>
                  </span>
                </span>

                <span className="fleet-metric">
                  <span className="fleet-metric-label">Propellant</span>
                  <span className="fleet-bar" aria-hidden="true">
                    <span style={{ width: `${Math.max(0, Math.min(100, fuelPct))}%` }} />
                  </span>
                  <span className="fleet-metric-value fleet-metric-small">{fuelPct}%</span>
                </span>

                <span className="fleet-metric">
                  <span className="fleet-metric-label">Weakest system</span>
                  <span
                    className={`fleet-metric-value fleet-metric-small${
                      worst.value < DEGRADED_BELOW ? ' fleet-degraded' : ''
                    }`}
                  >
                    {worst.name} {Math.round(worst.value * 100)}%
                  </span>
                </span>
              </button>
            </li>
          );
        })}
      </ul>

      <p className="fleet-note">
        Δv remaining and subsystem health are engine state. &ldquo;Degraded&rdquo; is a display
        threshold at {Math.round(DEGRADED_BELOW * 100)}% on the weakest subsystem — the engine
        reports continuous health and does not publish that boolean itself.
      </p>
    </section>
  );
}
