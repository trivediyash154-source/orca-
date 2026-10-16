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

/** Orbital elements, generated once per (family, size, seed). Positions are a pure function of
 * these and the time, so animating the constellation re-evaluates positions only: the 5,000
 * objects keep their identity, plane and slot from frame to frame, and nothing is regenerated. */
interface Elements {
  readonly satellites: readonly Omit<VisualObjectView, 'position'>[];
  readonly vehicles: readonly Omit<VisualObjectView, 'position'>[];
  readonly debris: readonly Omit<VisualObjectView, 'position'>[];
}

const elementCache = new Map<string, Elements>();

function elementsFor(size: number, seed: number, family: string): Elements {
  const key = `${family}:${size}:${seed}`;
  const hit = elementCache.get(key);
  if (hit !== undefined) return hit;

  const random = lcg(0x5_be11 ^ (seed * 2_654_435_761));
  const satellites: Omit<VisualObjectView, 'position'>[] = [];
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
        satellites.push({
          id: `VIS-${String(index).padStart(5, '0')}`,
          name: `${shell.band}-${Math.round(shell.altitudeKm)}-P${plane + 1}-S${slot + 1}`,
          status: random() < 0.015 ? 'WARNING' : 'NOMINAL',
          altitudeKm: shell.altitudeKm + (random() - 0.5) * (shell.band === 'LEO' ? 12 : 60),
          inclinationDeg: shell.inclinationDeg + (random() - 0.5) * 0.4,
          raanDeg,
          phaseDeg: (slot * 360) / perPlane + plane * phasing + random() * 0.8,
          band: shell.band,
        });
      }
    }
  }

  const vehicleCount = Math.max(3, Math.round(size / 400));
  const vehicles = Array.from({ length: vehicleCount }, (_, i) => ({
    id: `VIS-RV-${String(i + 1).padStart(3, '0')}`,
    name: `RESCUE-${i + 1}`,
    status: 'IDLE',
    altitudeKm: 520 + random() * 300,
    inclinationDeg: 45 + random() * 50,
    raanDeg: random() * 360,
    phaseDeg: random() * 360,
    band: 'LEO',
  }));

  const debris = Array.from({ length: Math.round(size * 0.08) }, (_, i) => ({
    id: `VIS-DEB-${String(i + 1).padStart(5, '0')}`,
    altitudeKm: 450 + random() * 900,
    inclinationDeg: random() * 100,
    raanDeg: random() * 360,
    phaseDeg: random() * 360,
    band: 'LEO',
    hazardRadiusKm: 5 + random() * 30,
  }));

  const elements: Elements = { satellites, vehicles, debris };
  // Bounded: a reader stepping through sizes and seeds should not accumulate every variant.
  if (elementCache.size > 8) elementCache.clear();
  elementCache.set(key, elements);
  return elements;
}

/** Places a set of elements at `timeSec` on its circular orbit. The phase reported is the phase
 * at that time, so the object detail panel and the drawn position agree. */
function place(
  objects: readonly Omit<VisualObjectView, 'position'>[],
  timeSec: number,
): VisualObjectView[] {
  return objects.map((o) => ({
    ...o,
    position: positionFor(o.altitudeKm ?? 550, o.inclinationDeg ?? 0, o.raanDeg ?? 0, o.phaseDeg ?? 0, timeSec),
  }));
}

export function demoVisualConstellation(
  size: number,
  seed: number,
  timeSec: number,
  family = 'visualShell',
): VisualConstellationSnapshot {
  const started = typeof performance === 'undefined' ? 0 : performance.now();
  const elements = elementsFor(size, seed, family);
  const satellites = place(elements.satellites, timeSec);
  const vehicles = place(elements.vehicles, timeSec);
  const debris = place(elements.debris, timeSec);

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

  return snapshot;
}
