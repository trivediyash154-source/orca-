import { Line } from '@react-three/drei';

import { orbitRingPoints } from './orbitGeometry';
import type { OrbitView } from '../../types/engine';

export interface OrbitRingProps {
  readonly orbit: OrbitView;
  /** Documented altitude-exaggeration visual transform (`coordinates.ts`) -- display only. */
  readonly exaggeration?: number;
}

/** Brief §16: subtle visual differentiation by band -- reads the design tokens' band
 * accents (`tokens.css`) directly rather than duplicating the hex values here. Phase 12
 * brief §2: brightened from Phase 11's near-invisible `#3d6ea8`-family palette (barely
 * distinguishable from the black background in a real-browser screenshot check) to
 * saturated, higher-contrast hues that stay legible at normal desktop viewing distance. */
const BAND_COLOR: Record<string, string> = {
  LEO: '#4fa8ff',
  MEO: '#a878ff',
  GEO: '#ff7ab8',
};

/** One entity's orbit, traced as a thin ring — brief §16/§43's "basic orbit line," never a
 * "giant glowing ring" (brief §16's explicit warning). Phase 12: brighter and slightly
 * thicker than Phase 11's version so the orbital shell structure reads clearly (brief §2's
 * "visible orbital shells/path geometry... enough contrast for normal desktop viewing"),
 * while staying a thin line, not a filled band. */
export function OrbitRing({ orbit, exaggeration = 1 }: OrbitRingProps) {
  const points = orbitRingPoints(orbit, 128, exaggeration);
  const color = BAND_COLOR[orbit.band] ?? '#4fa8ff';
  return <Line points={points} color={color} lineWidth={1.4} transparent opacity={0.55} />;
}
