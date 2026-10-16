/**
 * Frame-agreement test: does a satellite actually appear above the place it is over?
 *
 * This is the check that catches a whole class of plausible-looking bugs. A sign error, a
 * missing sidereal rotation, or an axis swap all still produce a globe with satellites around
 * it — they just put every object over the wrong part of the world, which no amount of looking
 * at the screen reliably reveals.
 *
 * The method avoids assuming either side is right. It takes a real satellite, propagates it two
 * independent ways, and requires the answers to agree:
 *
 *   A. the inertial position SGP4 produces, converted to scene coordinates by the renderer's
 *      own path (`propagateInto`);
 *   B. the geodetic sub-satellite point (`propagateState`, which applies the sidereal rotation
 *      through `eciToGeodetic`), converted back to the inertial scene frame by
 *      `geodeticToInertialScene`.
 *
 * Both describe the same direction from Earth's centre. If the Earth rotation convention is
 * wrong, they diverge by the size of the error.
 */

import { describe, expect, it } from 'vitest';
import { gstime } from 'satellite.js';

import { compileElements, propagateInto, propagateState } from '../src/catalog/propagation';
import { earthRotationY, geodeticToInertialScene, geodeticToScene } from '../src/visualization/catalog/earthRotation';
import type { CatalogElement } from '../src/catalog/types';

const ISS: CatalogElement = {
  id: 25544,
  name: 'ISS (ZARYA)',
  l1: '1 25544U 98067A   26250.49846501  .00005306  00000+0  10435-3 0  9991',
  l2: '2 25544  51.6306 252.7093 0004983 115.8922 244.2580 15.49018229584512',
  cls: 'NOMINAL',
  rel: 'NONE',
  reg: 'LEO',
  inc: 51.6306,
  ecc: 0.0004983,
  per: 416,
  apo: 423,
  pmin: 92.96,
};

function unit([x, y, z]: [number, number, number]): [number, number, number] {
  const r = Math.hypot(x, y, z) || 1;
  return [x / r, y / r, z / r];
}

describe('Earth orientation agrees with propagated satellite positions', () => {
  it('puts a satellite over its own sub-satellite point, at several instants', () => {
    const compiled = compileElements([ISS]);
    const satrec = compiled[0]?.satrec;
    expect(satrec).toBeTruthy();
    if (!satrec) return;

    // Sampled across a full revolution so a convention that happens to work at one point in
    // the orbit — or at one time of day — cannot pass.
    for (const minutes of [0, 17, 34, 51, 68, 85, 300, 1440]) {
      const date = new Date(Date.parse('2026-09-07T11:57:47Z') + minutes * 60_000);

      const positions = new Float32Array(3);
      propagateInto(compiled, date, positions, new Uint8Array(1));
      const fromRenderer = unit([positions[0] ?? 0, positions[1] ?? 0, positions[2] ?? 0]);

      const state = propagateState(satrec, date);
      expect(state).not.toBeNull();
      if (!state) continue;
      const fromGeodetic = unit(
        geodeticToInertialScene(state.latitudeDeg, state.longitudeDeg, date),
      );

      // Angle between the two directions. Earth's oblateness means the geodetic normal is not
      // exactly the geocentric radial, which is a genuine ~0.19-degree effect at mid latitudes;
      // anything beyond half a degree is a frame error, not ellipsoid geometry.
      const dot =
        fromRenderer[0] * fromGeodetic[0] +
        fromRenderer[1] * fromGeodetic[1] +
        fromRenderer[2] * fromGeodetic[2];
      const angleDeg = (Math.acos(Math.min(1, Math.max(-1, dot))) * 180) / Math.PI;
      expect(angleDeg, `frames disagree by ${angleDeg.toFixed(3)}° at +${minutes} min`).toBeLessThan(0.5);
    }
  });

  it('rotates the globe opposite to the inertial advance of the prime meridian', () => {
    const date = new Date('2026-09-07T11:57:47Z');
    // The mesh rotation must undo the inertial rotation, or the texture and the point cloud
    // drift apart over a day.
    expect(earthRotationY(date)).toBeCloseTo(-gstime(date), 10);
  });

  it('places longitude 0 on +X and rotates eastward toward +Z in the Earth-fixed frame', () => {
    // Matches the equirectangular texture's own convention (`earthTexture.ts`): u increases
    // eastward, and the unrotated prime meridian faces the scene's +X.
    const greenwich = geodeticToScene(0, 0);
    expect(greenwich[0]).toBeCloseTo(1, 10);
    expect(greenwich[2]).toBeCloseTo(0, 10);

    const east90 = geodeticToScene(0, 90);
    expect(east90[0]).toBeCloseTo(0, 10);
    expect(east90[2]).toBeCloseTo(1, 10);

    const northPole = geodeticToScene(90, 0);
    expect(northPole[1]).toBeCloseTo(1, 10);
  });
});
