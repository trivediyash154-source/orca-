/**
 * The large visual constellation, generated in the browser when no engine is reachable.
 *
 * `GET /visual/constellation` asks a one-shot engine process for thousands of synthetic
 * objects to draw behind the live mission. On a static deployment that request has nowhere to
 * go, and Mission Control was left with a caption promising 5,000 objects over an empty sky.
 *
 * This produces a `VisualConstellationSnapshot` of the same shape from the same closed-form
 * circular orbits `demoEngine.ts` uses, laid out the way real constellations are: Walker-style
 * shells of evenly spaced orbital planes, each carrying evenly phased satellites, plus a
 * scattering of rescue vehicles and debris. It is visualisation data, exactly as the engine's
 * version is — never part of the planning graph — and the UI captions say which produced it.
 */

import type { VisualConstellationSnapshot, VisualObjectView } from '../types/engine';
import { lcg, positionFor } from './demoEngine';

interface Shell {
  readonly altitudeKm: number;
  readonly inclinationDeg: number;
  readonly band: string;
  /** Share of the satellites this shell carries. */
  readonly share: number;
}

/** Modelled on the shells real LEO broadband and navigation constellations occupy: several
 * dense low shells at different inclinations (which is what makes the sphere read as a sphere
 * rather than a band), a sparse polar shell, and thin MEO and GEO populations. */
const SHELLS: readonly Shell[] = [
  { altitudeKm: 550, inclinationDeg: 53, band: 'LEO', share: 0.3 },
  { altitudeKm: 570, inclinationDeg: 70, band: 'LEO', share: 0.14 },
  { altitudeKm: 560, inclinationDeg: 97.6, band: 'LEO', share: 0.12 },
  { altitudeKm: 1_110, inclinationDeg: 53.8, band: 'LEO', share: 0.14 },
  { altitudeKm: 1_200, inclinationDeg: 87.9, band: 'LEO', share: 0.12 },
  { altitudeKm: 780, inclinationDeg: 86.4, band: 'LEO', share: 0.08 },
  { altitudeKm: 20_200, inclinationDeg: 55, band: 'MEO', share: 0.06 },
  { altitudeKm: 35_786, inclinationDeg: 0.1, band: 'GEO', share: 0.04 },
];

const cache = new Map<string, VisualConstellationSnapshot>();

export function demoVisualConstellation(
  size: number,
  seed: number,
  timeSec: number,
  family = 'visualShell',
): VisualConstellationSnapshot {
  const key = `${family}:${size}:${seed}:${timeSec}`;
  const hit = cache.get(key);
  if (hit !== undefined) return hit;

  const started = typeof performance === 'undefined' ? 0 : performance.now();
  const random = lcg(0x5_be11 ^ (seed * 2_654_435_761));

  const satellites: VisualObjectView[] = [];
  let index = 0;

  for (const shell of SHELLS) {
    const count = Math.max(1, Math.round(size * 0.92 * shell.share));
    // Roughly square: as many planes as satellites per plane, which is how Walker-delta
    // constellations are usually balanced.
    const planes = Math.max(1, Math.round(Math.sqrt(count)));
    const perPlane = Math.max(1, Math.ceil(count / planes));
    // Walker phasing factor: each plane's first satellite is offset from the previous plane's,
    // so neighbouring planes do not cross the equator in lockstep.
    const phasing = 360 / (planes * perPlane);

    for (let plane = 0; plane < planes; plane += 1) {
      const raanDeg = (plane * 360) / planes + random() * 0.6;
      for (let slot = 0; slot < perPlane && satellites.length < size * 0.92; slot += 1) {
        index += 1;
        const phaseDeg = (slot * 360) / perPlane + plane * phasing + random() * 0.8;
        const altitudeKm = shell.altitudeKm + (random() - 0.5) * (shell.band === 'LEO' ? 12 : 60);
        const inclinationDeg = shell.inclinationDeg + (random() - 0.5) * 0.4;
        satellites.push({
          id: `VIS-${String(index).padStart(5, '0')}`,
          name: `${shell.band}-${Math.round(shell.altitudeKm)}-P${plane + 1}-S${slot + 1}`,
          status: random() < 0.015 ? 'WARNING' : 'NOMINAL',
          position: positionFor(altitudeKm, inclinationDeg, raanDeg, phaseDeg, timeSec),
          altitudeKm,
          inclinationDeg,
          raanDeg,
          phaseDeg,
          band: shell.band,
        });
      }
    }
  }

  const vehicleCount = Math.max(3, Math.round(size / 400));
  const vehicles: VisualObjectView[] = Array.from({ length: vehicleCount }, (_, i) => {
    const altitudeKm = 520 + random() * 300;
    const inclinationDeg = 45 + random() * 50;
    const raanDeg = random() * 360;
    const phaseDeg = random() * 360;
    return {
      id: `VIS-RV-${String(i + 1).padStart(3, '0')}`,
      name: `RESCUE-${i + 1}`,
      status: 'IDLE',
      position: positionFor(altitudeKm, inclinationDeg, raanDeg, phaseDeg, timeSec),
      altitudeKm,
      inclinationDeg,
      raanDeg,
      phaseDeg,
      band: 'LEO',
    };
  });

  const debrisCount = Math.round(size * 0.08);
  const debris: VisualObjectView[] = Array.from({ length: debrisCount }, (_, i) => {
    const altitudeKm = 450 + random() * 900;
    const inclinationDeg = random() * 100;
    const raanDeg = random() * 360;
    const phaseDeg = random() * 360;
    return {
      id: `VIS-DEB-${String(i + 1).padStart(5, '0')}`,
      position: positionFor(altitudeKm, inclinationDeg, raanDeg, phaseDeg, timeSec),
      altitudeKm,
      inclinationDeg,
      raanDeg,
      phaseDeg,
      band: 'LEO',
      hazardRadiusKm: 5 + random() * 30,
    };
  });

  const snapshot: VisualConstellationSnapshot = {
    type: 'visualConstellation',
    scenarioName: `Browser-generated ${family} constellation (kinematic model)`,
    timeSec,
    generationMs:
      typeof performance === 'undefined' ? 0 : Math.round((performance.now() - started) * 100) / 100,
    satelliteCount: satellites.length,
    vehicleCount: vehicles.length,
    debrisCount: debris.length,
    satellites,
    vehicles,
    debris,
  };

  // Bounded: a reader stepping through sizes and seeds should not accumulate every variant.
  if (cache.size > 12) cache.clear();
  cache.set(key, snapshot);
  return snapshot;
}
