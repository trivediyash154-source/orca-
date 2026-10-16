/**
 * The ONE coordinate conversion between engine space and Three.js scene space — brief §42:
 * "create one documented conversion function... do not scatter axis swaps across components."
 *
 * ## Engine convention (ECI, `Orbit.hpp`)
 *
 * Earth's equatorial plane is the x-y plane, z is the polar axis (positive toward the north
 * pole) — visible directly in `Orbit::positionAtKm()`'s own formula:
 * `z = a * sin(theta) * sin(inclination)`. Units are kilometres.
 *
 * ## Scene convention (Three.js)
 *
 * Three.js is Y-up, so the engine's polar axis (z) maps onto the scene's vertical axis:
 *
 *     sceneX =  eciX / SCENE_SCALE_KM
 *     sceneY =  eciZ / SCENE_SCALE_KM   (polar axis becomes "up")
 *     sceneZ =  eciY / SCENE_SCALE_KM
 *
 * `SCENE_SCALE_KM` is Earth's own radius, so Earth renders at exactly 1 scene unit.
 *
 * ## VISUALIZATION TRANSFORM — altitude exaggeration (Phase 12 brief §4/§23)
 *
 * At true scale, ORCA's orbital shells are visually indistinguishable: a 500 km orbit sits at
 * r = 1.078 Earth radii and an 800 km orbit at r = 1.125 — a 4% difference, which on screen
 * collapses every shell into what looks like one line hugging the surface. That is exactly
 * the "orbital paths visually collapse" symptom this transform exists to fix.
 *
 * `applyAltitudeExaggeration()` pushes ALTITUDE ABOVE THE SURFACE outward while leaving
 * Earth's radius at 1 and preserving shell ORDERING and the direction of every position
 * vector. It saturates, so an arbitrarily high orbit (e.g. GEO at r = 6.6) stays inside a
 * bounded scene radius instead of flying off-frame:
 *
 *     altFrac  = |r| / R_earth - 1                      (true altitude, in Earth radii)
 *     altFrac' = MAX_VISUAL_ALT * (1 - exp(-altFrac * k / MAX_VISUAL_ALT))
 *     r'       = 1 + altFrac'
 *
 * At `k = 1` this is very close to the identity for LEO (0.0784 -> 0.0759), so
 * "exaggeration = 1" genuinely means "near true scale."
 *
 * **This is a rendering transform only.** It is applied when converting a position for
 * display and NOWHERE else. No value sent to the engine, used in a distance/proximity
 * readout, or shown in an inspector passes through it — those all use real kilometres
 * (`sceneDistanceToKm()` below deliberately does not exist for that reason; callers that
 * need real distance use the raw `PositionView` values directly).
 */

import type { PositionView } from '../types/engine';

export const SCENE_SCALE_KM = 6378.137; // kEarthRadiusKm (core/Constants.hpp) -- Earth = 1 scene unit

/** The largest altitude (in Earth radii above the surface) any exaggerated orbit can reach.
 * Keeps the whole constellation inside a scene radius of ~2.2 regardless of input. */
const MAX_VISUAL_ALT = 1.2;

/** @param exaggeration 1 = near true scale; larger values separate shells more. */
export function applyAltitudeExaggeration(radiusEarthRadii: number, exaggeration: number): number {
  if (exaggeration <= 1.0001) return radiusEarthRadii;
  const altFrac = Math.max(0, radiusEarthRadii - 1);
  const exaggerated = MAX_VISUAL_ALT * (1 - Math.exp((-altFrac * exaggeration) / MAX_VISUAL_ALT));
  return 1 + exaggerated;
}

/** Real ECI kilometres -> scene units, no visual transform. Use when true geometry matters. */
export function eciToScene(position: PositionView): [number, number, number] {
  return [position.x / SCENE_SCALE_KM, position.z / SCENE_SCALE_KM, position.y / SCENE_SCALE_KM];
}

/** Real ECI kilometres -> scene units WITH the documented altitude-exaggeration transform
 * above. Direction is preserved exactly; only radial distance is remapped. */
export function eciToSceneExaggerated(
  position: PositionView,
  exaggeration: number,
): [number, number, number] {
  const [x, y, z] = eciToScene(position);
  const r = Math.hypot(x, y, z);
  if (r < 1e-9) return [x, y, z];
  const scale = applyAltitudeExaggeration(r, exaggeration) / r;
  return [x * scale, y * scale, z * scale];
}

export function kmToSceneUnits(km: number): number {
  return km / SCENE_SCALE_KM;
}
