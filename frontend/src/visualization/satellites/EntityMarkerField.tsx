import { Html } from '@react-three/drei';
import { useFrame, type ThreeEvent } from '@react-three/fiber';
import { useEffect, useMemo, useRef, useState } from 'react';
import {
  AdditiveBlending,
  BufferAttribute,
  Color,
  DoubleSide,
  InstancedMesh,
  Mesh,
  Object3D,
  type Points,
} from 'three';

import { eciToSceneExaggerated } from '../coordinates';
import { useUI } from '../../state/UIContext';
import type { HealthView, PositionView } from '../../types/engine';
import { CRITICAL_STATUSES, DEFAULT_MARKER_COLOR, STATUS_COLORS } from './markerColors';
import { getMarkerGlowTexture } from './markerGlowTexture';

export interface EntityFieldItem {
  readonly id: string;
  readonly label: string;
  readonly status: string;
  readonly position: PositionView;
  readonly altitudeKm?: number;
  readonly fuelFraction?: number;
  readonly health?: HealthView;
  readonly deltaVRemainingKms?: number;
}

export interface EntityMarkerFieldProps {
  readonly items: readonly EntityFieldItem[];
  readonly kind: 'satellite' | 'vehicle';
  readonly selectedId: string | null;
  readonly onSelect: (id: string) => void;
  /** Documented altitude-exaggeration visual transform (`coordinates.ts`) -- display only,
   * so real planning entities sit on the same visual shells as the constellation around
   * them rather than floating at a different radius. */
  readonly exaggeration?: number;
}

// These are the engine's REAL planning entities, so they must stay findable inside the much
// denser synthetic constellation around them -- but sizing tuned for a 3-object scene made
// them render as giant blobs once thousands of neighbours appeared (caught in a real browser
// screenshot). They are now only ~3x a constellation object's radius and rely on their status
// colour + glow halo + selection ring to stand out, rather than on raw size.
const BASE_RADIUS = { satellite: 0.022, vehicle: 0.028 } as const;
const dummy = new Object3D();
const tmpColor = new Color();

/** Every marker's glow halo shares one `THREE.Points` draw call per kind -- a purely visual,
 * non-interactive layer sitting behind the real interactive `InstancedMesh` bodies below.
 * `THREE.Points` billboard the camera for free (no per-instance rotation math needed, unlike
 * `InstancedMesh`), which is what makes a soft, always-facing glow around a tiny marker cheap
 * at hundreds of objects (`docs/FINAL_BENCHMARK_REPORT.md` §6's instancing measurement still
 * applies -- this adds exactly one more draw call per kind, not one per object). */
function MarkerGlowField({
  items,
  scenePositions,
  glowSize,
}: {
  items: readonly EntityFieldItem[];
  scenePositions: readonly (readonly [number, number, number])[];
  glowSize: number;
}) {
  const pointsRef = useRef<Points>(null);
  const texture = useMemo(() => getMarkerGlowTexture(), []);

  const { positions, colors } = useMemo(
    () => ({
      positions: new Float32Array(Math.max(items.length, 1) * 3),
      colors: new Float32Array(Math.max(items.length, 1) * 3),
    }),
    [items.length],
  );

  useEffect(() => {
    items.forEach((item, i) => {
      const [x, y, z] = scenePositions[i]!;
      positions[i * 3] = x;
      positions[i * 3 + 1] = y;
      positions[i * 3 + 2] = z;
      tmpColor.set(STATUS_COLORS[item.status] ?? DEFAULT_MARKER_COLOR);
      colors[i * 3] = tmpColor.r;
      colors[i * 3 + 1] = tmpColor.g;
      colors[i * 3 + 2] = tmpColor.b;
    });
    const geometry = pointsRef.current?.geometry;
    if (!geometry) return;
    (geometry.getAttribute('position') as BufferAttribute).needsUpdate = true;
    (geometry.getAttribute('color') as BufferAttribute).needsUpdate = true;
  }, [items, scenePositions, positions, colors]);

  if (items.length === 0) return null;

  return (
    <points ref={pointsRef} raycast={() => null}>
      <bufferGeometry>
        <bufferAttribute attach="attributes-position" args={[positions, 3]} />
        <bufferAttribute attach="attributes-color" args={[colors, 3]} />
      </bufferGeometry>
      <pointsMaterial
        map={texture}
        size={glowSize}
        vertexColors
        transparent
        opacity={0.55}
        depthWrite={false}
        blending={AdditiveBlending}
        sizeAttenuation
      />
    </points>
  );
}

/** A subtle expanding ring for a critical/failed entity -- brief §20: "pulse, display a
 * subtle expanding ring... do not continuously flash the whole screen." One ring per
 * critical entity (there are only ever a handful of live emergencies at once, never one per
 * scenario object), so this stays outside the instanced field entirely. */
function EmergencyPulseRing({
  scenePos,
  radius,
}: {
  scenePos: readonly [number, number, number];
  radius: number;
}) {
  const ref = useRef<Mesh>(null);
  useFrame(({ clock }) => {
    if (!ref.current) return;
    const t = (clock.elapsedTime % 1.6) / 1.6;
    ref.current.scale.setScalar(1 + t * 2.2);
    const material = ref.current.material as { opacity: number };
    material.opacity = 0.45 * (1 - t);
  });
  return (
    <mesh ref={ref} position={scenePos} rotation={[Math.PI / 2, 0, 0]}>
      <ringGeometry args={[radius, radius * 1.3, 32]} />
      <meshBasicMaterial color="#d6544a" transparent opacity={0.4} />
    </mesh>
  );
}

