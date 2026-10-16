import { useMemo } from 'react';

import { LANDMASSES } from '../../../visualization/earth/continents';
import type { SimulationSnapshot } from '../../../types/engine';
import { formatClock } from '../../../components/charts/primitives';
import { VIT_PUNE, groundTrack, isSunlit, nextPass, nightPolygon, subPoint, subsolarPoint } from './geo';
import type { LatLon } from './geo';

/**
 * A world map with every object at its sub-satellite point, the day/night terminator, and the
 * next orbit's ground track for each rescuer and each casualty.
 *
 * The simulation clock starts at zero, and a map needs a calendar date to place Greenwich and
 * the Sun, so simulation time zero is anchored to the moment this page was loaded. Longitudes
 * then advance correctly with the simulation, at whatever speed it runs.
 */

const W = 720;
const H = 360;
const SIM_EPOCH_MS = Date.now();

const fmtLat = (lat: number) => `${Math.abs(lat).toFixed(1)}°${lat >= 0 ? 'N' : 'S'}`;
const fmtLon = (lon: number) => `${Math.abs(lon).toFixed(1)}°${lon >= 0 ? 'E' : 'W'}`;

const x = (lon: number) => ((lon + 180) / 360) * W;
const y = (lat: number) => ((90 - lat) / 180) * H;

function pathOf(points: readonly LatLon[], close = false): string {
  return (
    points.map((p, i) => `${i === 0 ? 'M' : 'L'}${x(p.lon).toFixed(1)} ${y(p.lat).toFixed(1)}`).join('') +
    (close ? 'Z' : '')
  );
}

const STATUS_TOKEN: Readonly<Record<string, string>> = {
  NOMINAL: '--orca-primary',
  WARNING: '--orca-warning',
  CRITICAL: '--orca-critical',
  FAILED: '--orca-critical',
  RESCUED: '--orca-success',
};

const CLOSED = new Set(['RESOLVED', 'EXPIRED', 'UNREACHABLE']);

