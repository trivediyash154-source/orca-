import { useFrame } from '@react-three/fiber';
import { useLayoutEffect, useMemo, useRef } from 'react';
import { AdditiveBlending, Group, MeshBasicMaterial, Vector3 } from 'three';

import {
  RESCUE_ORBIT,
  sceneColor,
  writeVelocityDirection,
  type MissionFrame,
} from './heroOrbits';

/**
 * Model space. Every dimension below is in "bus lengths"; one group scale converts the whole
 * craft to scene units at the end, so the proportions can be read and adjusted without
 * arithmetic against Earth's radius.
 */
const CRAFT_SCALE = 0.038;
/** Panels run LONG along the boom and short across it — an array that is wider than it is
 * long reads as a paddle. The pitch leaves a gap wide enough to survive being drawn at
 * thirty pixels, which is the size this craft actually renders at in the hero. */
const PANEL_WIDTH = 0.62;
const PANEL_DEPTH = 0.6;
const PANEL_PITCH = 0.74;
const PANEL_START = 0.95;
const BOOM_LENGTH = 0.66;

/** Spacecraft are drawn far above true scale — a 10 m vehicle at this scale would be
 * 1.6e-6 scene units, which is nothing. The workspace's `EntityMarkerField` makes the same
 * trade for the same reason; what stays true here is POSITION, not size. */

const forward = new Vector3();
const lookTarget = new Vector3();

export interface SolarWingProps {
  /** +1 or -1 along the boom axis. */
  readonly side: 1 | -1;
  readonly panelCount: number;
  readonly panelColor: string;
  readonly boomColor: string;
  /** Roll about the boom, radians. Zero is sun-pointing; a non-zero value is the visual
   * signature of an array that is no longer being steered. */
  readonly cantRad?: number;
}

/** One deployed array: a boom plus discrete panels with real gaps between them, because a
 * single unbroken slab reads as a paddle rather than as a segmented array.
 *
 * Exported because `TargetSatellite` builds its arrays from the same part. That is the point:
 * the casualty has to be recognisably the SAME class of hardware as the vehicle, or "this one
 * is broken" reads as "this one is a different object" — so the two craft share the component
 * and differ only in cant, panel count and colour. */
export function SolarWing({
  side,
  panelCount,
  panelColor,
  boomColor,
  cantRad = 0,
}: SolarWingProps) {
  const offsets = useMemo(
    () => Array.from({ length: panelCount }, (_, i) => PANEL_START + i * PANEL_PITCH),
    [panelCount],
  );

  return (
    <group rotation-x={cantRad}>
      <mesh position={[side * (BOOM_LENGTH / 2 + 0.28), 0, 0]} rotation-z={Math.PI / 2}>
        <cylinderGeometry args={[0.032, 0.032, BOOM_LENGTH, 8]} />
        <meshStandardMaterial
          color={boomColor}
          emissive={boomColor}
          emissiveIntensity={0.3}
          roughness={0.55}
          metalness={0.4}
        />
      </mesh>
      {offsets.map((offset) => (
        <mesh key={offset} position={[side * offset, 0, 0]}>
          <boxGeometry args={[PANEL_WIDTH, 0.026, PANEL_DEPTH]} />
          <meshStandardMaterial
            color={panelColor}
            emissive={panelColor}
            emissiveIntensity={0.34}
            roughness={0.32}
            metalness={0.55}
          />
        </mesh>
      ))}
    </group>
  );
}

export interface RescueVehicleProps {
  readonly frame: MissionFrame;
  readonly exaggeration: number;
  readonly reducedMotion: boolean;
}

/**
 * The healthy half of the scenario. It is under control and it says so without a caption:
 * arrays square to the sun, attitude locked along the velocity vector, a steady cyan status
 * light, and a live thruster plume.
 *
 * Attitude is derived from the analytic velocity direction at the vehicle's current phase
 * (`writeVelocityDirection`), so it genuinely faces along its own track. The MOTION along
 * that track is a constant angular rate at the orbit's real mean motion, compressed by one
 * documented constant — a presentation simplification. The workspace propagates real state
 * from the engine; this scene does not, and no number on the page is derived from it.
 */
