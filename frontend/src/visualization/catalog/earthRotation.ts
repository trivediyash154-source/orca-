/**
 * Earth's orientation in the inertial scene frame.
 *
 * ## The problem this solves
 *
 * Catalogue positions from SGP4 are inertial (TEME, treated as ECI). Earth's surface is not: it
 * rotates underneath them. If the globe is drawn at a fixed orientation, or spun at an arbitrary
 * "looks about right" rate, then every satellite is over the wrong country — the picture stays
 * pretty and becomes untrue. The rotation below is the real one, derived from Greenwich Mean
 * Sidereal Time for the instant being displayed, so a satellite rendered above a coastline is
 * genuinely above that coastline.
 *
 * ## Deriving the sign
 *
 * Three ingredients, each checked against the code that establishes it rather than assumed:
 *
 *   1. **The texture.** `earth/earthTexture.ts` is equirectangular with `u = 0` at longitude
 *      -180 and `u` increasing eastward.
 *   2. **The geometry.** Three.js `SphereGeometry` places `u = 0` at `phi = 0`, whose vertex is
 *      `x = -r·sin(theta)`, `z = 0` — the -X axis. So `u = 0.5`, longitude 0, lands on **+X**
 *      when the mesh is unrotated: the prime meridian starts pointing at the scene's +X.
 *   3. **The frame.** `coordinates.ts` maps `sceneX = eciX` and `sceneZ = eciY`. Scene +X is
 *      therefore the ECI x-axis — the vernal equinox — and Earth's prime meridian must sit at
 *      an angle of GMST from it, measured toward ECI +y, which is scene +Z.
 *
 * A Three.js rotation of `a` about +Y carries a local point `(r, 0)` in the (x, z) plane to
 * `(r·cos a, -r·sin a)`. We need it at `(r·cos G, +r·sin G)`, so `a = -G`.
 *
 * `earthRotation.test.ts` verifies this end to end rather than trusting the derivation: it
 * propagates a real satellite, takes its geodetic sub-satellite point, and checks that rotating
 * that surface point by this function's angle reproduces the satellite's own scene direction.
 */

import { gstime } from 'satellite.js';

/**
 * The mesh rotation, in radians about the scene's +Y axis, that puts Earth's prime meridian
 * where it truly is at `date`.
 */
export function earthRotationY(date: Date): number {
  return -gstime(date);
}

/**
 * Where a point on Earth's surface appears in scene coordinates at `date` — the forward
 * direction of the same transform, used by ground-track and sub-satellite markers so they
 * cannot drift out of agreement with the globe.
 *
 * Spherical, not ellipsoidal: this places a marker on the rendered sphere, which is itself a
 * sphere of radius 1. Geodetic latitude is used as supplied, so a marker sits where the
 * equirectangular texture draws that latitude.
 */
export function geodeticToScene(
  latitudeDeg: number,
  longitudeDeg: number,
  radius = 1,
): [number, number, number] {
  const lat = (latitudeDeg * Math.PI) / 180;
  const lon = (longitudeDeg * Math.PI) / 180;
  // Earth-fixed: longitude 0 on +X, increasing eastward toward +Z, pole on +Y.
  const cosLat = Math.cos(lat);
  const xFixed = cosLat * Math.cos(lon);
  const zFixed = cosLat * Math.sin(lon);
  const yFixed = Math.sin(lat);
  return [xFixed * radius, yFixed * radius, zFixed * radius];
}

/**
 * The same surface point expressed in the inertial scene frame at `date` — Earth-fixed rotated
 * forward by GMST. This is what a satellite's own scene position should agree with, and the
 * test asserts exactly that.
 */
export function geodeticToInertialScene(
  latitudeDeg: number,
  longitudeDeg: number,
  date: Date,
  radius = 1,
): [number, number, number] {
  const [xf, yf, zf] = geodeticToScene(latitudeDeg, longitudeDeg, radius);
  const g = gstime(date);
  // Rotate the Earth-fixed point by +GMST about the scene's Y, into the inertial frame. This is
  // the inverse of `earthRotationY`'s mesh rotation, which is what makes the two consistent.
  return [xf * Math.cos(g) - zf * Math.sin(g), yf, xf * Math.sin(g) + zf * Math.cos(g)];
}