export function GroundTrack({ snapshot }: { readonly snapshot: SimulationSnapshot }) {
  const date = useMemo(
    () => new Date(SIM_EPOCH_MS + snapshot.simulationTimeSec * 1000),
    [snapshot.simulationTimeSec],
  );

  const landPaths = useMemo(
    () => LANDMASSES.map((poly) => pathOf(poly.map(([lon, lat]) => ({ lon, lat })), true)),
    [],
  );

  const sun = useMemo(() => subsolarPoint(date), [date]);
  const night = pathOf(nightPolygon(sun), true);

  const { satellites, vehicles, tracks } = useMemo(() => {
    const live = snapshot.missions.filter((m) => m.completedAtSec === null);
    const vehicleIds = new Set(live.map((m) => m.vehicleId));
    const casualtyIds = new Set(
      snapshot.emergencies.filter((e) => !CLOSED.has(e.state)).map((e) => e.subjectId),
    );

    const trackList: { id: string; kind: 'vehicle' | 'casualty'; d: string }[] = [];
    for (const v of snapshot.rescueVehicles) {
      if (!vehicleIds.has(v.id)) continue;
      for (const [i, seg] of groundTrack(v.orbit, date, 95).entries()) {
        trackList.push({ id: `${v.id}-${i}`, kind: 'vehicle', d: pathOf(seg) });
      }
    }
    for (const s of snapshot.satellites) {
      if (!casualtyIds.has(s.id)) continue;
      for (const [i, seg] of groundTrack(s.orbit, date, 95).entries()) {
        trackList.push({ id: `${s.id}-${i}`, kind: 'casualty', d: pathOf(seg) });
      }
    }

    return {
      satellites: snapshot.satellites.map((s) => ({ s, at: subPoint(s.position, date), casualty: casualtyIds.has(s.id) })),
      vehicles: snapshot.rescueVehicles.map((v) => ({ v, at: subPoint(v.position, date) })),
      tracks: trackList,
    };
  }, [snapshot, date]);

  // The objects that matter to the operation -- every rescuer and every casualty -- with where
  // they are, whether they are in sunlight, and when they next clear 10° over the VIT Pune
  // station. A pass is the window in which a ground crew could talk to the spacecraft.
  const passes = useMemo(() => {
    const casualtyIds = new Set(
      snapshot.emergencies.filter((e) => !CLOSED.has(e.state)).map((e) => e.subjectId),
    );
    const rows = [
      ...snapshot.rescueVehicles.map((v) => ({ id: v.id, name: v.name, role: 'Rescuer' as const, orbit: v.orbit, position: v.position })),
      ...snapshot.satellites
        .filter((s) => casualtyIds.has(s.id))
        .map((s) => ({ id: s.id, name: s.name, role: 'Casualty' as const, orbit: s.orbit, position: s.position })),
    ];
    return rows.map((row) => ({
      ...row,
      at: subPoint(row.position, date),
      sunlit: isSunlit(row.position, sun, date),
      pass: nextPass(row.orbit, date),
    }));
  }, [snapshot, date, sun]);

  return (
    <figure className="cx-map">
      <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label="Ground tracks and sub-satellite points on a world map">
        <rect x="0" y="0" width={W} height={H} className="cx-map__ocean" />
        <g className="cx-map__graticule">
          {[-150, -120, -90, -60, -30, 0, 30, 60, 90, 120, 150].map((lon) => (
            <line key={`m${lon}`} x1={x(lon)} x2={x(lon)} y1="0" y2={H} />
          ))}
          {[-60, -30, 0, 30, 60].map((lat) => (
            <line key={`p${lat}`} x1="0" x2={W} y1={y(lat)} y2={y(lat)} className={lat === 0 ? 'cx-map__equator' : undefined} />
          ))}
        </g>
        <g className="cx-map__land">
          {landPaths.map((d, i) => (
            <path key={i} d={d} />
          ))}
        </g>
        <path d={night} className="cx-map__night" />
        <g className="cx-map__sun" transform={`translate(${x(sun.lon).toFixed(1)} ${y(sun.lat).toFixed(1)})`}>
          <circle r="7" />
          <circle r="12" className="cx-map__sun-halo" />
          <title>Subsolar point</title>
        </g>

        {tracks.map((t) => (
          <path key={t.id} d={t.d} className={`cx-map__track cx-map__track--${t.kind}`} />
        ))}

        {satellites.map(({ s, at, casualty }) => {
          const token = STATUS_TOKEN[s.status] ?? '--orca-neutral';
          return (
            <g key={s.id} transform={`translate(${x(at.lon).toFixed(1)} ${y(at.lat).toFixed(1)})`}>
              {casualty ? <circle r="8" className="cx-map__ping" style={{ stroke: `var(${token})` }} /> : null}
              <circle r={casualty ? 3.6 : 2.6} style={{ fill: `var(${token})` }}>
                <title>{`${s.name} · ${at.lat.toFixed(1)}°, ${at.lon.toFixed(1)}°`}</title>
              </circle>
              {casualty ? (
                <text x="8" y="-6" className="cx-map__label" style={{ fill: `var(${token})` }}>{s.name}</text>
              ) : null}
            </g>
          );
        })}

        <g className="cx-map__station" transform={`translate(${x(VIT_PUNE.lon).toFixed(1)} ${y(VIT_PUNE.lat).toFixed(1)})`}>
          <path d="M0 -6L6 0L0 6L-6 0Z" />
          <text x="-8" y="16" textAnchor="end">{VIT_PUNE.name}</text>
          <title>{`${VIT_PUNE.name} ground station · ${fmtLat(VIT_PUNE.lat)} ${fmtLon(VIT_PUNE.lon)}`}</title>
        </g>

        {vehicles.map(({ v, at }) => (
          <g key={v.id} transform={`translate(${x(at.lon).toFixed(1)} ${y(at.lat).toFixed(1)})`}>
            <path d="M0 -6L5 4H-5Z" className="cx-map__vehicle">
              <title>{`${v.name} · ${v.status.toLowerCase()}`}</title>
            </path>
            <text x="7" y="10" className="cx-map__vlabel">{v.id}</text>
          </g>
        ))}
      </svg>
      <figcaption className="cx-legend">
        <span><i className="cx-tri" /> Rescue vehicle</span>
        <span><i className="cx-line cx-line--vehicle" /> Rescuer track, next orbit</span>
        <span><i className="cx-line cx-line--casualty" /> Casualty track, next orbit</span>
        <span><i className="cx-shade" /> Night side</span>
        <span><i className="cx-station" /> {VIT_PUNE.name} ground station</span>
      </figcaption>

      <table className="cx-table cx-pass">
        <caption className="dash-vh">Sub-satellite point, lighting and next pass over {VIT_PUNE.name} for each rescuer and casualty</caption>
        <thead>
          <tr>
            <th scope="col">Object</th>
            <th scope="col">Sub-point</th>
            <th scope="col">Alt</th>
            <th scope="col">Light</th>
            <th scope="col">{VIT_PUNE.name} pass · 10° mask</th>
          </tr>
        </thead>
        <tbody>
          {passes.map((row) => (
            <tr key={row.id} className={row.role === 'Casualty' ? 'cx-pass--casualty' : undefined}>
              <td>
                <span className="cx-pass__name">{row.name}</span>
                <span className="cx-pass__role">{row.role === 'Rescuer' ? row.id : 'casualty'}</span>
              </td>
              <td className="mono">{fmtLat(row.at.lat)} {fmtLon(row.at.lon)}</td>
              <td className="mono">{Math.round(row.orbit.altitudeKm)} km</td>
              <td>
                <span className={`cx-light cx-light--${row.sunlit ? 'day' : 'night'}`}>{row.sunlit ? 'Sunlit' : 'Eclipse'}</span>
              </td>
              <td className="mono">
                {row.pass.aosInSec === 0 ? (
                  <span className="cx-pass__live">IN VIEW · {Math.round(row.pass.elevationDeg)}° el</span>
                ) : row.pass.aosInSec !== null ? (
                  <>
                    AOS in {formatClock(row.pass.aosInSec)}
                    <span className="cx-dim"> · max {Math.round(row.pass.peakDeg ?? 0)}°</span>
                  </>
                ) : (
                  <span className="cx-dim">none in 12 h</span>
                )}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </figure>
  );
}
