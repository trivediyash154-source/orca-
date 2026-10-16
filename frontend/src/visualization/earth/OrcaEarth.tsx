/**
 * ORCA's Earth — the one planet every screen shares.
 *
 * ## Why this replaced the older `Earth.tsx`
 *
 * There were two Earths. This one is textured from real NASA imagery and shaded by a real
 * sun-direction terminator with night-side city lights; the older one painted coastlines onto a
 * canvas at runtime and was lit by a scene light, which is why Mission Control's globe looked
 * markedly worse than the catalogue's. Rather than improving the weaker one, both screens now
 * mount this component.
 *
 * It takes a TIME GETTER rather than a clock object precisely so it can serve both: the
 * catalogue drives it from `SimClockContext`, Mission Control from the C++ engine's own
 * `simulationTimeSec`. One planet, two clocks, no duplicated shader.
 *
 * ## Axial tilt
 *
 * There is no tilt applied to this mesh, and that is deliberate rather than an omission. The
 * scene frame is inertial and its +Y IS Earth's rotation axis (`coordinates.ts` maps the ECI
 * polar axis to scene "up"). The 23.4-degree obliquity is therefore already expressed — it
 * appears in the SUN's direction relative to that axis (`sun.ts`), which is what produces
 * seasons and a terminator that leans. Tilting the globe here as well would tilt the planet
 * away from the very axis its satellites orbit around, and every ground track would be wrong by
 * that angle.
 */

import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef, useState } from 'react';
import { type Mesh, type ShaderMaterial } from 'three';

import {
  createAtmosphereMaterial,
  createCloudMaterial,
  createEarthSurfaceMaterial,
  EARTH_GRADING,
  loadEarthTextures,
  type EarthTextureSet,
} from './orcaEarthMaterial';
import { earthRotationY } from '../catalog/earthRotation';
import { sunDirectionScene } from '../catalog/sun';
import type { ResolvedTheme } from '../../state/ThemeContext';

export interface OrcaEarthProps {
  /**
   * Returns the instant to render, as Unix milliseconds. A getter rather than a clock object so
   * this component is not tied to one screen's notion of time: the catalogue drives it from
   * `SimClockContext`, while Mission Control drives it from the C++ engine's own
   * `simulationTimeSec`. Both need the same planet; neither should have to adopt the other's
   * clock to get it.
   */
  readonly getEpochMs: () => number;
  readonly showClouds?: boolean;
  readonly showAtmosphere?: boolean;
  /**
   * Accepted for symmetry with the other scene layers but deliberately unused: Earth's
   * rotation is not decoration, it is the frame the satellite positions are expressed in.
   * Freezing it under reduced motion would put every object over the wrong place rather than
   * calming the picture. The clock's own Pause is the honest way to stop the globe.
   */
  readonly reducedMotion?: boolean;
  /** Selects the exposure grading; see `EARTH_GRADING` for why the two genuinely differ. */
  readonly theme?: ResolvedTheme;
  /** Reports whether real imagery was found, so the UI can state which surface it is showing. */
  readonly onTextureSource?: (usingRealImagery: boolean) => void;
}

/**
 * Clouds lead the surface slightly. Real prevailing winds are a few tens of m/s against a
 * surface moving at hundreds, so the offset is small on purpose — enough to separate the two
 * shells visually, not enough to imply a weather model that does not exist.
 */
const CLOUD_LEAD = 1.04;

export function OrcaEarth({
  getEpochMs,
  showClouds = true,
  showAtmosphere = true,
  theme = 'dark',
  onTextureSource,
}: OrcaEarthProps) {
  const [textures, setTextures] = useState<EarthTextureSet | null>(null);
  const surfaceRef = useRef<Mesh>(null);
  const cloudRef = useRef<Mesh>(null);
  const gridRef = useRef<Mesh>(null);

  useEffect(() => {
    let cancelled = false;
    void loadEarthTextures().then((set) => {
      if (cancelled) return;
      setTextures(set);
      onTextureSource?.(set.usingRealImagery);
    });
    return () => {
      cancelled = true;
    };
  }, [onTextureSource]);

  const grading = EARTH_GRADING[theme];

  const surfaceMaterial = useMemo(
    () => (textures ? createEarthSurfaceMaterial(textures, grading) : null),
    [textures, grading],
  );
  const cloudMaterial = useMemo(
    () => (textures ? createCloudMaterial(textures, grading) : null),
    [textures, grading],
  );
  // The limb is brighter in the light theme so the planet still separates from a pale page.
  const innerAtmosphere = useMemo(
    () => createAtmosphereMaterial(grading.atmosphereColor, theme === 'light' ? 1.15 : 0.95),
    [grading, theme],
  );
  const outerAtmosphere = useMemo(
    () => createAtmosphereMaterial(theme === 'light' ? '#5fa8ee' : '#3d8fd6', theme === 'light' ? 0.6 : 0.5),
    [theme],
  );

  // Shader materials hold GPU programs and uniform buffers that outlive the React tree.
  useEffect(() => () => surfaceMaterial?.dispose(), [surfaceMaterial]);
  useEffect(() => () => cloudMaterial?.dispose(), [cloudMaterial]);
  useEffect(() => () => innerAtmosphere.dispose(), [innerAtmosphere]);
  useEffect(() => () => outerAtmosphere.dispose(), [outerAtmosphere]);

  useFrame(() => {
    const date = new Date(getEpochMs());
    const angle = earthRotationY(date);
    if (surfaceRef.current) surfaceRef.current.rotation.y = angle;
    if (gridRef.current) gridRef.current.rotation.y = angle;
    if (cloudRef.current) cloudRef.current.rotation.y = angle * CLOUD_LEAD;

    // One shared direction, written into each material's existing vector rather than assigning
    // a new one — this runs every frame and must not allocate.
    const [sx, sy, sz] = sunDirectionScene(date);
    surfaceMaterial?.sunDirection.set(sx, sy, sz);
    const setSun = (m: ShaderMaterial) => {
      const u = m.uniforms.uSunDirection;
      if (u !== undefined) u.value.set(sx, sy, sz);
    };
    setSun(innerAtmosphere);
    setSun(outerAtmosphere);
    if (cloudMaterial !== null) setSun(cloudMaterial);
  });

  // Nothing is drawn until the textures resolve. A grey placeholder sphere would read as a
  // moon-like planet for the moment it was on screen, which is worse than empty space.
  if (!textures || !surfaceMaterial) return null;

  return (
    <group>
      <mesh ref={surfaceRef} material={surfaceMaterial}>
        <sphereGeometry args={[1, 128, 128]} />
      </mesh>

      {showClouds && cloudMaterial !== null && (
        <mesh ref={cloudRef} material={cloudMaterial}>
          <sphereGeometry args={[1.012, 64, 64]} />
        </mesh>
      )}

      <mesh ref={gridRef}>
        <sphereGeometry args={[1.003, 24, 16]} />
        <meshBasicMaterial color="#cfe9ff" wireframe transparent opacity={0.04} />
      </mesh>

      {showAtmosphere && (
        <>
          <mesh material={innerAtmosphere}>
            <sphereGeometry args={[1.025, 64, 64]} />
          </mesh>
          <mesh material={outerAtmosphere}>
            <sphereGeometry args={[1.11, 48, 48]} />
          </mesh>
        </>
      )}
    </group>
  );
}
