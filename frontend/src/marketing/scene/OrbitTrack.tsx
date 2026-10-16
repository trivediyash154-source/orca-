import { Line } from '@react-three/drei';
import { useMemo } from 'react';

import { orbitRingPoints } from '../../visualization/orbit/orbitGeometry';
import type { OrbitView } from '../../types/engine';

export interface OrbitTrackProps {
  readonly orbit: OrbitView;
  /** The documented altitude-exaggeration visual transform (`coordinates.ts`) — display
   * only, and applied to radius alone. */
  readonly exaggeration: number;
  readonly color: string;
  readonly opacity: number;
  readonly dashed?: boolean;
  readonly lineWidth?: number;
  /** Raised for the two mission orbits, where the ring is read rather than felt. */
  readonly segments?: number;
}

/**
 * One orbit, traced from its real elements by the project's own `orbitRingPoints()` — the
 * same generator the workspace's `OrbitRing` uses, and the same `Line` primitive, so the
 * entry surface and the operator view are drawing the identical geometry rather than two
 * approximations of it.
 *
 * Colour is a parameter here (it is a band lookup in `OrbitRing`) because on this surface a
 * ring's colour encodes its ROLE in the scenario — departure orbit, casualty orbit, context
 * traffic — not its altitude band.
 */
export function OrbitTrack({
  orbit,
  exaggeration,
  color,
  opacity,
  dashed = false,
  lineWidth = 1,
  segments = 160,
}: OrbitTrackProps) {
  const points = useMemo(
    () => orbitRingPoints(orbit, segments, exaggeration),
    [orbit, segments, exaggeration],
  );

  return (
    <Line
      points={points}
      color={color}
      lineWidth={lineWidth}
      dashed={dashed}
      dashSize={0.05}
      gapSize={0.035}
      transparent
      opacity={opacity}
      depthWrite={false}
    />
  );
}
