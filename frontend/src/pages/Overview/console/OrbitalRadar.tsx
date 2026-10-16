import { useMemo } from 'react';

import type { SimulationSnapshot } from '../../../types/engine';

/**
 * Every object in the scenario on one polar plot, seen from above the North Pole: angle is the
 * object's right ascension, taken from its reported position; distance from centre is its
 * altitude. All orbital planes are overlaid, which is the honest limit of a 2-D view and is said
 * under the plot.
 *
 * Why this rather than another chart: rescue is a phasing problem. A vehicle and its target in
 * similar shells but far apart around the planet must wait for the geometry to come round; the
 * radar shows that gap directly, and each live transfer is drawn as the spiral it traces in this
 * view, from the rescuer's position to the casualty's.
 */

const SIZE = 440;
const C = SIZE / 2;
const ALT_MIN = 450;
const ALT_MAX = 850;
const R_MIN = 74;
const R_MAX = 196;
const RINGS = [500, 600, 700, 800] as const;
/** Room outside the outer ring for bearing labels and for names near the rim. */
const PAD = 30;
/** Ring labels sit on this bearing, between two spokes, where they cross the fewest tracks. */
const RING_LABEL_DEG = 105;

const STATUS_TOKEN: Readonly<Record<string, string>> = {
  NOMINAL: '--orca-primary',
  WARNING: '--orca-warning',
  CRITICAL: '--orca-critical',
  FAILED: '--orca-critical',
  RESCUED: '--orca-success',
  LOST: '--orca-neutral',
};

/** Right ascension in degrees, from a reported ECI position. */
function raOf(p: { readonly x: number; readonly y: number }): number {
  return (Math.atan2(p.y, p.x) * 180) / Math.PI;
}

function radiusFor(altitudeKm: number): number {
  const t = Math.min(1, Math.max(0, (altitudeKm - ALT_MIN) / (ALT_MAX - ALT_MIN)));
  return R_MIN + t * (R_MAX - R_MIN);
}

function polar(phaseDeg: number, radius: number): [number, number] {
  const a = (phaseDeg * Math.PI) / 180;
  return [C + radius * Math.cos(a), C - radius * Math.sin(a)];
}

/** A polar spiral from one (phase, radius) to another, the short way round. */
function spiral(fromDeg: number, fromR: number, toDeg: number, toR: number): string {
  let delta = ((toDeg - fromDeg + 540) % 360) - 180;
  if (Math.abs(delta) < 1) delta = 1;
  const steps = 32;
  let d = '';
  for (let i = 0; i <= steps; i += 1) {
    const t = i / steps;
    const [x, y] = polar(fromDeg + delta * t, fromR + (toR - fromR) * t);
    d += `${i === 0 ? 'M' : 'L'}${x.toFixed(1)} ${y.toFixed(1)}`;
  }
  return d;
}

