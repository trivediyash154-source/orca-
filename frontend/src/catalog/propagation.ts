/**
 * SGP4 propagation of catalogue element sets, and the coordinate frames involved.
 *
 * ## Which frames exist here, and where each conversion happens
 *
 * Mixing these silently is the classic way to produce a satellite field that looks plausible and
 * is wrong, so every transition is named:
 *
 *   1. **TLE mean elements** — what the provider publishes. Only SGP4 may consume them; nothing
 *      else in ORCA re-derives a position from them.
 *   2. **TEME** (True Equator, Mean Equinox) — what SGP4 actually outputs, in kilometres.
 *      Commonly called "ECI", and treated as such throughout: the difference from a rigorous
 *      ECI frame is under a kilometre for these epochs, far below what a point two pixels wide
 *      can express. Named honestly here rather than quietly relabelled.
 *   3. **ECEF / geodetic** — Earth-fixed. Reached by rotating TEME through the Greenwich mean
 *      sidereal time for the instant being propagated (`gstime`). This is the ONLY place a
 *      sidereal rotation is applied, and it is what makes a latitude/longitude readout correct.
 *   4. **Scene space** — Three.js, Y-up, Earth radius = 1 unit. Reached through the project's
 *      existing single conversion (`visualization/coordinates.ts`), whose axis mapping
 *      (`sceneX = eciX/R, sceneY = eciZ/R, sceneZ = eciY/R`) is reused verbatim so catalogue
 *      objects and engine entities share one frame rather than two that agree by coincidence.
 *
 * ## Why positions land in a Float32Array
 *
 * Ten thousand objects re-propagated on every clock tick cannot allocate ten thousand objects
 * per tick. Positions are written in place into a caller-owned typed array that is handed
 * straight to a `THREE.BufferAttribute` — no intermediate arrays, no garbage, and the same
 * buffer survives every frame.
 */

import { propagate, twoline2satrec, gstime, eciToGeodetic, type SatRec } from 'satellite.js';

import { SCENE_SCALE_KM } from '../visualization/coordinates';
import type { CatalogElement } from './types';

/** A compiled element set, or the reason it could not be compiled. */
export interface CompiledSatellite {
  readonly id: number;
  readonly satrec: SatRec | null;
  readonly error: string | null;
}

/**
 * Compiles TLE text into SGP4 records once. `twoline2satrec` does the expensive initialisation
 * (deep-space terms, secular rates), so it must not run per frame — which is exactly what makes
 * this the right thing to do once at load and keep.
 */
export function compileElements(elements: readonly CatalogElement[]): CompiledSatellite[] {
  const out: CompiledSatellite[] = [];
  for (const e of elements) {
    try {
      const satrec = twoline2satrec(e.l1, e.l2);
      // satellite.js reports initialisation failure on the record rather than by throwing.
      const code = (satrec as unknown as { error?: number }).error ?? 0;
      if (code !== 0) {
        out.push({ id: e.id, satrec: null, error: `SGP4 initialisation error ${code}` });
        continue;
      }
      // `twoline2satrec` does not validate its input: handed text that is not a TLE it parses
      // NaNs into the record and still reports error 0, and the NaNs only surface later as
      // silently-missing points. Checking the elements it actually needs turns that into an
      // honest per-object failure at load time. Found by feeding it deliberate garbage.
      if (!Number.isFinite(satrec.no) || !Number.isFinite(satrec.ecco) || !Number.isFinite(satrec.inclo)) {
        out.push({ id: e.id, satrec: null, error: 'element set did not parse to finite elements' });
        continue;
      }
      out.push({ id: e.id, satrec, error: null });
    } catch (err) {
      out.push({ id: e.id, satrec: null, error: err instanceof Error ? err.message : String(err) });
    }
  }
  return out;
}

/**
 * Per-object propagation outcome, parallel to the position buffer. A frozen object rather than
 * a `const enum`: this project compiles with `isolatedModules`, under which a `const enum`
 * cannot be inlined across module boundaries.
 */
export const PropagationFlag = {
  /** Position written and usable. */
  Ok: 0,
  /** SGP4 refused this object at this time — decayed, or outside the model's validity. */
  Failed: 1,
} as const;

export interface PropagationResult {
  /** How many objects produced a usable position. */
  readonly ok: number;
  readonly failed: number;
}

/**
 * Propagates every compiled record to `date`, writing scene-space positions into `positions`
 * (3 floats per object) and a status byte into `flags`.
 *
 * Objects that fail are written as NaN and flagged rather than left at their previous position:
 * a stale point that keeps moving with the camera reads as a real object, which is precisely the
 * fabrication this codebase forbids. The renderer drops flagged points.
 */
