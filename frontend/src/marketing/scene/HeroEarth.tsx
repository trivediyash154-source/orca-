import { useFrame } from '@react-three/fiber';
import { useMemo, useRef } from 'react';
import { AdditiveBlending, BackSide, type Mesh } from 'three';

import { SCENE_COLORS } from './spectral';
import { getEarthCloudTexture, getEarthDayTexture } from '../../visualization/earth/earthTexture';

/**
 * Earth for the entry surface.
 *
 * `src/visualization/earth/Earth.tsx` is deliberately NOT reused: it reads `EngineContext` for
 * simulation time and `UIContext` for the cloud/atmosphere toggles, and neither provider is
 * mounted on the landing page — the landing page never talks to the backend. What IS reused is
 * the part that matters, the procedurally painted textures in `earthTexture.ts`, so the planet
 * on the marketing surface is literally the same planet the workspace renders rather than a
 * second, divergent globe.
 *
 * Radius is exactly 1 scene unit, which is the project's convention rather than a choice:
 * `coordinates.ts`'s `SCENE_SCALE_KM` IS Earth's radius, so anything the mission layer places
 * at a real altitude sits at its true ratio to the planet without a second scale factor.
 */

export interface HeroEarthProps {
  readonly reducedMotion: boolean;
  /** Sphere tessellation. The mobile tier drops this: at 128 the surface sphere alone is
   * ~32k triangles, which is free on a discrete GPU and is not free on a phone that is also
   * being asked to composite a full-bleed canvas at 3x pixel density. */
  readonly segments?: number;
}

/** Radians per second of wall clock. Earth's real sidereal rate (7.29e-5 rad/s) is
 * imperceptible over the seconds a reader spends on a hero, and this surface has no simulation
 * clock to derive an angle from, so the rate here is openly a presentation choice: slow enough
 * that the globe never reads as a spinning logo, fast enough that it is visibly alive. */
const ROTATION_RAD_PER_SEC = 0.026;

/** Clouds lead the surface slightly. The offset is what separates the two shells optically;
 * without it they read as one painted sphere. Same 1.15 factor the workspace globe uses. */
const CLOUD_RATE_FACTOR = 1.15;

/** Where the globe starts. Chosen so the Atlantic and the west African coast face the key
 * light in the hero's opening frame — a framing decision, not a claim about a date. */
const START_ANGLE_RAD = 2.34;

/**
 * A backgrounded tab hands the next `useFrame` a delta covering the whole time it was hidden.
 * Multiplying by that produces a visible jump the moment the reader returns. Capping at ~3
 * frames of 60 Hz turns the jump into a resume: the scene is a presentation, so continuity of
 * appearance matters and continuity of angle does not.
 */
const MAX_FRAME_DELTA_SEC = 0.05;

export function HeroEarth({ reducedMotion, segments = 128 }: HeroEarthProps) {
  const dayTexture = useMemo(() => getEarthDayTexture(), []);
  const cloudTexture = useMemo(() => getEarthCloudTexture(), []);

  const surfaceRef = useRef<Mesh>(null);
  const cloudRef = useRef<Mesh>(null);
  const graticuleRef = useRef<Mesh>(null);

  // Angle is accumulated, not derived from elapsed time, so the rate is frame-rate independent
  // AND a paused frame loop (reduced motion, hidden tab, scrolled out of view) resumes from
  // where it stopped instead of snapping to wherever the clock has run to.
  const angleRef = useRef(START_ANGLE_RAD);

  useFrame((_state, delta) => {
    if (!reducedMotion) {
      angleRef.current += Math.min(delta, MAX_FRAME_DELTA_SEC) * ROTATION_RAD_PER_SEC;
    }
    const angle = angleRef.current;
    if (surfaceRef.current) surfaceRef.current.rotation.y = angle;
    if (graticuleRef.current) graticuleRef.current.rotation.y = angle;
    if (cloudRef.current) cloudRef.current.rotation.y = angle * CLOUD_RATE_FACTOR;
  });

  // Cheaper shells for the parts that carry no texture detail: the graticule is a wireframe
  // and the atmosphere is a flat additive wash, so tessellating either at surface resolution
  // buys nothing.
  const cloudSegments = Math.max(32, Math.round(segments / 2));
  const haloSegments = Math.max(24, Math.round(segments / 3));

  return (
    <group>
      {/* Surface. `emissiveMap` is the day texture itself, tinted by a very dark navy: the
          night hemisphere then shows a faint earthshine version of its own geography instead
          of going to pure black, which against a near-black ground would erase the planet's
          silhouette entirely. The terminator is real directional shading, not a painted band. */}
      <mesh ref={surfaceRef}>
        <sphereGeometry args={[1, segments, segments]} />
        <meshStandardMaterial
          map={dayTexture}
          emissive={SCENE_COLORS.earthNight}
          emissiveMap={dayTexture}
          emissiveIntensity={1}
          roughness={0.86}
          metalness={0.02}
        />
      </mesh>

      <mesh ref={cloudRef}>
        <sphereGeometry args={[1.012, cloudSegments, cloudSegments]} />
        <meshStandardMaterial
          map={cloudTexture}
          transparent
          opacity={0.38}
          depthWrite={false}
          roughness={1}
        />
      </mesh>

      {/* Graticule — an orientation cue at the threshold of visibility. Any more contrast and
          it reads as a wireframe globe illustration rather than as an instrument overlay. */}
      <mesh ref={graticuleRef}>
        <sphereGeometry args={[1.003, 24, 16]} />
        <meshBasicMaterial
          color={SCENE_COLORS.earthGraticule}
          wireframe
          transparent
          opacity={0.045}
          depthWrite={false}
        />
      </mesh>

      {/* Two back-side additive shells give the limb. Back-side means only the far hemisphere
          of each shell is drawn, so the glow accumulates around the silhouette and stays out
          of the lit disc — the standard cheap atmosphere, and the reason no shader is needed. */}
      <mesh>
        <sphereGeometry args={[1.024, haloSegments * 2, haloSegments * 2]} />
        <meshBasicMaterial
          color={SCENE_COLORS.earthRim}
          transparent
          opacity={0.15}
          side={BackSide}
          blending={AdditiveBlending}
          depthWrite={false}
        />
      </mesh>
      <mesh>
        <sphereGeometry args={[1.082, haloSegments, haloSegments]} />
        <meshBasicMaterial
          color={SCENE_COLORS.earthHalo}
          transparent
          opacity={0.062}
          side={BackSide}
          blending={AdditiveBlending}
          depthWrite={false}
        />
      </mesh>
      {/* A third, much fainter shell. A single additive shell terminates at its own radius
          with a hard circular edge, which reads as a grey collar around the planet rather
          than as atmosphere; stacking a wider, dimmer one turns that step into a falloff. */}
      <mesh>
        <sphereGeometry args={[1.19, haloSegments, haloSegments]} />
        <meshBasicMaterial
          color={SCENE_COLORS.earthHalo}
          transparent
          opacity={0.024}
          side={BackSide}
          blending={AdditiveBlending}
          depthWrite={false}
        />
      </mesh>
    </group>
  );
}
