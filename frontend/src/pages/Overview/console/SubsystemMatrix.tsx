import type { SimulationSnapshot } from '../../../types/engine';

/**
 * Subsystem health as a strip: one row per subsystem, one column per entity, so the fleet and
 * every satellite off nominal read left to right like a rack of status lamps. The bottom row
 * names each entity's weakest subsystem, which is the one a controller would ask about.
 *
 * The colour steps are bands, not a continuous ramp: readings in this scenario sit between 70 %
 * and 100 %, and a smooth ramp over 0–100 % painted all of them the same green.
 */

const SUBSYSTEMS = [
  { key: 'power', label: 'Power' },
  { key: 'propulsion', label: 'Propulsion' },
  { key: 'comms', label: 'Comms' },
  { key: 'thermal', label: 'Thermal' },
] as const;

type SubsystemKey = (typeof SUBSYSTEMS)[number]['key'];

function band(value: number): 'ok' | 'good' | 'watch' | 'warn' | 'fail' {
  if (value >= 0.9) return 'ok';
  if (value >= 0.8) return 'good';
  if (value >= 0.7) return 'watch';
  if (value >= 0.6) return 'warn';
  return 'fail';
}

export function SubsystemMatrix({ snapshot }: { readonly snapshot: SimulationSnapshot }) {
  const entities = [
    ...snapshot.rescueVehicles.map((v) => ({ id: v.id, name: v.name, tag: v.id, health: v.health, vehicle: true })),
    ...snapshot.satellites
      .filter((s) => s.status !== 'NOMINAL')
      .slice(0, 9)
      .map((s) => ({ id: s.id, name: s.name, tag: s.status.toLowerCase(), health: s.health, vehicle: false })),
  ];

  if (entities.length === 0) {
    return <p className="dash-collecting">No entity is reporting subsystem health.</p>;
  }

  const weakest = (health: Record<SubsystemKey, number>) =>
    SUBSYSTEMS.reduce((low, sub) => (health[sub.key] < health[low.key] ? sub : low), SUBSYSTEMS[0]);

  return (
    <div className="cx-matrix" role="table" aria-label="Subsystem health by entity" style={{ ['--cx-cols' as string]: entities.length }}>
      <div className="cx-matrix__row cx-matrix__head" role="row">
        <span className="cx-matrix__corner" role="columnheader">Subsystem</span>
        {entities.map((e) => (
          <span key={e.id} className={`cx-matrix__entity${e.vehicle ? ' cx-matrix__entity--vehicle' : ''}`} role="columnheader" title={e.name}>
            <b>{e.name}</b>
            <small>{e.tag}</small>
          </span>
        ))}
      </div>
      {SUBSYSTEMS.map((sub) => (
        <div key={sub.key} className="cx-matrix__row" role="row">
          <span className="cx-matrix__label" role="rowheader">{sub.label}</span>
          {entities.map((e) => {
            const value = e.health[sub.key];
            return (
              <span
                key={e.id}
                role="cell"
                className={`cx-matrix__cell cx-matrix__cell--${band(value)}`}
                title={`${e.name} · ${sub.label}: ${Math.round(value * 100)}%`}
              >
                {Math.round(value * 100)}
              </span>
            );
          })}
        </div>
      ))}
      <div className="cx-matrix__row cx-matrix__foot" role="row">
        <span className="cx-matrix__label" role="rowheader">Weakest</span>
        {entities.map((e) => {
          const sub = weakest(e.health);
          return (
            <span key={e.id} role="cell" className={`cx-matrix__weak cx-matrix__weak--${band(e.health[sub.key])}`}>
              {sub.label}
            </span>
          );
        })}
      </div>
      <p className="cx-matrix__scale" aria-hidden="true">
        <span className="cx-matrix__cell--fail">&lt;60</span>
        <span className="cx-matrix__cell--warn">60–70</span>
        <span className="cx-matrix__cell--watch">70–80</span>
        <span className="cx-matrix__cell--good">80–90</span>
        <span className="cx-matrix__cell--ok">≥90</span>
        <em>percent of nominal</em>
      </p>
    </div>
  );
}