export function propagateInto(
  compiled: readonly CompiledSatellite[],
  date: Date,
  positions: Float32Array,
  flags: Uint8Array,
): PropagationResult {
  let ok = 0;
  let failed = 0;
  for (let i = 0; i < compiled.length; i++) {
    const entry = compiled[i];
    const base = i * 3;
    if (entry === undefined || entry.satrec === null) {
      positions[base] = Number.NaN;
      positions[base + 1] = Number.NaN;
      positions[base + 2] = Number.NaN;
      flags[i] = PropagationFlag.Failed;
      failed++;
      continue;
    }
    const pv = propagate(entry.satrec, date);
    const p = pv.position;
    // `propagate` reports failure in three different shapes depending on how the record fails:
    // `false`, or an object whose `position` is `undefined`. Testing only for `false` let an
    // undefined slip through to `p.x`, which threw and aborted the whole loop mid-catalogue —
    // leaving a partly-written buffer and no result reported. Found in the browser.
    if (p === undefined || p === null || typeof p === 'boolean' || !Number.isFinite(p.x)) {
      positions[base] = Number.NaN;
      positions[base + 1] = Number.NaN;
      positions[base + 2] = Number.NaN;
      flags[i] = PropagationFlag.Failed;
      failed++;
      continue;
    }
    // TEME kilometres -> scene units, using the project's existing axis mapping: the engine's
    // polar axis (z) becomes the scene's vertical.
    positions[base] = p.x / SCENE_SCALE_KM;
    positions[base + 1] = p.z / SCENE_SCALE_KM;
    positions[base + 2] = p.y / SCENE_SCALE_KM;
    flags[i] = PropagationFlag.Ok;
    ok++;
  }
  return { ok, failed };
}

/** A single object's full state at an instant, in the units a readout wants. */
export interface SatelliteState {
  readonly positionEciKm: { readonly x: number; readonly y: number; readonly z: number };
  readonly velocityKmS: number | null;
  readonly latitudeDeg: number;
  readonly longitudeDeg: number;
  readonly altitudeKm: number;
}

/**
 * Full state for one object, including the Earth-fixed geodetic position.
 *
 * The sidereal rotation is applied here and only here. Longitude is normalised to (-180, 180]
 * because `eciToGeodetic` returns a value that can run past a half-turn.
 */
export function propagateState(satrec: SatRec, date: Date): SatelliteState | null {
  const pv = propagate(satrec, date);
  const p = pv.position;
  if (p === undefined || p === null || typeof p === 'boolean' || !Number.isFinite(p.x)) return null;

  const v = pv.velocity;
  const speed =
    v !== undefined && v !== null && typeof v !== 'boolean' && Number.isFinite(v.x)
      ? Math.hypot(v.x, v.y, v.z)
      : null;

  const geodetic = eciToGeodetic(p, gstime(date));
  let longitudeDeg = (geodetic.longitude * 180) / Math.PI;
  // Wrap into (-180, 180]: a longitude of 190 degrees is -170, not a point off the map.
  longitudeDeg = ((((longitudeDeg + 180) % 360) + 360) % 360) - 180;

  return {
    positionEciKm: { x: p.x, y: p.y, z: p.z },
    velocityKmS: speed,
    latitudeDeg: (geodetic.latitude * 180) / Math.PI,
    longitudeDeg,
    altitudeKm: geodetic.height,
  };
}

/**
 * Traces one object's orbit by propagating it across a full period — the real path SGP4 says it
 * follows, sampled, rather than a circle drawn from its mean altitude. An orbit that is
 * eccentric, or whose plane is regressing, therefore looks like what it is.
 *
 * Returned flat as [x,y,z, x,y,z, ...] in scene units, ready for a line geometry.
 */
export function traceOrbit(satrec: SatRec, startDate: Date, periodMinutes: number, samples = 180): Float32Array {
  const out = new Float32Array(samples * 3);
  const stepMs = (periodMinutes * 60_000) / (samples - 1);
  const t0 = startDate.getTime();
  for (let i = 0; i < samples; i++) {
    const pv = propagate(satrec, new Date(t0 + i * stepMs));
    const p = pv.position;
    const base = i * 3;
    if (p === undefined || p === null || typeof p === 'boolean' || !Number.isFinite(p.x)) {
      out[base] = Number.NaN;
      out[base + 1] = Number.NaN;
      out[base + 2] = Number.NaN;
      continue;
    }
    out[base] = p.x / SCENE_SCALE_KM;
    out[base + 1] = p.z / SCENE_SCALE_KM;
    out[base + 2] = p.y / SCENE_SCALE_KM;
  }
  return out;
}
