import { useFrame } from '@react-three/fiber';
import { useMemo, useRef } from 'react';
import { AdditiveBlending, BackSide, type Mesh } from 'three';

import { getEarthCloudTexture, getEarthDayTexture } from './earthTexture';
import { useEngine } from '../../state/EngineContext';
import { useUI } from '../../state/UIContext';

/** One sidereal day in seconds -- Earth's real rotation period. The engine's own
 * `simulationTimeSec` is in the same units, so the surface angle below is a real derived
 * quantity, not an arbitrary animation speed. */
const SIDEREAL_DAY_SEC = 86164.0905;

/** Presentational idle drift (rad/s of wall-clock) applied ON TOP of the real sidereal
 * angle, so the globe still reads as alive while the simulation is paused at t=0. Explicitly
 * a display effect, not a physical claim -- and switchable off via Settings' "Earth
 * rotation", which leaves only the real sim-time term. */
const IDLE_DRIFT_RAD_PER_SEC = 0.03;

/** Earth — Phase 12: a recognizable planet, not a shaded ball. Surface texture is painted
 * from real (coarse) continent outlines (`continents.ts`/`earthTexture.ts`, no CDN, no
 * vendored image); a separate cloud shell rotates at a slightly different rate; two additive
 * atmosphere shells give the limb. Radius stays exactly 1 scene unit by construction
 * (`coordinates.ts`'s `SCENE_SCALE_KM` IS Earth's radius) -- the visualization never rescales
 * the planet, so every satellite altitude stays in its true ratio to it. */
export function Earth() {
  const dayTexture = useMemo(() => getEarthDayTexture(), []);
  const cloudTexture = useMemo(() => getEarthCloudTexture(), []);
  const surfaceRef = useRef<Mesh>(null);
  const cloudRef = useRef<Mesh>(null);
  const gridRef = useRef<Mesh>(null);
  const { snapshot } = useEngine();
  const { earthRotation, showClouds, showAtmosphere } = useUI();

  const simTimeSec = snapshot?.simulationTimeSec ?? 0;

  useFrame(({ clock }) => {
    const sidereal = (simTimeSec / SIDEREAL_DAY_SEC) * Math.PI * 2;
    const drift = earthRotation ? clock.elapsedTime * IDLE_DRIFT_RAD_PER_SEC : 0;
    const angle = sidereal + drift;
    if (surfaceRef.current) surfaceRef.current.rotation.y = angle;
    if (gridRef.current) gridRef.current.rotation.y = angle;
    // Clouds drift slightly faster than the surface -- a small, deliberate offset so the two
    // layers are visibly distinct rather than looking like one painted sphere.
    if (cloudRef.current) cloudRef.current.rotation.y = angle * 1.15;
  });

  return (
    <group>
      <mesh ref={surfaceRef}>
        <sphereGeometry args={[1, 128, 128]} />
        <meshStandardMaterial map={dayTexture} roughness={0.82} metalness={0.02} />
      </mesh>

      {showClouds && (
        <mesh ref={cloudRef}>
          <sphereGeometry args={[1.012, 64, 64]} />
          <meshStandardMaterial
            map={cloudTexture}
            transparent
            opacity={0.42}
            depthWrite={false}
            roughness={1}
          />
        </mesh>
      )}

      {/* Faint graticule -- a secondary orientation cue now that the surface carries real
          detail, not the only surface feature as in Phase 11. */}
      <mesh ref={gridRef}>
        <sphereGeometry args={[1.003, 24, 16]} />
        <meshBasicMaterial color="#cfe9ff" wireframe transparent opacity={0.05} />
      </mesh>

      {showAtmosphere && (
        <>
          {/* Inner limb -- tight and brighter. */}
          <mesh>
            <sphereGeometry args={[1.022, 64, 64]} />
            <meshBasicMaterial
              color="#7ec8ff"
              transparent
              opacity={0.16}
              side={BackSide}
              blending={AdditiveBlending}
              depthWrite={false}
            />
          </mesh>
          {/* Outer halo -- larger and softer, so Earth separates from a black background. */}
          <mesh>
            <sphereGeometry args={[1.09, 48, 48]} />
            <meshBasicMaterial
              color="#3d8fd6"
              transparent
              opacity={0.09}
              side={BackSide}
              blending={AdditiveBlending}
              depthWrite={false}
            />
          </mesh>
        </>
      )}
    </group>
  );
}
