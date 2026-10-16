/**
 * Solar direction, so the day/night terminator on the rendered globe is the real one.
 *
 * A light parked at a fixed corner of the scene produces a terminator that never moves and never
 * matches the date being displayed — the globe looks lit, and lies about where noon is. This is
 * the standard low-precision solar position (Astronomical Almanac's abridged series), accurate
 * to roughly 0.01 degrees over the years around J2000. That is far finer than a terminator drawn
 * across a sphere a few hundred pixels wide, and it costs a handful of trigonometric calls per
 * frame rather than an ephemeris table.
 *
 * The result is a DIRECTION, not a position: the Sun is 23,000 Earth radii away, so what a
 * renderer needs is the unit vector toward it, scaled out to wherever the light is placed.
 */

/** Days since the J2000.0 epoch (2000-01-01 12:00 TT), the argument of the series below. */
function daysSinceJ2000(date: Date): number {
  return date.getTime() / 86_400_000 - 10_957.5;
}

const DEG = Math.PI / 180;

/**
 * Unit vector toward the Sun in the inertial frame, expressed in SCENE axes — the same
 * `sceneX = eciX, sceneY = eciZ, sceneZ = eciY` mapping `coordinates.ts` defines, so it can be
 * used directly as a light position without a second, divergent conversion.
 */
export function sunDirectionScene(date: Date): [number, number, number] {
  const n = daysSinceJ2000(date);

  // Mean longitude and mean anomaly of the Sun, degrees.
  const meanLongitude = 280.46 + 0.9856474 * n;
  const meanAnomaly = (357.528 + 0.9856003 * n) * DEG;

  // Ecliptic longitude, applying the equation of centre — this is the term that makes the
  // terminator correct through the year rather than only at the equinoxes.
  const eclipticLongitude =
    (meanLongitude + 1.915 * Math.sin(meanAnomaly) + 0.02 * Math.sin(2 * meanAnomaly)) * DEG;

  // Obliquity of the ecliptic: Earth's axial tilt, and the reason seasons appear at all.
  const obliquity = (23.439 - 0.0000004 * n) * DEG;

  // Equatorial unit vector from the ecliptic one.
  const eciX = Math.cos(eclipticLongitude);
  const eciY = Math.cos(obliquity) * Math.sin(eclipticLongitude);
  const eciZ = Math.sin(obliquity) * Math.sin(eclipticLongitude);

  // ECI -> scene axes (polar axis becomes "up").
  return [eciX, eciZ, eciY];
}

/** Earth's axial tilt in degrees, at the displayed instant. Used for the visible pole tilt. */
export function obliquityDeg(date: Date): number {
  return 23.439 - 0.0000004 * daysSinceJ2000(date);
}

/**
 * The sub-solar point — where the Sun is directly overhead. Useful as a readout ("local noon is
 * here") and as a check that the lighting and the globe agree.
 */
export function subSolarPoint(date: Date, gmstRad: number): { latDeg: number; lonDeg: number } {
  const [sx, sy, sz] = sunDirectionScene(date);
  // Scene y carries the polar axis, so declination is measured from it.
  const latDeg = (Math.asin(Math.max(-1, Math.min(1, sy))) * 180) / Math.PI;
  // Right ascension in the scene's equatorial plane, then rotated into Earth-fixed longitude
  // by removing the sidereal angle.
  const rightAscension = Math.atan2(sz, sx);
  let lonDeg = ((rightAscension - gmstRad) * 180) / Math.PI;
  lonDeg = ((((lonDeg + 180) % 360) + 360) % 360) - 180;
  return { latDeg, lonDeg };
}