/** Every satellite (or every vehicle) rendered as ONE `InstancedMesh` -- Phase 11 brief
 * §32-35's visualization-scaling requirement: at scenario scale (hundreds of objects,
 * `docs/FINAL_BENCHMARK_REPORT.md`'s measured comparison), one draw call per kind replaces
 * one draw call per object. Selection halo, the emergency pulse ring, and the hover/select
 * tooltip stay as small, separate, non-instanced elements -- there is only ever one selected
 * item and a handful of critical ones at once, so instancing them would add complexity for
 * no measurable benefit (`ARCHITECTURE.md`'s "no premature optimization" cuts the other way
 * here too: only the O(N) part is worth the instancing complexity). */
export function EntityMarkerField({ items, kind, selectedId, onSelect, exaggeration = 1 }: EntityMarkerFieldProps) {
  const meshRef = useRef<InstancedMesh>(null);
  const [hoveredIndex, setHoveredIndex] = useState<number | null>(null);
  const { reducedMotion } = useUI();
  const radius = BASE_RADIUS[kind];

  const scenePositions = useMemo(
    () => items.map((item) => eciToSceneExaggerated(item.position, exaggeration)),
    [items, exaggeration],
  );

  useEffect(() => {
    const mesh = meshRef.current;
    if (!mesh) return;
    items.forEach((item, i) => {
      const [x, y, z] = scenePositions[i]!;
      const isSelected = item.id === selectedId;
      const isHovered = i === hoveredIndex;
      const scale = isSelected ? 1.7 : isHovered ? 1.5 : 1;
      dummy.position.set(x, y, z);
      dummy.scale.setScalar(scale);
      dummy.updateMatrix();
      mesh.setMatrixAt(i, dummy.matrix);
      tmpColor.set(STATUS_COLORS[item.status] ?? DEFAULT_MARKER_COLOR);
      if (isSelected || isHovered) tmpColor.lerp(new Color('#ffffff'), 0.35);
      mesh.setColorAt(i, tmpColor);
    });
    mesh.instanceMatrix.needsUpdate = true;
    if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
    mesh.count = items.length;
  }, [items, scenePositions, selectedId, hoveredIndex]);

  const handleClick = (e: ThreeEvent<MouseEvent>) => {
    e.stopPropagation();
    if (e.instanceId === undefined) return;
    const item = items[e.instanceId];
    if (item) onSelect(item.id);
  };

  const handlePointerMove = (e: ThreeEvent<PointerEvent>) => {
    e.stopPropagation();
    setHoveredIndex(e.instanceId ?? null);
  };

  const hoveredItem = hoveredIndex !== null ? items[hoveredIndex] : undefined;
  const selectedIndex = selectedId !== null ? items.findIndex((it) => it.id === selectedId) : -1;
  const selectedItem = selectedIndex >= 0 ? items[selectedIndex] : undefined;
  const tooltipItem = hoveredItem ?? selectedItem;
  const tooltipPos = hoveredItem
    ? scenePositions[hoveredIndex!]
    : selectedIndex >= 0
      ? scenePositions[selectedIndex]
      : undefined;

  if (items.length === 0) return null;

  return (
    <>
      <MarkerGlowField items={items} scenePositions={scenePositions} glowSize={radius * 4.5} />

      <instancedMesh
        ref={meshRef}
        args={[undefined, undefined, items.length]}
        onClick={handleClick}
        onPointerMove={handlePointerMove}
        onPointerOut={() => setHoveredIndex(null)}
      >
        {kind === 'vehicle' ? (
          <octahedronGeometry args={[radius, 0]} />
        ) : (
          <sphereGeometry args={[radius, 12, 12]} />
        )}
        {/* `MeshBasicMaterial` deliberately, not `MeshStandardMaterial` -- a UI marker needs
            its real status color (via `InstancedMesh.setColorAt()`, multiplied into this
            material's base color automatically) to stay clearly readable regardless of
            scene lighting, not PBR-shaded. An `emissive` channel here would NOT pick up the
            per-instance color at all (emissive isn't tinted by `instanceColor`), which is
            why Phase 11's version washed every marker to the same white regardless of
            status once brightened. */}
        <meshBasicMaterial toneMapped={false} />
      </instancedMesh>

      {selectedItem && (
        <mesh position={tooltipPos} rotation={[Math.PI / 2, 0, 0]}>
          <ringGeometry args={[radius * 1.8, radius * 2.3, 32]} />
          <meshBasicMaterial color="#e6ecf5" transparent opacity={0.85} side={DoubleSide} />
        </mesh>
      )}

      {!reducedMotion &&
        items
          .filter((item) => CRITICAL_STATUSES.has(item.status))
          .map((item) => {
            const idx = items.indexOf(item);
            return <EmergencyPulseRing key={item.id} scenePos={scenePositions[idx]!} radius={radius} />;
          })}

      {tooltipItem && tooltipPos && (
        <Html position={tooltipPos} distanceFactor={8} style={{ pointerEvents: 'none' }}>
          <div className="scene-tooltip">
            <div className="scene-tooltip-title">{tooltipItem.label}</div>
            <div className={`scene-tooltip-status status-text-${tooltipItem.status.toLowerCase()}`}>
              {tooltipItem.status}
            </div>
            {tooltipItem.altitudeKm !== undefined && <div>Altitude: {tooltipItem.altitudeKm.toFixed(0)} km</div>}
            {tooltipItem.fuelFraction !== undefined && (
              <div>Fuel: {(tooltipItem.fuelFraction * 100).toFixed(0)}%</div>
            )}
            {tooltipItem.deltaVRemainingKms !== undefined && (
              <div>Δv remaining: {tooltipItem.deltaVRemainingKms.toFixed(2)} km/s</div>
            )}
            {tooltipItem.health && <div>Battery: {(tooltipItem.health.power * 100).toFixed(0)}%</div>}
          </div>
        </Html>
      )}
    </>
  );
}
