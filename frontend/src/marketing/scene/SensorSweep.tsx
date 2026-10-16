import { useFrame } from '@react-three/fiber';
import { useLayoutEffect, useRef } from 'react';
import { AdditiveBlending, DoubleSide, Group, MeshBasicMaterial, Vector3 } from 'three';

import { sceneColor, type MissionFrame } from './heroOrbits';

/** Cone half-width at the target, in scene units. Narrow on purpose: a wide cone reads as a
 * spotlight, and this is a ranging sensor, not stage lighting. */
const BEAM_RADIUS = 0.017;

/** Scan-ring radii in scene units. The casualty's own half-span is about 0.06, so a ring
 * running from below it to above it visibly passes ACROSS the satellite rather than
 * decorating a point near it. */
const RING_MIN = 0.022;
const RING_MAX = 0.088;
const RING_PERIOD_SEC = 2.6;

/** Axis conventions of the two primitives used here: a cone's axis is +Y, a ring's normal
 * is +Z. Both are rotated onto the beam direction rather than being rebuilt. */
const CONE_AXIS = new Vector3(0, 1, 0);
const RING_NORMAL = new Vector3(0, 0, 1);

const beamDirection = new Vector3();

export interface SensorSweepProps {
  readonly frame: MissionFrame;
  readonly reducedMotion: boolean;
  /** Rings per sweep. The authentication plate runs one; the landing hero runs two, offset
   * half a cycle apart, which is what makes the scan read as continuous. */
  readonly ringCount?: 1 | 2;
}

/**
 * The rescue vehicle ranging on the casualty.
 *
 * This is the one piece of the scene that is pure narrative: nothing in the engine models a
 * sensor cone, and no proximity or detection figure on the page comes from it. It earns its
 * place because it is what makes the two spacecraft read as ONE event rather than as two
 * objects that happen to share a frame — the vehicle is looking at the casualty, and the
 * viewer can see that it is.
 *
 * Both ends are taken from the shared frame every tick, so the beam tracks the two craft
 * exactly as they move rather than pointing at where they were when it was built.
 */
export function SensorSweep({ frame, reducedMotion, ringCount = 2 }: SensorSweepProps) {
  const beamGroupRef = useRef<Group>(null);
  const beamMaterialRef = useRef<MeshBasicMaterial>(null);
  const ringGroupRef = useRef<Group>(null);
  const ringARef = useRef<Group>(null);
  const ringBRef = useRef<Group>(null);
  const ringAMaterialRef = useRef<MeshBasicMaterial>(null);
  const ringBMaterialRef = useRef<MeshBasicMaterial>(null);

  // One colour for beam and ring: they are the same instrument, and giving the ring its own
  // hue would imply it measures something the beam does not.
  const beamColor = sceneColor('scanBeam', 0.2);
  const ringColor = sceneColor('scanBeam', 0.2);

  function applyRing(
    group: Group | null,
    material: MeshBasicMaterial | null,
    stagger: number,
  ): void {
    if (!group || !material) return;
    if (reducedMotion) {
      group.scale.setScalar((RING_MIN + RING_MAX) / 2);
      material.opacity = 0.18;
      return;
    }
    const u = ((frame.elapsed / RING_PERIOD_SEC + stagger) % 1 + 1) % 1;
    group.scale.setScalar(RING_MIN + (RING_MAX - RING_MIN) * u);
    // Snaps in, fades out: a ring that faded in symmetrically would read as a pulse sitting
    // on the satellite rather than as a front travelling outward through it.
    material.opacity = Math.min(1, u * 7) * (1 - u) * 0.62;
  }

  function applySweep(): void {
    beamDirection.copy(frame.target).sub(frame.rescue);
    const distance = beamDirection.length();
    // The two craft are on different shells and can never coincide, but a degenerate
    // direction would put NaN into a quaternion and silently blank the whole layer, so the
    // guard stays.
    if (distance < 1e-6) return;
    beamDirection.divideScalar(distance);

    const beamGroup = beamGroupRef.current;
    if (beamGroup) {
      beamGroup.position.copy(frame.rescue);
      beamGroup.quaternion.setFromUnitVectors(CONE_AXIS, beamDirection);
      // Reach modulation: the beam retracts a few percent between sweeps and touches the
      // casualty exactly at each peak. Under reduced motion it simply holds the lock.
      const reach = reducedMotion
        ? 1
        : 0.94 + 0.06 * (0.5 + 0.5 * Math.sin((frame.elapsed / RING_PERIOD_SEC) * Math.PI * 2));
      beamGroup.scale.set(1, distance * reach, 1);
    }

    if (beamMaterialRef.current) {
      beamMaterialRef.current.opacity = reducedMotion
        ? 0.085
        : 0.075 + 0.075 * (0.5 + 0.5 * Math.sin((frame.elapsed / RING_PERIOD_SEC) * Math.PI * 2));
    }

    const ringGroup = ringGroupRef.current;
    if (ringGroup) {
      ringGroup.position.copy(frame.target);
      ringGroup.quaternion.setFromUnitVectors(RING_NORMAL, beamDirection);
    }

    applyRing(ringARef.current, ringAMaterialRef.current, 0);
    applyRing(ringBRef.current, ringBMaterialRef.current, 0.5);
  }

  useLayoutEffect(applySweep);
  useFrame(applySweep);

  return (
    <group>
      <group ref={beamGroupRef}>
        {/* Cone height 1 with its apex pulled back to the group origin, so scaling the group
            in Y lengthens the beam away from the sensor rather than about its middle. */}
        <mesh position={[0, 0.5, 0]} rotation-x={Math.PI}>
          <coneGeometry args={[BEAM_RADIUS, 1, 22, 1, true]} />
          <meshBasicMaterial
            ref={beamMaterialRef}
            color={beamColor}
            transparent
            opacity={0.07}
            blending={AdditiveBlending}
            depthWrite={false}
            side={DoubleSide}
            toneMapped={false}
          />
        </mesh>
      </group>

      <group ref={ringGroupRef}>
        <group ref={ringARef}>
          <mesh>
            <ringGeometry args={[0.86, 1, 56]} />
            <meshBasicMaterial
              ref={ringAMaterialRef}
              color={ringColor}
              transparent
              opacity={0}
              blending={AdditiveBlending}
              depthWrite={false}
              side={DoubleSide}
              toneMapped={false}
            />
          </mesh>
        </group>

        {ringCount === 2 && (
          <group ref={ringBRef}>
            <mesh>
              <ringGeometry args={[0.9, 1, 56]} />
              <meshBasicMaterial
                ref={ringBMaterialRef}
                color={ringColor}
                transparent
                opacity={0}
                blending={AdditiveBlending}
                depthWrite={false}
                side={DoubleSide}
                toneMapped={false}
              />
            </mesh>
          </group>
        )}
      </group>
    </group>
  );
}