export function OrbitalRadar({ snapshot }: { readonly snapshot: SimulationSnapshot }) {
  const transfers = useMemo(() => {
    return snapshot.missions
      .filter((m) => m.completedAtSec === null)
      .map((mission) => {
        const vehicle = snapshot.rescueVehicles.find((v) => v.id === mission.vehicleId);
        const emergency = snapshot.emergencies.find((e) => e.id === mission.emergencyId);
        const target = snapshot.satellites.find((s) => s.id === emergency?.subjectId);
        if (!vehicle || !target) return null;
        return {
          id: mission.id,
          d: spiral(
            raOf(vehicle.position),
            radiusFor(vehicle.orbit.altitudeKm),
            raOf(target.position),
            radiusFor(target.orbit.altitudeKm),
          ),
          label: `${mission.id}: ${vehicle.name} → ${target.name}`,
          mission: mission.id,
          vehicle: vehicle.id,
          target: target.name,
          // Signed angle from rescuer to casualty along the direction of motion: positive means
          // the casualty is ahead and the rescuer must catch up by dropping to a faster orbit.
          phaseDeg: ((raOf(target.position) - raOf(vehicle.position) + 540) % 360) - 180,
          altitudeGapKm: target.orbit.altitudeKm - vehicle.orbit.altitudeKm,
        };
      })
      .filter((t): t is NonNullable<typeof t> => t !== null);
  }, [snapshot]);

  const debris = snapshot.debris.filter((d) => d.detected);

  return (
    <figure className="cx-radar">
      <svg viewBox={`${-PAD} ${-PAD} ${SIZE + 2 * PAD} ${SIZE + 2 * PAD}`} role="img" aria-label={`Orbital radar: ${snapshot.satellites.length} satellites and ${snapshot.rescueVehicles.length} vehicles by phase and altitude`}>
        <defs>
          <radialGradient id="cx-radar-earth" cx="40%" cy="38%" r="70%">
            <stop offset="0%" stopColor="var(--orca-primary)" stopOpacity="0.32" />
            <stop offset="100%" stopColor="var(--orca-primary)" stopOpacity="0.04" />
          </radialGradient>
          <linearGradient id="cx-radar-sweep" x1="0" y1="0" x2="1" y2="0">
            <stop offset="0%" stopColor="var(--orca-primary)" stopOpacity="0" />
            <stop offset="100%" stopColor="var(--orca-primary)" stopOpacity="0.28" />
          </linearGradient>
          <marker id="cx-radar-arrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
            <path d="M0 0L10 5L0 10z" fill="var(--orca-mission-active)" />
          </marker>
        </defs>

        {/* Altitude rings and phase spokes. */}
        <g className="cx-radar__grid">
          {RINGS.map((alt) => (
            <g key={alt}>
              <circle cx={C} cy={C} r={radiusFor(alt)} />
              <text
                x={polar(RING_LABEL_DEG, radiusFor(alt))[0]}
                y={polar(RING_LABEL_DEG, radiusFor(alt))[1] - 3}
                textAnchor="middle"
                className="cx-radar__ring-label"
              >
                {alt} km
              </text>
            </g>
          ))}
          {[0, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330].map((deg) => {
            const [x1, y1] = polar(deg, R_MIN - 8);
            const [x2, y2] = polar(deg, R_MAX + 8);
            return <line key={deg} x1={x1} y1={y1} x2={x2} y2={y2} />;
          })}
          {[0, 90, 180, 270].map((deg) => {
            const [x, y] = polar(deg, R_MAX + 22);
            return (
              <text key={deg} x={x} y={y + 4} textAnchor="middle" className="cx-radar__bearing">
                {deg}°
              </text>
            );
          })}
        </g>

        <circle cx={C} cy={C} r={R_MIN - 18} fill="url(#cx-radar-earth)" className="cx-radar__earth" />
        <text x={C} y={C + 4} textAnchor="middle" className="cx-radar__earth-label">EARTH</text>

        {/* The sweep: decoration that says "live", and the only animated element. */}
        <g className="cx-radar__sweep">
          <path d={`M${C} ${C} L${C + R_MAX + 8} ${C} A${R_MAX + 8} ${R_MAX + 8} 0 0 0 ${polar(28, R_MAX + 8).join(' ')} Z`} fill="url(#cx-radar-sweep)" />
        </g>

        {debris.map((d) => {
          const r = Math.hypot(d.position.x, d.position.y, d.position.z) - 6_371;
          const [x, y] = polar(raOf(d.position), radiusFor(r));
          return <circle key={d.id} cx={x} cy={y} r="1.6" className="cx-radar__debris"><title>{`${d.id}: tracked debris`}</title></circle>;
        })}

        {transfers.map((t) => (
          <path key={t.id} d={t.d} className="cx-radar__transfer" markerEnd="url(#cx-radar-arrow)">
            <title>{t.label}</title>
          </path>
        ))}

        {snapshot.satellites.map((s) => {
          const [x, y] = polar(raOf(s.position), radiusFor(s.orbit.altitudeKm));
          const token = STATUS_TOKEN[s.status] ?? '--orca-neutral';
          const inDanger = s.activeEmergencyId !== null;
          return (
            <g key={s.id}>
              {inDanger ? <circle cx={x} cy={y} r="9" className="cx-radar__ping" style={{ stroke: `var(${token})` }} /> : null}
              <circle cx={x} cy={y} r={inDanger ? 4.5 : 3.4} style={{ fill: `var(${token})` }} opacity={inDanger ? 1 : 0.85}>
                <title>{`${s.name} · ${s.status.toLowerCase()} · ${Math.round(s.orbit.altitudeKm)} km`}</title>
              </circle>
              {inDanger ? (
                <text
                  x={x > C + 40 ? x - 9 : x + 9}
                  y={y - 7}
                  textAnchor={x > C + 40 ? 'end' : 'start'}
                  className="cx-radar__tag"
                  style={{ fill: `var(${token})` }}
                >
                  {s.name}
                </text>
              ) : null}
            </g>
          );
        })}

        {snapshot.rescueVehicles.map((v) => {
          const ra = raOf(v.position);
          const [x, y] = polar(ra, radiusFor(v.orbit.altitudeKm));
          // Pointing along the direction of motion (prograde, counter-clockwise here).
          const heading = -ra - 180;
          return (
            <g key={v.id} transform={`translate(${x.toFixed(1)} ${y.toFixed(1)}) rotate(${heading.toFixed(1)})`}>
              <path d="M0 -7L5.5 5H-5.5Z" className="cx-radar__vehicle">
                <title>{`${v.name} · ${v.status.toLowerCase()} · Δv ${v.deltaVRemainingKms.toFixed(2)} km/s`}</title>
              </path>
            </g>
          );
        })}
        {snapshot.rescueVehicles.map((v) => {
          const [x, y] = polar(raOf(v.position), radiusFor(v.orbit.altitudeKm));
          return (
            <text
              key={`${v.id}-label`}
              x={x > C + 40 ? x - 8 : x + 8}
              y={y + 13}
              textAnchor={x > C + 40 ? 'end' : 'start'}
              className="cx-radar__vlabel"
            >
              {v.id}
            </text>
          );
        })}
      </svg>
      <figcaption className="cx-legend">
        <span><i className="cx-dot" style={{ background: 'var(--orca-primary)' }} /> Nominal</span>
        <span><i className="cx-dot" style={{ background: 'var(--orca-warning)' }} /> Warning</span>
        <span><i className="cx-dot" style={{ background: 'var(--orca-critical)' }} /> Critical</span>
        <span><i className="cx-dot" style={{ background: 'var(--orca-success)' }} /> Rescued</span>
        <span><i className="cx-tri" /> Rescue vehicle</span>
        <span><i className="cx-dash" /> Transfer in flight</span>
      </figcaption>

      {transfers.length > 0 ? (
        <table className="cx-table cx-phasing">
          <caption className="dash-vh">Phase and altitude gap between each rescuer and its casualty</caption>
          <thead>
            <tr>
              <th scope="col">Transfer</th>
              <th scope="col">Phase gap</th>
              <th scope="col">Altitude gap</th>
            </tr>
          </thead>
          <tbody>
            {transfers.map((t) => (
              <tr key={t.id}>
                <td>
                  <span className="cx-pass__name">{t.vehicle} → {t.target}</span>
                  <span className="cx-pass__role">{t.mission}</span>
                </td>
                <td className={`mono ${t.phaseDeg >= 0 ? 'cx-phasing__ahead' : 'cx-phasing__behind'}`}>
                  {Math.abs(t.phaseDeg).toFixed(1)}° {t.phaseDeg >= 0 ? 'ahead' : 'behind'}
                </td>
                <td className="mono">
                  {t.altitudeGapKm >= 0 ? '+' : '−'}
                  {Math.abs(t.altitudeGapKm).toFixed(0)} km
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      ) : null}
    </figure>
  );
}
