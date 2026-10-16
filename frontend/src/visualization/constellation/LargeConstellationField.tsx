import { Html } from '@react-three/drei';
import type { ThreeEvent } from '@react-three/fiber';
import { useEffect, useMemo, useRef, useState } from 'react';
import { AdditiveBlending, BufferAttribute, Color, type Points } from 'three';

import { eciToScene, SCENE_SCALE_KM } from '../coordinates';
import { getMarkerGlowTexture } from '../satellites/markerGlowTexture';
import { DEFAULT_MARKER_COLOR, STATUS_COLORS } from '../satellites/markerColors';
import type { VisualObjectView } from '../../types/engine';

export interface LargeConstellationFieldProps {
  readonly items: readonly VisualObjectView[];
  readonly pointSize: number;
  readonly selectedId: string | null;
  readonly onSelect: (id: string) => void;
  /** Phase 12 brief §9's proximity system: when set, every point (other than the selection
   * itself) is colored by its REAL Euclidean distance from the selected object -- computed
   * here from the same real x/y/z the engine reported, never fabricated -- instead of by
   * status. `NEAR_COLOR`/`MEDIUM_COLOR`/`FAR_COLOR` below and `ProximityLegend.tsx` document
   * the exact mapping so it is never a mystery gradient. */
  readonly proximityMode: boolean;
}

const NEAR_COLOR = new Color('#4fd6ff');
const MEDIUM_COLOR = new Color('#ffb84d');
const FAR_COLOR = new Color('#ff5a5a');
const SELECTED_COLOR = new Color('#ffffff');

/** A large (hundreds-to-thousands of objects) VISUAL constellation field -- Phase 12 brief
 * §5's "different layer" from `EntityMarkerField`'s small, fully-interactive planning-scene
 * markers. One `THREE.Points` draw call for the whole field regardless of count (real,
 * measured precedent: `docs/FINAL_BENCHMARK_REPORT.md` §6 already established Points-style
 * instancing's draw-call win; this pushes the same idea from "glow overlay" to "the primary
 * marker" at 10x-100x the object count). Real click/hover support via the same
 * `ThreeEvent.index` r3f's Points raycaster already provides. */
export function LargeConstellationField({
  items,
  pointSize,
  selectedId,
  onSelect,
  proximityMode,
}: LargeConstellationFieldProps) {
  const pointsRef = useRef<Points>(null);
  const texture = useMemo(() => getMarkerGlowTexture(), []);
  const [hoveredIndex, setHoveredIndex] = useState<number | null>(null);

  const scenePositions = useMemo(() => items.map((item) => eciToScene(item.position)), [items]);
  const selectedIndex = useMemo(
    () => (selectedId !== null ? items.findIndex((it) => it.id === selectedId) : -1),
    [items, selectedId],
  );

  const { positions, colors } = useMemo(
    () => ({
      positions: new Float32Array(Math.max(items.length, 1) * 3),
      colors: new Float32Array(Math.max(items.length, 1) * 3),
    }),
    [items.length],
  );

  // Real distances from the selection, in scene units -- converted back to km for the
  // tooltip via `SCENE_SCALE_KM`, the same single conversion constant `coordinates.ts`
  // documents for every other real-position consumer in this app.
  const distancesFromSelection = useMemo(() => {
    if (!proximityMode || selectedIndex < 0) return null;
    const [sx, sy, sz] = scenePositions[selectedIndex]!;
    const distances = scenePositions.map(([x, y, z]) => Math.hypot(x - sx, y - sy, z - sz));
    const maxDistance = Math.max(1e-6, ...distances.filter((_, i) => i !== selectedIndex));
    return { distances, maxDistance };
  }, [proximityMode, selectedIndex, scenePositions]);

  useEffect(() => {
    const tmp = new Color();
    items.forEach((item, i) => {
      const [x, y, z] = scenePositions[i]!;
      positions[i * 3] = x;
      positions[i * 3 + 1] = y;
      positions[i * 3 + 2] = z;

      if (i === selectedIndex) {
        tmp.copy(SELECTED_COLOR);
      } else if (distancesFromSelection) {
        const ratio = distancesFromSelection.distances[i]! / distancesFromSelection.maxDistance;
        tmp.copy(ratio < 0.33 ? NEAR_COLOR : ratio < 0.66 ? MEDIUM_COLOR : FAR_COLOR);
      } else {
        tmp.set(STATUS_COLORS[item.status ?? ''] ?? DEFAULT_MARKER_COLOR);
        if (i === hoveredIndex) tmp.lerp(SELECTED_COLOR, 0.4);
      }
      colors[i * 3] = tmp.r;
      colors[i * 3 + 1] = tmp.g;
      colors[i * 3 + 2] = tmp.b;
    });
    const geometry = pointsRef.current?.geometry;
    if (!geometry) return;
    (geometry.getAttribute('position') as BufferAttribute).needsUpdate = true;
    (geometry.getAttribute('color') as BufferAttribute).needsUpdate = true;
  }, [items, scenePositions, positions, colors, selectedIndex, hoveredIndex, distancesFromSelection]);

  const handleClick = (e: ThreeEvent<MouseEvent>) => {
    e.stopPropagation();
    if (e.index === undefined) return;
    const item = items[e.index];
    if (item) onSelect(item.id);
  };

  const handlePointerMove = (e: ThreeEvent<PointerEvent>) => {
    e.stopPropagation();
    setHoveredIndex(e.index ?? null);
  };

  const tooltipIndex = hoveredIndex ?? (selectedIndex >= 0 ? selectedIndex : null);
  const tooltipItem = tooltipIndex !== null ? items[tooltipIndex] : undefined;
  const tooltipPos = tooltipIndex !== null ? scenePositions[tooltipIndex] : undefined;
  const tooltipDistanceKm =
    tooltipIndex !== null && distancesFromSelection && tooltipIndex !== selectedIndex
      ? distancesFromSelection.distances[tooltipIndex]! * SCENE_SCALE_KM
      : null;

  if (items.length === 0) return null;

  return (
    <>
      <points
        ref={pointsRef}
        onClick={handleClick}
        onPointerMove={handlePointerMove}
        onPointerOut={() => setHoveredIndex(null)}
      >
        <bufferGeometry>
          <bufferAttribute attach="attributes-position" args={[positions, 3]} />
          <bufferAttribute attach="attributes-color" args={[colors, 3]} />
        </bufferGeometry>
        <pointsMaterial
          map={texture}
          size={pointSize}
          vertexColors
          transparent
          opacity={0.95}
          depthWrite={false}
          blending={AdditiveBlending}
          sizeAttenuation
        />
      </points>

      {tooltipItem && tooltipPos && (
        <Html position={tooltipPos} distanceFactor={8} style={{ pointerEvents: 'none' }}>
          <div className="scene-tooltip">
            <div className="scene-tooltip-title">{tooltipItem.name ?? tooltipItem.id}</div>
            {tooltipItem.status && (
              <div className={`scene-tooltip-status status-text-${tooltipItem.status.toLowerCase()}`}>
                {tooltipItem.status}
              </div>
            )}
            {tooltipItem.altitudeKm !== undefined && <div>Altitude: {tooltipItem.altitudeKm.toFixed(0)} km</div>}
            {tooltipItem.band && <div>Band: {tooltipItem.band}</div>}
            {tooltipDistanceKm !== null && <div>Distance from selection: {tooltipDistanceKm.toFixed(0)} km</div>}
          </div>
        </Html>
      )}
    </>
  );
}

export { NEAR_COLOR, MEDIUM_COLOR, FAR_COLOR };
