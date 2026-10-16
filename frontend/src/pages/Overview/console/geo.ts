/**
 * Geometry for the console's map and radar: where a satellite is over the Earth, where the Sun
 * is, and which half of the planet is in darkness.
 *
 * Everything here is a projection of values the snapshot already carries — ECI positions and
 * circular orbital elements — onto a map. Nothing is a planning quantity. The predicted ground
 * track uses the same circular-orbit relation the engine's simplified orbital model uses, and
 * the panel that draws it says so.
 */

import { gstime } from 'satellite.js';

import { positionFor } from '../../../services/demoEngine';
import type { OrbitView, PositionView } from '../../../types/engine';

const DEG = 180 / Math.PI;

export interface LatLon {
  readonly lat: number;
  readonly lon: number;
}

/** Wraps a longitude into [-180, 180). */
export function wrapLon(lon: number): number {
  return ((((lon + 180) % 360) + 360) % 360) - 180;
}

/**
 * The point on the ground directly below an ECI position at `date`: latitude from the
 * position's declination, longitude from its right ascension less Greenwich sidereal time.
 * Spherical, which is the right model for a map at this scale.
 */
export function subPoint(position: PositionView, date: Date): LatLon {
  const r = Math.hypot(position.x, position.y, position.z) || 1;
  const lat = Math.asin(position.z / r) * DEG;
  const ra = Math.atan2(position.y, position.x) * DEG;
  return { lat, lon: wrapLon(ra - gstime(date) * DEG) };
}

/**
 * The subsolar point, to within about a degree: declination from the day of the year, longitude
 * from UTC time of day. Accurate enough to draw where night is; not an ephemeris.
 */
export function subsolarPoint(date: Date): LatLon {
  const start = Date.UTC(date.getUTCFullYear(), 0, 0);
  const dayOfYear = (date.getTime() - start) / 86_400_000;
  const declination = 23.44 * Math.sin(((2 * Math.PI) / 365) * (dayOfYear + 284));
  const utcHours = date.getUTCHours() + date.getUTCMinutes() / 60 + date.getUTCSeconds() / 3600;
  return { lat: declination, lon: wrapLon(-15 * (utcHours - 12)) };
}

/**
 * The night side as one closed polygon in (lon, lat), for an equirectangular map: the
 * terminator latitude at each longitude, closed off along whichever pole is in darkness.
 */
export function nightPolygon(sun: LatLon, steps = 180): LatLon[] {
  // A declination of exactly zero makes tan() vanish; the equinox terminator is a meridian
  // pair, and a hair of offset keeps the formula continuous through it.
  const dec = (Math.abs(sun.lat) < 0.05 ? 0.05 : sun.lat) / DEG;
  const points: LatLon[] = [];
  for (let i = 0; i <= steps; i += 1) {
    const lon = -180 + (360 * i) / steps;
    const hourAngle = (lon - sun.lon) / DEG;
    const lat = Math.atan(-Math.cos(hourAngle) / Math.tan(dec)) * DEG;
    points.push({ lat, lon });
  }
  // The pole opposite the Sun's hemisphere is the dark one.
  const darkPole = sun.lat >= 0 ? -90 : 90;
  return [{ lat: darkPole, lon: -180 }, ...points, { lat: darkPole, lon: 180 }];
}

/**
 * A predicted ground track: sub-satellite points over the next `minutes`, from the orbit's
 * elements, split wherever the track crosses the antimeridian so no segment is drawn across the
 * whole map.
 */
export function groundTrack(
  orbit: OrbitView,
  date: Date,
  minutes: number,
  stepSeconds = 60,
): LatLon[][] {
  const segments: LatLon[][] = [];
  let current: LatLon[] = [];
  let previousLon: number | null = null;

  for (let t = 0; t <= minutes * 60; t += stepSeconds) {
    const position = positionFor(orbit.altitudeKm, orbit.inclinationDeg, orbit.raanDeg, orbit.phaseDeg, t);
    const point = subPoint(position, new Date(date.getTime() + t * 1000));
    if (previousLon !== null && Math.abs(point.lon - previousLon) > 180) {
      segments.push(current);
      current = [];
    }
    current.push(point);
    previousLon = point.lon;
  }
  if (current.length > 0) segments.push(current);
  return segments;
}

