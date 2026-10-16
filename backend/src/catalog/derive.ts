/**
 * Quantities derived from a TLE's mean elements.
 *
 * Everything here is computed from the element set the provider published — nothing is fetched
 * and nothing is guessed. SATCAT does publish its own rounded `APOGEE`/`PERIGEE`/`PERIOD`
 * columns, and they are deliberately ignored: they are integer-rounded summaries of an older
 * epoch, so using them would mean the altitude shown next to an object disagreed with the orbit
 * actually being propagated for it.
 *
 * ## Mean vs osculating
 *
 * A TLE's mean motion is an SGP4 *mean* element with secular effects absorbed into it, so the
 * semi-major axis recovered below is a mean value under the Keplerian relation, not an
 * osculating one. The difference is on the order of a few kilometres in LEO — immaterial for
 * the regime classification and altitude filtering these values are used for, and material
 * enough that no part of the system uses them for propagation, which is SGP4's job alone.
 */

import type { DerivedOrbit, ElementSet, OrbitRegime } from './types.js';

/** WGS-84 geocentric gravitational constant, km³/s². The same value SGP4 itself uses. */
const MU_KM3_S2 = 398600.4418;

/** WGS-84 equatorial radius, km. Matches the frontend's `SCENE_SCALE_KM` exactly, so an
 * altitude computed here and a scene radius computed there refer to the same surface. */
const EARTH_RADIUS_KM = 6378.137;

const SECONDS_PER_DAY = 86400;

/** Upper altitude of low Earth orbit, km — the conventional 2,000 km boundary. */
const LEO_CEILING_KM = 2000;

/** Geostationary radius: the altitude at which the orbital period is one sidereal day. */
const GEO_ALTITUDE_KM = 35786;

/** Half-width of the band an object must sit inside, at both apses, to count as geostationary. */
const GEO_BAND_KM = 1500;

/** Eccentricity at or above which an orbit is classified highly elliptical regardless of apses. */
const HEO_ECCENTRICITY = 0.25;

/**
 * Semi-major axis from mean motion, by Kepler's third law: n² a³ = μ, with n in radians per
 * second. Returns NaN for a non-positive mean motion, which `deriveOrbit` treats as unknown
 * rather than propagating a nonsense radius into the filters.
 */
export function semiMajorAxisKm(meanMotionRevPerDay: number): number {
  if (!(meanMotionRevPerDay > 0)) return Number.NaN;
  const nRadPerSec = (meanMotionRevPerDay * 2 * Math.PI) / SECONDS_PER_DAY;
  return Math.cbrt(MU_KM3_S2 / (nRadPerSec * nRadPerSec));
}

/**
 * Regime boundaries, applied in a deliberate order. Eccentricity is tested first because a
 * Molniya-class orbit has a LEO-altitude perigee and a beyond-GEO apogee: classifying it by
 * either apsis alone would put it in the wrong band, and it is the eccentricity that actually
 * characterises it.
 */
export function classifyRegime(
  perigeeAltKm: number,
  apogeeAltKm: number,
  eccentricity: number,
): OrbitRegime {
  if (!Number.isFinite(perigeeAltKm) || !Number.isFinite(apogeeAltKm)) return 'UNKNOWN';
  if (eccentricity >= HEO_ECCENTRICITY) return 'HEO';
  if (apogeeAltKm < LEO_CEILING_KM) return 'LEO';
  if (
    Math.abs(perigeeAltKm - GEO_ALTITUDE_KM) <= GEO_BAND_KM &&
    Math.abs(apogeeAltKm - GEO_ALTITUDE_KM) <= GEO_BAND_KM
  ) {
    return 'GEO';
  }
  if (apogeeAltKm >= LEO_CEILING_KM) return 'MEO';
  return 'UNKNOWN';
}

export function deriveOrbit(elements: ElementSet): DerivedOrbit {
  const a = semiMajorAxisKm(elements.meanMotionRevPerDay);
  if (!Number.isFinite(a)) {
    return {
      semiMajorAxisKm: Number.NaN,
      perigeeAltitudeKm: Number.NaN,
      apogeeAltitudeKm: Number.NaN,
      periodMinutes: Number.NaN,
      regime: 'UNKNOWN',
    };
  }
  const e = elements.eccentricity;
  const perigeeAltitudeKm = a * (1 - e) - EARTH_RADIUS_KM;
  const apogeeAltitudeKm = a * (1 + e) - EARTH_RADIUS_KM;
  return {
    semiMajorAxisKm: a,
    perigeeAltitudeKm,
    apogeeAltitudeKm,
    // Minutes per revolution is the reciprocal of revolutions per day, scaled to minutes.
    periodMinutes: 1440 / elements.meanMotionRevPerDay,
    regime: classifyRegime(perigeeAltitudeKm, apogeeAltitudeKm, e),
  };
}

export const ORBIT_CONSTANTS = {
  MU_KM3_S2,
  EARTH_RADIUS_KM,
  LEO_CEILING_KM,
  GEO_ALTITUDE_KM,
  GEO_BAND_KM,
  HEO_ECCENTRICITY,
} as const;
