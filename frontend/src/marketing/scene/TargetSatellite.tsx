import { useFrame } from '@react-three/fiber';
import { useLayoutEffect, useRef } from 'react';
import { AdditiveBlending, Group, MeshBasicMaterial } from 'three';

import { SolarWing } from './RescueVehicle';
import { sceneColor, type MissionFrame } from './heroOrbits';

const CRAFT_SCALE = 0.036;

/** Tumble rates, rad per second of scenario time. Deliberately incommensurate, and slow
 * enough to read as an uncontrolled body rather than as a spinning prop. */
const TUMBLE_PITCH = 0.116;
const TUMBLE_ROLL = 0.047;

/** The attitude held when motion is reduced. Not the identity: a satellite frozen square to
 * its track would read as healthy, and the failure has to stay legible without animation. */
const STATIC_PITCH = 0.62;
const STATIC_ROLL = 0.41;

export interface TargetSatelliteProps {
  readonly frame: MissionFrame;
  readonly reducedMotion: boolean;
}

/**
 * The casualty. Everything about it is the rescue vehicle's silhouette gone wrong, and the
 * contrast is the story the scene is telling before a single word of copy is read:
 *
 * - arrays canted well off sun-pointing, and canted by DIFFERENT amounts on each side, which
 *   is what a stuck drive looks like rather than a commanded slew;
 * - one array short a panel;
 * - a slow tumble about two axes, so no face holds toward Earth;
 * - a strobing red-amber fault beacon where the vehicle carries a steady cool status light.
 *
 * Its position is still real: it rides `TARGET_ORBIT` at the scenario's compressed rate like
 * everything else here. Only the attitude is failure theatre, and no figure on the page is
 * derived from it.
 */
export function TargetSatellite({ frame, reducedMotion }: TargetSatelliteProps) {
  const groupRef = useRef<Group>(null);
  const tumbleRef = useRef<Group>(null);
  const faultMaterialRef = useRef<MeshBasicMaterial>(null);

  // `targetVehicle` is the palette's neutral for an intact object: "an intact object is not
  // an event". The casualty keeps it, and everything else about it is dimmed a step — dead
  // arrays generate nothing — so the single element carrying `targetDistress` is the only
  // thing on this craft with any colour at all.
  const hull = sceneColor('targetVehicle', 0.99);
  // Grey cells against the vehicle's saturated blue ones: the arrays are still THERE, the
  // colour has gone out of them. Dropping them to the darkest neutral instead would have
  // hidden the cant, and the cant is the thing that says "not being steered".
  const panel = sceneColor('debrisNear', 0.99);
  const boom = sceneColor('debrisFar', 0.99);
  const dish = sceneColor('debrisNear', 0.99);
  const fault = sceneColor('targetDistress', 0.72);

  /** Position from the shared frame; attitude from elapsed scenario time rather than an
   * accumulator, so a dropped frame cannot make the tumble drift and reduced motion holds a
   * fixed pose for free. */
  function applyPose(): void {
    const group = groupRef.current;
    const tumble = tumbleRef.current;
    if (group) group.position.copy(frame.target);
    if (tumble) {
      const elapsed = reducedMotion ? 0 : frame.elapsed;
      tumble.rotation.x = STATIC_PITCH + elapsed * TUMBLE_PITCH;
      tumble.rotation.z = STATIC_ROLL + elapsed * TUMBLE_ROLL;
    }
  }

  useLayoutEffect(applyPose);

  useFrame(() => {
    applyPose();
    if (reducedMotion || !faultMaterialRef.current) return;
    // A strobe, not a glow: the eighth power collapses the sine into a short, hard flash on a
    // dark duty cycle, which is what a distress beacon looks like and what a healthy status
    // light does not.
    const strobe = Math.pow(Math.max(0, Math.sin(frame.elapsed * 2.6)), 8);
    faultMaterialRef.current.opacity = 0.16 + 0.84 * strobe;
  });

  return (
    <group ref={groupRef}>
      <group ref={tumbleRef}>
        <group scale={CRAFT_SCALE}>
          <mesh>
            <boxGeometry args={[0.68, 0.62, 0.72]} />
            <meshStandardMaterial
              color={hull}
              emissive={hull}
              emissiveIntensity={0.2}
              roughness={0.62}
              metalness={0.48}
            />
          </mesh>

          {/* Communications dish, pointing at nothing in particular — the antenna is still
              there, the pointing is not. */}
          <mesh position={[0.16, 0.44, -0.2]} rotation-x={-0.9} rotation-z={0.5}>
            <cylinderGeometry args={[0.28, 0.05, 0.18, 16, 1, true]} />
            <meshStandardMaterial
              color={dish}
              emissive={dish}
              emissiveIntensity={0.18}
              roughness={0.55}
              metalness={0.35}
              transparent
              opacity={0.85}
            />
          </mesh>

          {/* Asymmetric arrays: different cant per side, and one panel fewer to starboard. */}
          <SolarWing side={1} panelCount={3} panelColor={panel} boomColor={boom} cantRad={0.74} />
          <SolarWing side={-1} panelCount={2} panelColor={panel} boomColor={boom} cantRad={-1.18} />

          <mesh position={[0, 0.38, 0.24]}>
            <sphereGeometry args={[0.105, 12, 12]} />
            <meshBasicMaterial
              ref={faultMaterialRef}
              color={fault}
              transparent
              opacity={0.9}
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
