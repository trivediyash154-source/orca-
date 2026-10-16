/**
 * SGP4 propagation tests.
 *
 * These assert against PHYSICS, not against a recorded snapshot of our own output. A golden-file
 * test would pass just as happily if the axis mapping were wrong, so instead: the ISS must come
 * out at its real altitude, its speed must be orbital, its ground track must stay within its
 * inclination, and a full period must return it to where it started. Those are checks a
 * coordinate-frame mistake cannot survive.
 *
 * The element set is a real one, served by CelesTrak for NORAD 25544.
 */

import { describe, expect, it } from 'vitest';

import {
  compileElements,
  PropagationFlag,
  propagateInto,
  propagateState,
  traceOrbit,
} from '../src/catalog/propagation';
import { SCENE_SCALE_KM } from '../src/visualization/coordinates';
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

/** The element-set epoch itself — where SGP4 is most accurate. */
const EPOCH = new Date('2026-09-07T11:57:47Z');

describe('SGP4 compilation', () => {
  it('compiles a real element set without error', () => {
    const [compiled] = compileElements([ISS]);
    expect(compiled?.error).toBeNull();
    expect(compiled?.satrec).not.toBeNull();
    expect(compiled?.id).toBe(25544);
  });

  it('reports a malformed element set instead of throwing or silently succeeding', () => {
    const [compiled] = compileElements([{ ...ISS, l1: 'not a tle', l2: 'also not a tle' }]);
    expect(compiled?.satrec === null || compiled?.error !== null).toBe(true);
  });
});

describe('propagated state is physically correct', () => {
  it('places the ISS at its real altitude and orbital speed', () => {
    const [compiled] = compileElements([ISS]);
    const satrec = compiled?.satrec;
    expect(satrec).toBeTruthy();
    if (!satrec) return;

    const state = propagateState(satrec, EPOCH);
    expect(state).not.toBeNull();
    if (!state) return;

    // The ISS orbits near 420 km. A wrong frame or a unit slip lands nowhere near this band.
    expect(state.altitudeKm).toBeGreaterThan(380);
    expect(state.altitudeKm).toBeLessThan(460);

    // Circular orbital speed at that radius is ~7.66 km/s.
    expect(state.velocityKmS).not.toBeNull();
    expect(state.velocityKmS ?? 0).toBeGreaterThan(7.5);
    expect(state.velocityKmS ?? 0).toBeLessThan(7.8);

    // Geocentric radius = Earth radius + altitude.
    const r = Math.hypot(state.positionEciKm.x, state.positionEciKm.y, state.positionEciKm.z);
    expect(r).toBeGreaterThan(6700);
    expect(r).toBeLessThan(6850);
  });

  it('never puts the ground track outside the orbit inclination', () => {
    const [compiled] = compileElements([ISS]);
    const satrec = compiled?.satrec;
    if (!satrec) return;
    // A satellite at 51.6306 degrees inclination reaches that GEOCENTRIC latitude — but
    // `eciToGeodetic` returns GEODETIC latitude, measured against the WGS-84 ellipsoid's local
    // normal rather than the geocentre. Because Earth is oblate, geodetic exceeds geocentric
    // everywhere off the equator and poles, by ~0.19 degrees at this latitude, so the real
    // ground track legitimately touches ~51.76. The bound below allows exactly that and no
    // more: a naive spherical conversion would cap at 51.63, and a latitude/longitude swap
    // would blow straight through it.
    for (let minute = 0; minute < 95; minute += 5) {
      const state = propagateState(satrec, new Date(EPOCH.getTime() + minute * 60_000));
      expect(state).not.toBeNull();
      if (!state) continue;
      expect(Math.abs(state.latitudeDeg)).toBeLessThanOrEqual(51.9);
      expect(Math.abs(state.latitudeDeg)).toBeGreaterThanOrEqual(0);
      expect(state.longitudeDeg).toBeGreaterThan(-180);
      expect(state.longitudeDeg).toBeLessThanOrEqual(180);
    }
  });

  it('returns the satellite to its starting point after one full period', () => {
    const [compiled] = compileElements([ISS]);
    const satrec = compiled?.satrec;
    if (!satrec) return;
    const start = propagateState(satrec, EPOCH);
    const afterOnePeriod = propagateState(satrec, new Date(EPOCH.getTime() + ISS.pmin * 60_000));
    expect(start).not.toBeNull();
    expect(afterOnePeriod).not.toBeNull();
    if (!start || !afterOnePeriod) return;
    const separation = Math.hypot(
      start.positionEciKm.x - afterOnePeriod.positionEciKm.x,
      start.positionEciKm.y - afterOnePeriod.positionEciKm.y,
      start.positionEciKm.z - afterOnePeriod.positionEciKm.z,
    );
    // Not identical: the period is quoted to two decimals and the orbit genuinely precesses.
    // But it must be a small fraction of the ~6,780 km orbital radius, not halfway around.
    expect(separation).toBeLessThan(200);
  });

  it('moves the satellite a sensible distance in one minute', () => {
    const [compiled] = compileElements([ISS]);
    const satrec = compiled?.satrec;
    if (!satrec) return;
    const a = propagateState(satrec, EPOCH);
    const b = propagateState(satrec, new Date(EPOCH.getTime() + 60_000));
    if (!a || !b) return;
    const moved = Math.hypot(
      a.positionEciKm.x - b.positionEciKm.x,
      a.positionEciKm.y - b.positionEciKm.y,
      a.positionEciKm.z - b.positionEciKm.z,
    );
    // ~7.66 km/s for 60 s, slightly less as a chord than along the arc.
    expect(moved).toBeGreaterThan(400);
    expect(moved).toBeLessThan(470);
  });
});