const EARTH_RADIUS_KM = 6_371;

/** A ground station: a named point on the surface. */
export interface GroundStation {
  readonly name: string;
  readonly lat: number;
  readonly lon: number;
}

/** VIT Pune, Upper Indira Nagar — the station the console computes passes for. */
export const VIT_PUNE: GroundStation = { name: 'VIT Pune', lat: 18.4636, lon: 73.8682 };

/** Elevation in degrees of an ECI position above a station's local horizon at `date`. */
export function elevationFrom(station: GroundStation, position: PositionView, date: Date): number {
  const lat = station.lat / DEG;
  const angle = station.lon / DEG + gstime(date);
  const up = { x: Math.cos(lat) * Math.cos(angle), y: Math.cos(lat) * Math.sin(angle), z: Math.sin(lat) };
  const rho = {
    x: position.x - up.x * EARTH_RADIUS_KM,
    y: position.y - up.y * EARTH_RADIUS_KM,
    z: position.z - up.z * EARTH_RADIUS_KM,
  };
  const range = Math.hypot(rho.x, rho.y, rho.z) || 1;
  return Math.asin((rho.x * up.x + rho.y * up.y + rho.z * up.z) / range) * DEG;
}

export interface PassPrediction {
  /** Elevation now, degrees. */
  readonly elevationDeg: number;
  /** Seconds until the object next rises above the mask; 0 while it is already above. Null if
   * it does not rise within the search horizon. */
  readonly aosInSec: number | null;
  /** Highest elevation reached during that pass, degrees. */
  readonly peakDeg: number | null;
}

/**
 * The next pass of a circular orbit over a station: sampled every `stepSec` over `horizonSec`,
 * with a 10° elevation mask, which is the usual working floor for a small ground antenna. A
 * one-minute step can miss a grazing pass shorter than a minute; those are not worth a
 * contact plan anyway.
 */
export function nextPass(
  orbit: OrbitView,
  date: Date,
  station: GroundStation = VIT_PUNE,
  maskDeg = 10,
  horizonSec = 12 * 3600,
  stepSec = 60,
): PassPrediction {
  const at = (t: number) =>
    elevationFrom(
      station,
      positionFor(orbit.altitudeKm, orbit.inclinationDeg, orbit.raanDeg, orbit.phaseDeg, t),
      new Date(date.getTime() + t * 1000),
    );

  const elevationDeg = at(0);
  let aosInSec: number | null = null;
  let peakDeg: number | null = null;
  for (let t = 0; t <= horizonSec; t += stepSec) {
    const elevation = t === 0 ? elevationDeg : at(t);
    if (aosInSec === null) {
      if (elevation >= maskDeg) {
        aosInSec = t;
        peakDeg = elevation;
      }
    } else if (elevation >= maskDeg) {
      peakDeg = Math.max(peakDeg ?? elevation, elevation);
    } else {
      break;
    }
  }
  return { elevationDeg, aosInSec, peakDeg };
}

/**
 * Whether an ECI position is in sunlight, by the cylindrical-shadow model: behind the Earth as
 * seen from the Sun and within one Earth radius of the Earth–Sun line. Good to a few seconds at
 * the eclipse boundary for a LEO orbit, which is all a status column needs.
 */
export function isSunlit(position: PositionView, sun: LatLon, date: Date): boolean {
  const dec = sun.lat / DEG;
  const angle = sun.lon / DEG + gstime(date);
  const s = { x: Math.cos(dec) * Math.cos(angle), y: Math.cos(dec) * Math.sin(angle), z: Math.sin(dec) };
  const along = position.x * s.x + position.y * s.y + position.z * s.z;
  if (along >= 0) return true;
  const perp = Math.hypot(position.x - along * s.x, position.y - along * s.y, position.z - along * s.z);
  return perp > EARTH_RADIUS_KM;
}
