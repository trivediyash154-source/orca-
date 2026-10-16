import { useFrame } from '@react-three/fiber';
import { useLayoutEffect, useMemo, useRef } from 'react';
import { AdditiveBlending, Color, DoubleSide, InstancedMesh, Object3D } from 'three';

import { OrbitTrack } from './OrbitTrack';
import { RescueVehicle } from './RescueVehicle';
import { SensorSweep } from './SensorSweep';
import { SpectralTransferArc } from './SpectralTransferArc';
import { TargetSatellite } from './TargetSatellite';
import {
  AUTH_BACKDROP_ORBITS,
  BACKDROP_ORBITS,
  HERO_EXAGGERATION,
  RESCUE_ORBIT,
  TARGET_ORBIT,
  advanceMissionFrame,
  advanceSolveState,
  createMissionFrame,
  createSolveState,
  sceneColor,
  writeTransferPoint,
  type MissionFrame,
  type SolveState,
} from './heroOrbits';

/** Where along the transfer the route's nodes sit. These are waypoints on an illustrative
 * arc, not graph nodes from the engine's mission graph — the workspace draws those. */
const HERO_WAYPOINTS: readonly number[] = [0.17, 0.39, 0.61, 0.83];
const AUTH_WAYPOINTS: readonly number[] = [0.34, 0.68];

const WAYPOINT_RADIUS = 0.019;

const instanceProxy = new Object3D();
const waypointTint = new Color();
/** Parsed once at module load: `Color` construction is a string parse plus an sRGB
 * conversion, and this value is read on every marker on every frame. */
const waypointBase = new Color(sceneColor('waypoint', 1));

interface WaypointMarkersProps {
  readonly frame: MissionFrame;
  readonly solve: SolveState;
  readonly exaggeration: number;
  readonly positions: readonly number[];
}

/**
 * Route nodes on the adopted transfer.
 *
 * A waypoint only appears once the solving front has passed it, and flares briefly as it
 * does — so the sweep reads as a search reaching nodes in order rather than as a line being
 * wiped in. Diamond outlines rather than filled dots, because a filled marker at this size
 * competes with the two spacecraft for the eye.
 */
function WaypointMarkers({ frame, solve, exaggeration, positions }: WaypointMarkersProps) {
  const meshRef = useRef<InstancedMesh>(null);
  const count = positions.length;

  useLayoutEffect(() => {
    const mesh = meshRef.current;
    if (!mesh) return;
    instanceProxy.scale.setScalar(0);
    instanceProxy.updateMatrix();
    waypointTint.setRGB(0, 0, 0);
    for (let i = 0; i < count; i++) {
      mesh.setMatrixAt(i, instanceProxy.matrix);
      mesh.setColorAt(i, waypointTint);
    }
    mesh.instanceMatrix.needsUpdate = true;
    if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
  }, [count]);

  useFrame((state) => {
    const mesh = meshRef.current;
    if (!mesh) return;
    for (let i = 0; i < count; i++) {
      const s = positions[i] ?? 0;
      const passed = solve.progress - s;
      if (passed < 0) {
        instanceProxy.scale.setScalar(0);
        instanceProxy.updateMatrix();
        mesh.setMatrixAt(i, instanceProxy.matrix);
        continue;
      }
      writeTransferPoint(frame, solve.shape, s, exaggeration, instanceProxy.position);
      instanceProxy.quaternion.copy(state.camera.quaternion);
      const flare = Math.max(0, 1 - passed * 5.5);
      instanceProxy.scale.setScalar(WAYPOINT_RADIUS * (1 + flare * 1.35));
      instanceProxy.updateMatrix();
      mesh.setMatrixAt(i, instanceProxy.matrix);
      // One colour for every node, not a ramp sample per node: the ramp encodes progress
      // along the arc and the arc already carries it. A waypoint is a waypoint wherever it
      // sits, so it takes the palette's dedicated `waypoint` value and varies only in
      // brightness as the front passes.
      const intensity = solve.alpha * (0.42 + 0.58 * flare);
      waypointTint.setRGB(
        waypointBase.r * intensity,
        waypointBase.g * intensity,
        waypointBase.b * intensity,
      );
      mesh.setColorAt(i, waypointTint);
    }
    mesh.instanceMatrix.needsUpdate = true;
    if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
  });

  return (
    <instancedMesh
      ref={meshRef}
      args={[undefined, undefined, count]}
      frustumCulled={false}
      renderOrder={4}
    >
      {/* Four theta segments: a diamond outline, which reads as an instrument tick where a
          circle would read as another glow. */}
      <ringGeometry args={[0.6, 1, 4, 1]} />
      <meshBasicMaterial
        transparent
        opacity={0.95}
        blending={AdditiveBlending}
        depthWrite={false}
        side={DoubleSide}
        toneMapped={false}
      />
    </instancedMesh>
  );
}

