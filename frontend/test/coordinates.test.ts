import { describe, expect, it } from 'vitest';

import {
  applyAltitudeExaggeration,
  eciToScene,
  eciToSceneExaggerated,
  SCENE_SCALE_KM,
} from '../src/visualization/coordinates';

/** The altitude-exaggeration VISUALIZATION transform (Phase 12 brief §4/§23). These tests
 * pin the properties the transform must have for it to be honest: it may remap radius, but
 * it must never reorder shells, never move Earth's surface, and never change direction. */
describe('applyAltitudeExaggeration', () => {
  const LEO_500 = (6378.137 + 500) / SCENE_SCALE_KM;
  const LEO_800 = (6378.137 + 800) / SCENE_SCALE_KM;
  const GEO = (6378.137 + 35786) / SCENE_SCALE_KM;

  it('is the identity at exaggeration = 1 (true scale)', () => {
    expect(applyAltitudeExaggeration(LEO_500, 1)).toBeCloseTo(LEO_500, 10);
    expect(applyAltitudeExaggeration(GEO, 1)).toBeCloseTo(GEO, 10);
  });

  it("never moves Earth's own surface (r = 1 stays r = 1)", () => {
    for (const k of [1, 6, 10, 20]) {
      expect(applyAltitudeExaggeration(1, k)).toBeCloseTo(1, 10);
    }
  });

  it('is strictly monotonic, so shell ORDER is always preserved', () => {
    for (const k of [1, 6, 10, 20]) {
      const a = applyAltitudeExaggeration(LEO_500, k);
      const b = applyAltitudeExaggeration(LEO_800, k);
      const c = applyAltitudeExaggeration(GEO, k);
      expect(a).toBeLessThan(b);
      expect(b).toBeLessThan(c);
    }
  });

  it('actually separates shells that are nearly identical at true scale', () => {
    // The real problem it exists to solve: at true scale these differ by <5%.
    const trueGap = (LEO_800 - LEO_500) / LEO_500;
    expect(trueGap).toBeLessThan(0.05);
    const a = applyAltitudeExaggeration(LEO_500, 10);
    const b = applyAltitudeExaggeration(LEO_800, 10);
    expect((b - a) / a).toBeGreaterThan(trueGap * 2);
  });

  it('stays bounded even for a very high orbit, so nothing flies off-frame', () => {
    expect(applyAltitudeExaggeration(GEO, 20)).toBeLessThan(6);
    expect(applyAltitudeExaggeration(1000, 20)).toBeLessThan(10);
  });
});

describe('eciToSceneExaggerated', () => {
  const pos = { x: 6878.137, y: 0, z: 0 };

  it('preserves direction exactly -- only radial distance is remapped', () => {
    const [x, y, z] = eciToSceneExaggerated({ x: 3000, y: 4000, z: 5000 }, 10);
    const r = Math.hypot(x, y, z);
    // Same unit vector as the untransformed conversion.
    const [ux, uy, uz] = eciToScene({ x: 3000, y: 4000, z: 5000 });
    const ur = Math.hypot(ux, uy, uz);
    expect(x / r).toBeCloseTo(ux / ur, 9);
    expect(y / r).toBeCloseTo(uy / ur, 9);
    expect(z / r).toBeCloseTo(uz / ur, 9);
  });

  it('matches the untransformed conversion at exaggeration = 1', () => {
    expect(eciToSceneExaggerated(pos, 1)).toEqual(eciToScene(pos));
  });

  it('pushes an orbit outward at higher exaggeration', () => {
    const near = Math.hypot(...eciToSceneExaggerated(pos, 1));
    const far = Math.hypot(...eciToSceneExaggerated(pos, 10));
    expect(far).toBeGreaterThan(near);
  });
});
