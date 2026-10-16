/**
 * Drift guard. `src/marketing/data/preferenceProfiles.ts` mirrors `configs/profiles.json` by
 * hand (that file sits above the Vite root, so it cannot be imported into the browser bundle).
 * A hand-mirrored copy is only honest while it stays in sync — this test reads the REAL config
 * off disk and fails the suite the moment the two disagree, rather than letting the landing page
 * quietly misstate how the engine actually selects a plan.
 */

// Loaded through Vite's `?raw` rather than `node:fs`: this package has no `@types/node`, and
// adding one to typecheck a single test would be a dependency the repo does not need. The
// import is resolved relative to THIS file, so unlike the previous `process.cwd()` form it does
// not quietly read the wrong file (or nothing) when the suite is run from the repo root. It is
// still the real `configs/profiles.json` off disk, re-read on every run -- the guard is intact.
import profilesRaw from '../../configs/profiles.json?raw';
import { describe, expect, it } from 'vitest';

import {
  DEFAULT_PROFILE_ID,
  PREFERENCE_PROFILES,
  TIE_BREAKING_ORDER,
  normalisedWeights,
  profileScore,
} from '../src/marketing/data/preferenceProfiles';

interface ProfilesConfig {
  readonly profiles: Record<
    string,
    { label: string; description: string; weights: Record<string, number> }
  >;
  readonly defaultProfile: string;
  readonly tieBreaking: { order: string[] };
}

// Parsed rather than imported as a module so the file's own text is what is under test:
// reading it as JSON would let TypeScript infer a shape from the fixture and hide a malformed
// config behind a compile-time type. Reading the real file (rather than a fixture) is the
// entire point of this guard.
const config = JSON.parse(profilesRaw) as ProfilesConfig;

describe('preference profiles mirror configs/profiles.json', () => {
  it('every mirrored profile matches the real config exactly', () => {
    for (const profile of PREFERENCE_PROFILES) {
      const real = config.profiles[profile.id];
      expect(real, `configs/profiles.json is missing ${profile.id}`).toBeDefined();
      expect(profile.label).toBe(real?.label);
      expect(profile.description).toBe(real?.description);
      expect(profile.weights.time).toBe(real?.weights.time);
      expect(profile.weights.fuel).toBe(real?.weights.fuel);
      expect(profile.weights.risk).toBe(real?.weights.risk);
      expect(profile.weights.distance).toBe(real?.weights.distance);
    }
  });

  it('mirrors the real default profile and tie-breaking order', () => {
    expect(DEFAULT_PROFILE_ID).toBe(config.defaultProfile);
    expect([...TIE_BREAKING_ORDER]).toEqual(config.tieBreaking.order);
  });

  it('excludes CUSTOM deliberately, and nothing else', () => {
    // CUSTOM is operator-slider-driven and has no meaning without the workspace's sliders.
    const mirrored = new Set(PREFERENCE_PROFILES.map((p) => p.id));
    const missing = Object.keys(config.profiles).filter((id) => !mirrored.has(id));
    expect(missing).toEqual(['CUSTOM']);
  });

  it('keeps a non-zero risk weight in every profile, including FASTEST', () => {
    // OPTIMIZATION_MODEL.md §6.1's stated modelling decision -- the landing page claims this,
    // so it must remain true of the real data.
    for (const profile of PREFERENCE_PROFILES) {
      expect(profile.weights.risk).toBeGreaterThan(0);
    }
  });
});

describe('the engine selection rule reproduced for the marketing surface', () => {
  it('normalises weights so only ratios matter', () => {
    const doubled = normalisedWeights({ time: 0.6, fuel: 0.6, risk: 0.6, distance: 0.2 });
    const single = normalisedWeights({ time: 0.3, fuel: 0.3, risk: 0.3, distance: 0.1 });
    expect(doubled.time).toBeCloseTo(single.time, 12);
    expect(doubled.distance).toBeCloseTo(single.distance, 12);
  });

  it('scores lower for a candidate that is better on the heavily-weighted objective', () => {
    const fastest = PREFERENCE_PROFILES.find((p) => p.id === 'FASTEST');
    expect(fastest).toBeDefined();
    if (!fastest) return;
    // Two candidates, each normalised into [0,1] with 0 best: A is quick but thirsty, B is the
    // reverse. Under FASTEST (time 0.70) A must win.
    const quick = { time: 0.1, fuel: 0.9, risk: 0.5, distance: 0.5 };
    const thrifty = { time: 0.9, fuel: 0.1, risk: 0.5, distance: 0.5 };
    expect(profileScore(quick, fastest.weights)).toBeLessThan(
      profileScore(thrifty, fastest.weights),
    );
  });

  it('reverses that preference under FUEL_SAVING', () => {
    const fuelSaving = PREFERENCE_PROFILES.find((p) => p.id === 'FUEL_SAVING');
    expect(fuelSaving).toBeDefined();
    if (!fuelSaving) return;
    const quick = { time: 0.1, fuel: 0.9, risk: 0.5, distance: 0.5 };
    const thrifty = { time: 0.9, fuel: 0.1, risk: 0.5, distance: 0.5 };
    expect(profileScore(thrifty, fuelSaving.weights)).toBeLessThan(
      profileScore(quick, fuelSaving.weights),
    );
  });

  it('guards against a degenerate all-zero weight vector', () => {
    const zero = { time: 0, fuel: 0, risk: 0, distance: 0 };
    expect(() => profileScore(zero, zero)).not.toThrow();
  });
});