describe('buffer propagation for the point field', () => {
  it('writes scene-space positions in place, with Earth at one unit', () => {
    const compiled = compileElements([ISS]);
    const positions = new Float32Array(3);
    const flags = new Uint8Array(1);
    const result = propagateInto(compiled, EPOCH, positions, flags);

    expect(result.ok).toBe(1);
    expect(result.failed).toBe(0);
    expect(flags[0]).toBe(PropagationFlag.Ok);

    // Earth is exactly 1 scene unit, so a 420 km orbit sits at ~1.066.
    const r = Math.hypot(positions[0] ?? 0, positions[1] ?? 0, positions[2] ?? 0);
    expect(r).toBeGreaterThan(1.05);
    expect(r).toBeLessThan(1.08);
  });

  it('uses the project axis mapping, not a raw x/y/z copy', () => {
    const compiled = compileElements([ISS]);
    const satrec = compiled[0]?.satrec;
    if (!satrec) return;
    const positions = new Float32Array(3);
    propagateInto(compiled, EPOCH, positions, new Uint8Array(1));
    const state = propagateState(satrec, EPOCH);
    if (!state) return;
    // sceneY must carry the ECI polar axis (z), and sceneZ the ECI y. Swapping them still
    // produces a sphere of points, which is why this is asserted rather than eyeballed.
    expect(positions[1] ?? 0).toBeCloseTo(state.positionEciKm.z / SCENE_SCALE_KM, 4);
    expect(positions[2] ?? 0).toBeCloseTo(state.positionEciKm.y / SCENE_SCALE_KM, 4);
    expect(positions[0] ?? 0).toBeCloseTo(state.positionEciKm.x / SCENE_SCALE_KM, 4);
  });

  it('flags an object it cannot propagate instead of leaving a stale position', () => {
    const compiled = compileElements([{ ...ISS, l1: 'garbage', l2: 'garbage' }]);
    const positions = new Float32Array([9, 9, 9]);
    const flags = new Uint8Array(1);
    const result = propagateInto(compiled, EPOCH, positions, flags);
    expect(result.failed).toBe(1);
    expect(flags[0]).toBe(PropagationFlag.Failed);
    // The previous value must not survive — a stale point is indistinguishable from a real one.
    expect(Number.isNaN(positions[0] ?? 0)).toBe(true);
  });
});

