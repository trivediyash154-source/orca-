import { useEngine } from '../state/EngineContext';
import type { DebrisView, PositionView, SatelliteView, VehicleView } from '../types/engine';

function findSatellite(satellites: readonly SatelliteView[], id: string): SatelliteView | undefined {
  return satellites.find((s) => s.id === id);
}
function findVehicle(vehicles: readonly VehicleView[], id: string): VehicleView | undefined {
  return vehicles.find((v) => v.id === id);
}

function distanceKm(a: PositionView, b: PositionView): number {
  return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

/** Phase 12 brief §9/§10: real distances (Euclidean, from already-real reported positions --
 * no new physics computed here, unlike a derived quantity like orbital period would be) to
 * every other real object in the current snapshot, nearest three. A genuinely different,
 * smaller-scale version of the same real "distance has visual meaning" idea
 * `LargeConstellationField.tsx`'s proximity mode uses for the large constellation. */
function nearestObjects(
  origin: PositionView,
  ownId: string,
  satellites: readonly SatelliteView[],
  vehicles: readonly VehicleView[],
  debris: readonly DebrisView[],
): Array<{ id: string; kind: string; distanceKm: number }> {
  const all: Array<{ id: string; kind: string; position: PositionView }> = [
    ...satellites.map((s) => ({ id: s.id, kind: 'Satellite', position: s.position })),
    ...vehicles.map((v) => ({ id: v.id, kind: 'Vehicle', position: v.position })),
    ...debris.map((d) => ({ id: d.id, kind: 'Debris', position: d.position })),
  ].filter((o) => o.id !== ownId);
  return all
    .map((o) => ({ id: o.id, kind: o.kind, distanceKm: distanceKm(origin, o.position) }))
    .sort((a, b) => a.distanceKm - b.distanceKm)
    .slice(0, 3);
}

/** Brief §23/§73/Phase 12 §10: per-object detail for a selected satellite or vehicle --
 * every real field the snapshot reports (orbit elements, health subsystems, real nearby-
 * object distances), `N/A` only for what genuinely is not modelled (brief §10's own exact
 * wording: "Not available from current simulation model"), never fabricated. Mission-level
 * selections are Mission Intelligence's concern (`MissionIntelligence.tsx`), not this
 * component's, so they fall through to the same "click to select" hint rather than silently
 * rendering nothing. */
export function ObjectInspector() {
  const { snapshot, selected } = useEngine();

  if (!selected || !snapshot || (selected.kind !== 'satellite' && selected.kind !== 'vehicle')) {
    return (
      <section className="object-inspector">
        <h2>Selection</h2>
        <p className="muted">Click a satellite or vehicle marker to inspect it.</p>
      </section>
    );
  }

  if (selected.kind === 'satellite') {
    const sat = findSatellite(snapshot.satellites, selected.id);
    if (!sat) return null;
    const nearby = nearestObjects(sat.position, sat.id, snapshot.satellites, snapshot.rescueVehicles, snapshot.debris);
    return (
      <section className="object-inspector">
        <h2>Selected: {sat.id}</h2>
        <dl>
          <dt>Name</dt>
          <dd>{sat.name}</dd>
          <dt>Status</dt>
          <dd>{sat.status}</dd>
          <dt>Altitude</dt>
          <dd>{sat.orbit.altitudeKm.toFixed(1)} km</dd>
          <dt>Inclination</dt>
          <dd>{sat.orbit.inclinationDeg.toFixed(2)}°</dd>
          <dt>RAAN</dt>
          <dd>{sat.orbit.raanDeg.toFixed(2)}°</dd>
          <dt>Band</dt>
          <dd>{sat.orbit.band}</dd>
          <dt>Position (ECI)</dt>
          <dd>
            {sat.position.x.toFixed(0)}, {sat.position.y.toFixed(0)}, {sat.position.z.toFixed(0)} km
          </dd>
          <dt>Fuel</dt>
          <dd>{(sat.fuelFraction * 100).toFixed(0)}%</dd>
          <dt>Power</dt>
          <dd>{(sat.health.power * 100).toFixed(0)}%</dd>
          <dt>Propulsion</dt>
          <dd>{(sat.health.propulsion * 100).toFixed(0)}%</dd>
          <dt>Comms</dt>
          <dd>{(sat.health.comms * 100).toFixed(0)}%</dd>
          <dt>Thermal</dt>
          <dd>{(sat.health.thermal * 100).toFixed(0)}%</dd>
          <dt>Emergency</dt>
          <dd>{sat.activeEmergencyId ?? 'None'}</dd>
          <dt>Semi-major axis, period</dt>
          <dd>Not available from current simulation model</dd>
        </dl>
        <h3 className="object-inspector-subheading">Nearest Objects (real distance)</h3>
        <ul className="object-inspector-nearby">
          {nearby.map((n) => (
            <li key={n.id}>
              {n.id} ({n.kind}) — {n.distanceKm.toFixed(0)} km
            </li>
          ))}
        </ul>
      </section>
    );
  }

  const vehicle = findVehicle(snapshot.rescueVehicles, selected.id);
  if (!vehicle) return null;
  const nearbyVehicle = nearestObjects(
    vehicle.position,
    vehicle.id,
    snapshot.satellites,
    snapshot.rescueVehicles,
    snapshot.debris,
  );
  return (
    <section className="object-inspector">
      <h2>Selected: {vehicle.id}</h2>
      <dl>
        <dt>Name</dt>
        <dd>{vehicle.name}</dd>
        <dt>Status</dt>
        <dd>{vehicle.status}</dd>
        <dt>Altitude</dt>
        <dd>{vehicle.orbit.altitudeKm.toFixed(1)} km</dd>
        <dt>Inclination</dt>
        <dd>{vehicle.orbit.inclinationDeg.toFixed(2)}°</dd>
        <dt>Band</dt>
        <dd>{vehicle.orbit.band}</dd>
        <dt>Position (ECI)</dt>
        <dd>
          {vehicle.position.x.toFixed(0)}, {vehicle.position.y.toFixed(0)}, {vehicle.position.z.toFixed(0)} km
        </dd>
        <dt>Δv remaining</dt>
        <dd>
          {vehicle.deltaVRemainingKms.toFixed(2)} / {vehicle.deltaVBudgetKms.toFixed(2)} km/s
        </dd>
        <dt>Fuel</dt>
        <dd>{(vehicle.fuelFraction * 100).toFixed(0)}%</dd>
        <dt>Power</dt>
        <dd>{(vehicle.health.power * 100).toFixed(0)}%</dd>
        <dt>Propulsion</dt>
        <dd>{(vehicle.health.propulsion * 100).toFixed(0)}%</dd>
        <dt>Mission</dt>
        <dd>{vehicle.activeMissionId ?? 'None'}</dd>
      </dl>
      <h3 className="object-inspector-subheading">Nearest Objects (real distance)</h3>
      <ul className="object-inspector-nearby">
        {nearbyVehicle.map((n) => (
          <li key={n.id}>
            {n.id} ({n.kind}) — {n.distanceKm.toFixed(0)} km
          </li>
        ))}
      </ul>
    </section>
  );
}