export interface MissionLayerProps {
  readonly reducedMotion: boolean;
  readonly variant: 'hero' | 'auth';
}

/**
 * The scenario the entry surface exists to tell: a controlled rescue vehicle on a 520 km
 * orbit, a tumbling casualty at 786 km in a plane 58 degrees away, and a transfer between
 * them that is solved, held, and then solved again.
 *
 * This layer owns the two pieces of per-frame state its children share — the live pose
 * (`MissionFrame`) and the replanning cycle (`SolveState`) — and advances both in a single
 * `useFrame`. They are plain mutable objects rather than React state on purpose: the arc,
 * the beam and the waypoints all need the spacecraft's CURRENT positions sixty times a
 * second, and routing that through a render would cost more than everything else here
 * combined. Children read the same two objects, so they agree with each other by
 * construction and the arc terminates on the vehicles rather than near them.
 *
 * The `auth` variant tells the same story with less furniture: one backdrop orbit, two
 * waypoints, one scan ring, and no dominated alternatives — behind a sign-in form the
 * Pareto argument is made in words, not geometry.
 *
 * `ProbeRipples` is deliberately NOT mounted here: it belongs to the whole canvas rather than
 * to the mission, and `OrbitalHeroScene` mounts it as a sibling. Mounting it in both places
 * would put two ripples on every click.
 *
 * Nothing in this layer is engine output. The entry surface makes no backend call; the
 * orbits are real elements evaluated with the engine's own position formula, and everything
 * else — the transfer geometry, the solve cadence, the sensor beam — is presentation, said
 * plainly here and in the page's visible copy.
 */
export function MissionLayer({ reducedMotion, variant }: MissionLayerProps) {
  const exaggeration = HERO_EXAGGERATION;
  const frame = useMemo(() => createMissionFrame(exaggeration), [exaggeration]);
  const solve = useMemo(() => createSolveState(), []);

  const isAuth = variant === 'auth';
  const backdrop = isAuth ? AUTH_BACKDROP_ORBITS : BACKDROP_ORBITS;
  const waypoints = isAuth ? AUTH_WAYPOINTS : HERO_WAYPOINTS;

  // `spectral.ts` keeps orbit rings near-neutral on purpose, so that the transfer arc stays
  // the only saturated line in the frame. The one exception is the casualty's shell, which
  // carries `targetDistress` because "this is the orbit the emergency is on" is a fact worth
  // a colour — and it is a dashed hairline, not a second glowing band.
  const rescueTrack = sceneColor('orbitShell', 0.99);
  const targetTrack = sceneColor('targetDistress', 0.72);
  const backdropTrack = sceneColor('orbitShell', 0.99);

  useFrame((_, delta) => {
    // Clamped: a backgrounded tab returns one very large delta, and an unclamped step would
    // teleport both craft several minutes down their orbits in a single frame.
    const step = Math.min(delta, 0.05);
    if (!reducedMotion) advanceMissionFrame(frame, step, exaggeration);
    advanceSolveState(solve, step, reducedMotion);
  });

  return (
    <group>
      {backdrop.map((orbit) => (
        <OrbitTrack
          key={`${orbit.altitudeKm}-${orbit.raanDeg}`}
          orbit={orbit}
          exaggeration={exaggeration}
          color={backdropTrack}
          opacity={0.16}
          lineWidth={0.8}
          segments={96}
        />
      ))}

      <OrbitTrack
        orbit={RESCUE_ORBIT}
        exaggeration={exaggeration}
        color={rescueTrack}
        opacity={0.72}
        lineWidth={1.4}
      />
      {/* Dashed, because this is the orbit the mission has to REACH — the casualty is on it,
          the vehicle is not. */}
      <OrbitTrack
        orbit={TARGET_ORBIT}
        exaggeration={exaggeration}
        color={targetTrack}
        opacity={0.42}
        lineWidth={1.1}
        dashed
      />

      <SpectralTransferArc
        frame={frame}
        solve={solve}
        exaggeration={exaggeration}
        reducedMotion={reducedMotion}
        showAlternatives={!isAuth}
      />

      <WaypointMarkers
        frame={frame}
        solve={solve}
        exaggeration={exaggeration}
        positions={waypoints}
      />

      <SensorSweep frame={frame} reducedMotion={reducedMotion} ringCount={isAuth ? 1 : 2} />

      <RescueVehicle frame={frame} exaggeration={exaggeration} reducedMotion={reducedMotion} />
      <TargetSatellite frame={frame} reducedMotion={reducedMotion} />
    </group>
  );
}
