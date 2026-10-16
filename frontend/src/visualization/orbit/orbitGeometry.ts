/**
 * Generates the points of a circular orbit ring from real orbital ELEMENTS (altitude,
 * inclination, RAAN) — brief §43: "the frontend may generate geometry from parameters. It
 * must not invent orbital state." This is the exact position formula `Orbit::positionAtKm()`
 * (`cpp/domain/Orbit.hpp`) uses, evaluated at `segments` evenly spaced phase angles to trace
 * the ring's shape — a static geometric visualization of a real orbit's SHAPE, not a
 * time-dependent propagation (no `t` appears anywhere below; the actual, live position at
 * the current simulation time comes from `OrbitView.phaseDeg`/`PositionView` in the
 * snapshot itself, never re-derived here).
 */

import { applyAltitudeExaggeration, SCENE_SCALE_KM } from '../coordinates';
import type { OrbitView } from '../../types/engine';

const EARTH_RADIUS_KM = 6378.137;

export function orbitRingPoints(
  orbit: OrbitView,
  segments = 128,
  exaggeration = 1,
): Array<[number, number, number]> {
  const aTrue = orbit.altitudeKm + EARTH_RADIUS_KM;
  // Only the RADIUS passes through the documented visual transform; the orbit's real shape,
  // orientation and the engine's own values are untouched (`coordinates.ts`).
  const a = applyAltitudeExaggeration(aTrue / SCENE_SCALE_KM, exaggeration) * SCENE_SCALE_KM;
  const inc = (orbit.inclinationDeg * Math.PI) / 180;
  const raan = (orbit.raanDeg * Math.PI) / 180;
  const cosRaan = Math.cos(raan);
  const sinRaan = Math.sin(raan);
  const cosInc = Math.cos(inc);
  const sinInc = Math.sin(inc);

  const points: Array<[number, number, number]> = [];
  for (let i = 0; i <= segments; i++) {
    const theta = (i / segments) * 2 * Math.PI;
    const cosT = Math.cos(theta);
    const sinT = Math.sin(theta);
    const eciX = a * (cosRaan * cosT - sinRaan * sinT * cosInc);
    const eciY = a * (sinRaan * cosT + cosRaan * sinT * cosInc);
    const eciZ = a * (sinT * sinInc);
    // Same axis mapping as `coordinates.ts::eciToScene` -- polar (z) becomes scene "up".
    points.push([eciX / SCENE_SCALE_KM, eciZ / SCENE_SCALE_KM, eciY / SCENE_SCALE_KM]);
  }
  return points;
}
