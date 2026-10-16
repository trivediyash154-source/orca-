import type { VisualObjectView } from '../types/engine';

const EARTH_RADIUS_KM = 6378.137;
const MU_EARTH = 398600.4418; // km^3/s^2 -- `cpp/core/Constants.hpp`'s kMuEarth

export interface ConstellationObjectPanelProps {
  readonly object: VisualObjectView;
  readonly onClose: () => void;
}

/** Inspector for a SYNTHETIC visual-constellation object (Phase 12 brief §9/§22). Distinct
 * from `ObjectInspector`, which inspects a REAL planning entity from the engine snapshot --
 * these objects are generated visualization data and carry no mission/health/emergency
 * state, so this panel says so plainly instead of showing empty fields.
 *
 * Semi-major axis and orbital period ARE shown, because they are exact closed-form functions
 * of the real altitude the generator produced (`a = R + h`; `T = 2*pi*sqrt(a^3/mu)`, the same
 * two-body relation `cpp/domain/SimplifiedOrbitalModel.cpp` uses) -- derived, not invented.
 * Anything the model genuinely does not have is labelled, never filled in. */
export function ConstellationObjectPanel({ object, onClose }: ConstellationObjectPanelProps) {
  const altitudeKm = object.altitudeKm;
  const semiMajorKm = altitudeKm !== undefined ? altitudeKm + EARTH_RADIUS_KM : undefined;
  const periodMin =
    semiMajorKm !== undefined
      ? (2 * Math.PI * Math.sqrt((semiMajorKm * semiMajorKm * semiMajorKm) / MU_EARTH)) / 60
      : undefined;
  const speedKms =
    semiMajorKm !== undefined ? Math.sqrt(MU_EARTH / semiMajorKm) : undefined;

  const na = 'Unavailable from current simulation model';

  return (
    <section className="object-inspector">
      <div className="inspector-header">
        <h2>Selected: {object.name ?? object.id}</h2>
        <button className="inspector-close" onClick={onClose} aria-label="Clear selection">
          ✕
        </button>
      </div>
      <p className="inspector-badge">SYNTHETIC VISUALIZATION OBJECT</p>
      <dl>
        <dt>ID</dt>
        <dd>{object.id}</dd>
        <dt>Type</dt>
        <dd>Generated constellation object</dd>
        <dt>Status</dt>
        <dd>{object.status ?? na}</dd>
        <dt>Altitude</dt>
        <dd>{altitudeKm !== undefined ? `${altitudeKm.toFixed(0)} km` : na}</dd>
        <dt>Semi-major axis</dt>
        <dd>{semiMajorKm !== undefined ? `${semiMajorKm.toFixed(0)} km` : na}</dd>
        <dt>Orbital period</dt>
        <dd>{periodMin !== undefined ? `${periodMin.toFixed(1)} min` : na}</dd>
        <dt>Orbital speed</dt>
        <dd>{speedKms !== undefined ? `${speedKms.toFixed(3)} km/s` : na}</dd>
        <dt>Inclination</dt>
        <dd>{object.inclinationDeg !== undefined ? `${object.inclinationDeg.toFixed(2)}°` : na}</dd>
        <dt>RAAN</dt>
        <dd>{object.raanDeg !== undefined ? `${object.raanDeg.toFixed(2)}°` : na}</dd>
        <dt>Phase</dt>
        <dd>{object.phaseDeg !== undefined ? `${object.phaseDeg.toFixed(2)}°` : na}</dd>
        <dt>Band</dt>
        <dd>{object.band ?? na}</dd>
        <dt>Position (ECI)</dt>
        <dd>
          {object.position.x.toFixed(0)}, {object.position.y.toFixed(0)}, {object.position.z.toFixed(0)} km
        </dd>
        <dt>Subsystem health</dt>
        <dd>{na}</dd>
        <dt>Emergency / mission</dt>
        <dd>{na}</dd>
      </dl>
      <p className="muted inspector-footnote">
        This object is part of the synthetic visual constellation, not the mission-planning
        graph — it has no simulated health, emergency or mission state. Real planning entities
        (the engine's own satellites and rescue vehicles) show a fuller inspector.
      </p>
    </section>
  );
}
