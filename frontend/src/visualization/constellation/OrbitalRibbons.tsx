import { useMemo } from 'react';
import { AdditiveBlending, BufferGeometry, Float32BufferAttribute } from 'three';

import { applyAltitudeExaggeration, SCENE_SCALE_KM } from '../coordinates';
import type { VisualObjectView } from '../../types/engine';

const EARTH_RADIUS_KM = 6378.137;

export interface OrbitalRibbonsProps {
  readonly objects: readonly VisualObjectView[];
  readonly exaggeration: number;
  /** How many representative orbital planes to trace. Thousands of individual ribbons would
   * be unreadable AND expensive (brief §5: "Do not create thousands of expensive individual
   * geometries"), so the constellation is bucketed and a bounded sample is drawn.
   *
   * Tuned against a real browser screenshot: 140 ribbons at 0.28 additive opacity saturated
   * into a white tangle that buried the satellites it was meant to contextualise. Brief §5's
   * own hierarchy puts constellation bands at "subtle", so the count and opacity are both
   * well below what looked reasonable in the abstract. */
  readonly maxRibbons?: number;
  readonly opacity?: number;
}

/** Traces one orbit's full circle using the EXACT position formula the engine itself uses
 * (`som::positionKm`, `cpp/domain/SimplifiedOrbitalModel.cpp` lines 29-45) -- so a ribbon is
 * the real shape of a real orbit's path, evaluated from the real elements the engine
 * reported, never an invented circle. Only the RADIUS passes through the documented
 * altitude-exaggeration visual transform (`coordinates.ts`). */
function traceOrbit(
  altitudeKm: number,
  inclinationDeg: number,
  raanDeg: number,
  exaggeration: number,
  segments: number,
  out: number[],
): void {
  const aKm = altitudeKm + EARTH_RADIUS_KM;
  const rTrue = aKm / SCENE_SCALE_KM;
  const r = applyAltitudeExaggeration(rTrue, exaggeration);
  const inc = (inclinationDeg * Math.PI) / 180;
  const raan = (raanDeg * Math.PI) / 180;
  const cosRaan = Math.cos(raan);
  const sinRaan = Math.sin(raan);
  const cosInc = Math.cos(inc);
  const sinInc = Math.sin(inc);

  let prevX = 0;
  let prevY = 0;
  let prevZ = 0;
  for (let i = 0; i <= segments; i++) {
    const theta = (i / segments) * Math.PI * 2;
    const cosT = Math.cos(theta);
    const sinT = Math.sin(theta);
    const eciX = r * (cosRaan * cosT - sinRaan * sinT * cosInc);
    const eciY = r * (sinRaan * cosT + cosRaan * sinT * cosInc);
    const eciZ = r * (sinT * sinInc);
    // Same axis mapping as `coordinates.ts::eciToScene`: polar (z) becomes scene "up".
    const x = eciX;
    const y = eciZ;
    const z = eciY;
    if (i > 0) {
      out.push(prevX, prevY, prevZ, x, y, z);
    }
    prevX = x;
    prevY = y;
    prevZ = z;
  }
}

/** Every ribbon merged into ONE `LineSegments` geometry -- one draw call for the whole
 * orbital-shell system regardless of how many planes are traced (brief §5's explicit
 * "efficient batching" requirement). Colour-coded per band so LEO/MEO/GEO shells are
 * distinguishable at a glance. */
export function OrbitalRibbons({ objects, exaggeration, maxRibbons = 55, opacity = 0.1 }: OrbitalRibbonsProps) {
  const geometry = useMemo(() => {
    // Bucket by (altitude band, inclination band, RAAN band) and keep one representative per
    // bucket -- gives a readable spread of genuinely different planes instead of 5,000
    // near-duplicates.
    const buckets = new Map<string, VisualObjectView>();
    for (const o of objects) {
      if (o.altitudeKm === undefined || o.inclinationDeg === undefined || o.raanDeg === undefined) continue;
      const key = `${Math.round(o.altitudeKm / 220)}|${Math.round(o.inclinationDeg / 12)}|${Math.round(o.raanDeg / 24)}`;
      if (!buckets.has(key)) buckets.set(key, o);
      if (buckets.size >= maxRibbons * 3) break;
    }

    const chosen = [...buckets.values()].slice(0, maxRibbons);
    const positions: number[] = [];
    const colors: number[] = [];

    for (const o of chosen) {
      const before = positions.length;
      // GEO/MEO circles are much larger on screen, so they get more segments to stay smooth.
      const segments = (o.band === 'GEO' ? 160 : o.band === 'MEO' ? 128 : 96);
      traceOrbit(o.altitudeKm!, o.inclinationDeg!, o.raanDeg!, exaggeration, segments, positions);
      const added = (positions.length - before) / 3;
      const [r, g, b] =
        o.band === 'GEO' ? [1.0, 0.48, 0.72] : o.band === 'MEO' ? [0.66, 0.47, 1.0] : [0.31, 0.66, 1.0];
      for (let i = 0; i < added; i++) colors.push(r, g, b);
    }

    const geo = new BufferGeometry();
    geo.setAttribute('position', new Float32BufferAttribute(positions, 3));
    geo.setAttribute('color', new Float32BufferAttribute(colors, 3));
    return geo;
  }, [objects, exaggeration, maxRibbons]);

  if (objects.length === 0) return null;

  return (
    <lineSegments geometry={geometry} raycast={() => null}>
      <lineBasicMaterial
        vertexColors
        transparent
        opacity={opacity}
        depthWrite={false}
        blending={AdditiveBlending}
      />
    </lineSegments>
  );
}

/** ONE bright ribbon for the selected object's own orbit -- brief §9's "selecting an object
 * must also highlight its orbital ribbon". Deliberately a separate, tiny geometry drawn on
 * top of the subtle batched field above, so it wins the visual hierarchy (brief §5: selected
 * orbit = highlighted, constellation bands = subtle). */
export function SelectedOrbitRibbon({
  object,
  exaggeration,
}: {
  object: VisualObjectView | null;
  exaggeration: number;
}) {
  const geometry = useMemo(() => {
    if (
      !object ||
      object.altitudeKm === undefined ||
      object.inclinationDeg === undefined ||
      object.raanDeg === undefined
    ) {
      return null;
    }
    const positions: number[] = [];
    traceOrbit(object.altitudeKm, object.inclinationDeg, object.raanDeg, exaggeration, 192, positions);
    const geo = new BufferGeometry();
    geo.setAttribute('position', new Float32BufferAttribute(positions, 3));
    return geo;
  }, [object, exaggeration]);

  if (!geometry) return null;

  return (
    <lineSegments geometry={geometry} raycast={() => null}>
      <lineBasicMaterial color="#ffffff" transparent opacity={0.85} depthWrite={false} />
    </lineSegments>
  );
}