export function RescueVehicle({ frame, exaggeration, reducedMotion }: RescueVehicleProps) {
  const groupRef = useRef<Group>(null);
  const plumeRef = useRef<Group>(null);
  const plumeMaterialRef = useRef<MeshBasicMaterial>(null);
  const beaconMaterialRef = useRef<MeshBasicMaterial>(null);

  // Palette keys are `spectral.ts`'s, which is the one module in this layer allowed to hold a
  // colour literal. The vehicle's identity colour (`rescueVehicle`, the ramp's departure stop)
  // is spent on the two elements that are unmistakably its own — its exhaust and its status
  // light — rather than painted over the whole hull, which would make a spacecraft read as a
  // toy and would put a saturated mass next to the one arc allowed to be saturated.
  const hull = sceneColor('debrisNear', 0.99);
  const panel = sceneColor('scanBeam', 0.2);
  const boom = sceneColor('debrisFar', 0.99);
  const dish = sceneColor('starlight', 0.99);
  const plume = sceneColor('rescueVehicle', 0);
  const healthy = sceneColor('rescueVehicle', 0);

  /** Applied from both `useFrame` and mount so the craft is correctly placed even if the
   * host canvas renders on demand rather than continuously. */
  function applyPose(): void {
    const group = groupRef.current;
    if (!group) return;
    group.position.copy(frame.rescue);
    writeVelocityDirection(RESCUE_ORBIT, frame.rescueTheta, exaggeration, forward);
    // The model is built nose-down -Z, which is what `lookAt` orients; world up resolves
    // roll, and an inclined orbit's velocity never aligns with it closely enough to spin.
    lookTarget.copy(frame.rescue).add(forward);
    group.lookAt(lookTarget);
  }

  useLayoutEffect(applyPose);

  useFrame(() => {
    applyPose();
    if (reducedMotion) return;
    // Throttle band of a station-keeping burn: enough variation to read as live, never
    // enough to flicker.
    const pulse = 0.78 + 0.22 * Math.sin(frame.elapsed * 6.1);
    if (plumeRef.current) plumeRef.current.scale.set(1, 1, pulse);
    if (plumeMaterialRef.current) plumeMaterialRef.current.opacity = 0.14 + 0.14 * pulse;
    if (beaconMaterialRef.current) {
      beaconMaterialRef.current.opacity = 0.72 + 0.28 * Math.sin(frame.elapsed * 2.4);
    }
  });

  return (
    <group ref={groupRef}>
      <group scale={CRAFT_SCALE}>
        {/* Service bus, long axis along the velocity vector. */}
        <mesh>
          <boxGeometry args={[0.6, 0.58, 1]} />
          <meshStandardMaterial
            color={hull}
            emissive={hull}
            emissiveIntensity={0.22}
            roughness={0.44}
            metalness={0.62}
          />
        </mesh>

        {/* Docking collar at the nose — the end that has to mate with the casualty. */}
        <mesh position={[0, 0, -0.58]} rotation-x={Math.PI / 2}>
          <cylinderGeometry args={[0.19, 0.24, 0.16, 12, 1, true]} />
          <meshStandardMaterial
            color={dish}
            emissive={dish}
            emissiveIntensity={0.34}
            roughness={0.4}
            metalness={0.5}
          />
        </mesh>

        {/* High-gain dish, nadir-pointing. */}
        <mesh position={[0, -0.42, 0.12]} rotation-x={Math.PI}>
          <cylinderGeometry args={[0.3, 0.05, 0.2, 16, 1, true]} />
          <meshStandardMaterial
            color={dish}
            emissive={dish}
            emissiveIntensity={0.3}
            roughness={0.38}
            metalness={0.44}
            transparent
            opacity={0.92}
          />
        </mesh>

        <SolarWing side={1} panelCount={3} panelColor={panel} boomColor={boom} />
        <SolarWing side={-1} panelCount={3} panelColor={panel} boomColor={boom} />

        {/* Status light: steady, cool, and single. A healthy craft does not need a light
            show — the contrast against the casualty's fault beacon is the whole signal. */}
        <mesh position={[0, 0.34, -0.3]}>
          <sphereGeometry args={[0.095, 12, 12]} />
          <meshBasicMaterial
            ref={beaconMaterialRef}
            color={healthy}
            transparent
            opacity={0.9}
            blending={AdditiveBlending}
            depthWrite={false}
            toneMapped={false}
          />
        </mesh>

        {/* Thruster plume, streaming aft. Additive and open-ended so it reads as exhaust
            rather than as a solid cone bolted to the tail. The pulse scales the OUTER group,
            whose origin sits on the nozzle, so the plume lengthens away from the engine
            instead of detaching from it. */}
        <group ref={plumeRef} position={[0, 0, 0.5]}>
          <mesh position={[0, 0, 0.31]} rotation-x={-Math.PI / 2}>
            <coneGeometry args={[0.15, 0.62, 14, 1, true]} />
            <meshBasicMaterial
              ref={plumeMaterialRef}
              color={plume}
              transparent
              opacity={0.24}
              blending={AdditiveBlending}
              depthWrite={false}
              toneMapped={false}
            />
          </mesh>
        </group>
      </group>
    </group>
  );
}