describe('orbit tracing', () => {
  it('traces a closed path at the right radius', () => {
    const compiled = compileElements([ISS]);
    const satrec = compiled[0]?.satrec;
    if (!satrec) return;
    const path = traceOrbit(satrec, EPOCH, ISS.pmin, 60);
    expect(path).toHaveLength(180);

    for (let i = 0; i < 60; i++) {
      const r = Math.hypot(path[i * 3] ?? 0, path[i * 3 + 1] ?? 0, path[i * 3 + 2] ?? 0);
      // Every sample sits on the orbit, above the surface and near-circular for the ISS.
      expect(r).toBeGreaterThan(1.05);
      expect(r).toBeLessThan(1.08);
    }

    // First and last samples close the loop.
    const gap = Math.hypot(
      (path[0] ?? 0) - (path[177] ?? 0),
      (path[1] ?? 0) - (path[178] ?? 0),
      (path[2] ?? 0) - (path[179] ?? 0),
    );
    expect(gap).toBeLessThan(0.05);
  });
});

describe('propagation completes over a whole catalogue, whatever individual objects do', () => {
  it('never throws, and always accounts for every object', () => {
    // The regression this guards. satellite.js's `propagate` signals failure in more than one
    // shape: `false`, and an object whose `position` is `undefined`. The original guard tested
    // only for `false`, so an undefined reached `p.x`, threw, and aborted the loop partway
    // through the catalogue — leaving a half-written position buffer and never reporting a
    // result. In the browser that appeared as "Placed by SGP4: 0" beside a field of visibly
    // rendered points: a contradiction no type checker would catch.
    //
    // The invariant asserted here is the one that actually broke: the loop RUNS TO COMPLETION
    // and every object ends up counted, whatever SGP4 makes of it individually.
    const batch: CatalogElement[] = [
      ISS,
      { ...ISS, id: 2, l1: 'not a tle', l2: 'not a tle' },
      { ...ISS, id: 3 },
    ];
    const compiled = compileElements(batch);
    const positions = new Float32Array(batch.length * 3);
    const flags = new Uint8Array(batch.length);

    // Epoch, then far future and far past. Propagating decades from epoch is where SGP4
    // genuinely diverges, which is exactly where the undefined-position path is reached.
    for (const offsetDays of [0, 365 * 40, -365 * 40, 400000]) {
      const date = new Date(EPOCH.getTime() + offsetDays * 86400000);
      let ok = -1;
      let failed = -1;
      expect(() => {
        const result = propagateInto(compiled, date, positions, flags);
        ok = result.ok;
        failed = result.failed;
      }).not.toThrow();
      expect(ok + failed).toBe(batch.length);
    }
  });

  it('marks a failed object as failed rather than leaving its previous position behind', () => {
    const compiled = compileElements([ISS]);
    const positions = new Float32Array(3);
    const flags = new Uint8Array(1);

    propagateInto(compiled, EPOCH, positions, flags);
    expect(flags[0]).toBe(PropagationFlag.Ok);
    expect(Number.isFinite(positions[0] ?? Number.NaN)).toBe(true);

    // Far enough from epoch that SGP4 gives up.
    const far = new Date(EPOCH.getTime() + 365 * 200 * 86400000);
    const result = propagateInto(compiled, far, positions, flags);
    if (result.failed === 1) {
      expect(flags[0]).toBe(PropagationFlag.Failed);
      // The stale position must not survive: a point that keeps drawing where the object used
      // to be is indistinguishable from a real one.
      expect(Number.isNaN(positions[0] ?? 0)).toBe(true);
    }
  });
});
