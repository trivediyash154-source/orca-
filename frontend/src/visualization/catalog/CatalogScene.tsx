/**
 * The catalogue scene: Earth, the point field, and the selected object's real orbit.
 *
 * Composition only — every piece of orbital truth is computed elsewhere (`catalog/propagation`,
 * `earthRotation`, `sun`) so this file can be read as "what is in the picture" rather than
 * "how the picture is derived".
 */

import { Line, OrbitControls, Stars } from '@react-three/drei';
import { Canvas, useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef, useState } from 'react';
import { type DirectionalLight } from 'three';

import { OrcaEarth } from '../earth/OrcaEarth';
import { CatalogPointField } from './CatalogPointField';
import { colorFor, type CatalogClassName, type RelevanceName } from './catalogColors';
import { sunDirectionScene } from './sun';
import { compileElements, traceOrbit } from '../../catalog/propagation';
import type { CatalogElement } from '../../catalog/types';
import type { SimClockHandle } from '../../state/SimClockContext';
import { useTheme } from '../../state/ThemeContext';

/** Places the key light along the real solar direction for the displayed instant. */
function SunLight({ clock }: { readonly clock: React.MutableRefObject<SimClockHandle> }) {
  const ref = useRef<DirectionalLight>(null);
  useFrame(() => {
    const [x, y, z] = sunDirectionScene(new Date(clock.current.epochMs));
    // Distance is arbitrary for a directional light; only the direction matters. Ten units
    // keeps it clear of the scene's own geometry.
    ref.current?.position.set(x * 10, y * 10, z * 10);
  });
  return <directionalLight ref={ref} intensity={2.2} color="#fff6e8" />;
}

/**
 * The selected object's orbit, traced by propagating it across one full period — the path SGP4
 * says it actually follows, not a circle drawn at its mean altitude. An eccentric orbit
 * therefore looks eccentric, and a regressing plane visibly regresses as the clock advances.
 */
function SelectedOrbit({
  element,
  clock,
}: {
  readonly element: CatalogElement | null;
  readonly clock: React.MutableRefObject<SimClockHandle>;
}) {
  const satrec = useMemo(() => {
    if (!element) return null;
    return compileElements([element])[0]?.satrec ?? null;
  }, [element]);

  const [points, setPoints] = useState<Array<[number, number, number]>>([]);

  // Retracing on every frame would be 180 SGP4 evaluations per frame for no visible gain: the
  // orbit's shape changes over minutes, not milliseconds. Re-tracing every two seconds of
  // simulated time is frequent enough to show a plane visibly regressing under time
  // acceleration, and rare enough that the setState here is not a per-frame re-render.
  const lastTraceMs = useRef(Number.NEGATIVE_INFINITY);
  useFrame(() => {
    if (!satrec || !element) return;
    const nowMs = clock.current.epochMs;
    if (Math.abs(nowMs - lastTraceMs.current) < 2000) return;
    lastTraceMs.current = nowMs;

    const path = traceOrbit(satrec, new Date(nowMs), element.pmin, 180);
    const next: Array<[number, number, number]> = [];
    for (let i = 0; i < path.length; i += 3) {
      const x = path[i];
      const y = path[i + 1];
      const z = path[i + 2];
      // A sample SGP4 could not produce is dropped rather than drawn at the origin, which
      // would put a spike through the middle of the planet.
      if (x === undefined || y === undefined || z === undefined || !Number.isFinite(x)) continue;
      next.push([x, y, z]);
    }
    setPoints(next);
  });

  // Reset when the selection changes, so the previous object's orbit does not linger for the
  // two seconds before the next trace.
  useEffect(() => {
    setPoints([]);
    lastTraceMs.current = Number.NEGATIVE_INFINITY;
  }, [element?.id]);

  if (!element || !satrec || points.length < 2) return null;
  const color = colorFor(element.cls as CatalogClassName, element.rel as RelevanceName);

  // drei's `Line`, matching `visualization/orbit/OrbitRing.tsx`. The bare `<line>` intrinsic
  // resolves to SVG's element in TSX, not to Three's.
  return <Line points={points} color={color} lineWidth={1.6} transparent opacity={0.8} />;
}

export interface CatalogSceneProps {
  readonly elements: readonly CatalogElement[];
  readonly clock: React.MutableRefObject<SimClockHandle>;
  readonly selectedId: number | null;
  readonly hoveredId: number | null;
  readonly onSelect: (id: number | null) => void;
  readonly onHover: (id: number | null) => void;
  readonly reducedMotion?: boolean;
  readonly showStarfield?: boolean;
  readonly onPropagationResult?: (ok: number, failed: number) => void;
}

/**
 * The space around the globe. Dark theme keeps it near-black so the planet carries the contrast.
 * Light theme uses a deep blue-slate rather than white: a white void behind an orbital scene
 * removes every depth cue the starfield and the atmospheric limb provide, and the result reads
 * as a diagram rather than as a view from orbit. The PAGE around the panel goes pale (see
 * `catalog.css`); the scene panel itself stays a window onto space.
 */
const SCENE_BACKGROUND: Readonly<Record<'dark' | 'light', string>> = {
  dark: '#04060c',
  light: '#101d31',
};

export function CatalogScene({
  elements,
  clock,
  selectedId,
  hoveredId,
  onSelect,
  onHover,
  reducedMotion = false,
  showStarfield = true,
  onPropagationResult,
}: CatalogSceneProps) {
  const { theme } = useTheme();
  const background = SCENE_BACKGROUND[theme];
  const selectedElement = useMemo(
    () => elements.find((e) => e.id === selectedId) ?? null,
    [elements, selectedId],
  );

  return (
    <Canvas
      camera={{ position: [0, 1.5, 3.6], fov: 50 }}
      style={{ background }}
      // Capped so a high-DPI display does not quadruple the fragment cost of a full-screen
      // scene for a point cloud that gains nothing from it.
      dpr={[1, 2]}
    >
      <fog attach="fog" args={[background, 12, 55]} />
      {/* These lights reach the point field and the orbit ribbons; the globe has its own
          shader and is graded by theme in `earthMaterial.ts` instead. */}
      <ambientLight intensity={theme === 'light' ? 0.5 : 0.34} />
      <SunLight clock={clock} />
      {/* A cool fill from the anti-solar side, so the night hemisphere reads as a sphere
          rather than a void. Low intensity: it must not compete with the real terminator. */}
      <pointLight position={[-6, -2, -5]} intensity={0.22} color="#4fa8ff" />

      {showStarfield && <Stars radius={140} depth={70} count={4500} factor={3} fade />}

      <OrcaEarth
        getEpochMs={() => clock.current.epochMs}
        reducedMotion={reducedMotion}
        theme={theme}
      />

      <CatalogPointField
        elements={elements}
        clock={clock}
        selectedId={selectedId}
        hoveredId={hoveredId}
        onSelect={onSelect}
        onHover={onHover}
        reducedMotion={reducedMotion}
        onPropagationResult={onPropagationResult}
      />

      <SelectedOrbit element={selectedElement} clock={clock} />

      <OrbitControls enablePan minDistance={1.15} maxDistance={30} />
    </Canvas>
  );
}
