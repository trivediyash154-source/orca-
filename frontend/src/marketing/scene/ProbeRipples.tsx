import { useFrame, useThree } from '@react-three/fiber';
import { useEffect, useLayoutEffect, useMemo, useRef } from 'react';
import {
  AdditiveBlending,
  Color,
  DoubleSide,
  InstancedMesh,
  Object3D,
  Plane,
  Raycaster,
  Vector2,
  Vector3,
} from 'three';

import { SPECTRAL_RAMP, spectralAt } from './spectral';

/** Concurrent ripples. Six is enough that a fast series of taps all register and small
 * enough that the scene never turns into a light show. */
const POOL_SIZE = 6;
const LIFETIME_SEC = 1.2;
/** Reduced motion gets a brief acknowledgement instead of an expansion — the interaction
 * still confirms itself, it just does not move. */
const STATIC_LIFETIME_SEC = 0.42;

const RING_MIN_RADIUS = 0.024;
const RING_MAX_RADIUS = 0.26;
const RING_STATIC_RADIUS = 0.075;
const CORE_RADIUS = 0.016;
const CORE_LIFETIME_FRACTION = 0.22;

/** Ripples land on a plane through the scene origin, so a click near the frame edge would
 * otherwise resolve metres away from anything. */
const MAX_ORIGIN_DISTANCE = 2.6;

const SCENE_ORIGIN = new Vector3(0, 0, 0);
const instanceProxy = new Object3D();
const tintColor = new Color();

interface Ripple {
  active: boolean;
  ageSec: number;
  colorIndex: number;
  readonly position: Vector3;
}

function easeOutCubic(t: number): number {
  const u = 1 - t;
  return 1 - u * u * u;
}

export interface ProbeRipplesProps {
  readonly reducedMotion: boolean;
}

/**
 * Click or tap anywhere in the scene and a ripple opens at that point in space.
 *
 * It is a probe, not a toy, and the restraint is deliberate: one hairline ring, one brief
 * core flash, a colour taken in turn from the spectral ramp so a series of taps walks the
 * spectrum rather than picking at random. Nothing about it claims to measure anything.
 *
 * The pick is a real raycast against a plane through the scene origin facing the camera, so
 * a ripple sits at the depth the viewer was looking at rather than being pasted onto the
 * screen. `pointerdown` is bound once on the canvas element, which covers mouse, pen and
 * touch in one path.
 *
 * Both pools are pre-allocated. A click writes into a slot; the frame loop mutates instance
 * matrices and instance colours. No React state is touched by either, because a ripple that
 * re-rendered the scene graph would cost more than the entire rest of this layer.
 */
export function ProbeRipples({ reducedMotion }: ProbeRipplesProps) {
  const camera = useThree((state) => state.camera);
  const domElement = useThree((state) => state.gl.domElement);
  /** Under reduced motion `OrbitalHeroScene` runs the canvas on `frameloop="demand"`, where
   * nothing renders unless something asks. Without these requests a probe would be spawned
   * into the pool and never drawn — the one interaction on the page would silently do
   * nothing for exactly the readers who most need feedback that their input registered. */
  const invalidate = useThree((state) => state.invalidate);

  const ringsRef = useRef<InstancedMesh>(null);
  const coresRef = useRef<InstancedMesh>(null);
  const cursorRef = useRef(0);

  const pool = useMemo<Ripple[]>(
    () =>
      Array.from({ length: POOL_SIZE }, () => ({
        active: false,
        ageSec: 0,
        colorIndex: 0,
        position: new Vector3(),
      })),
    [],
  );

  const picker = useMemo(
    () => ({
      raycaster: new Raycaster(),
      plane: new Plane(),
      ndc: new Vector2(),
      normal: new Vector3(),
      hit: new Vector3(),
    }),
    [],
  );

  /** Stops on the ramp, so successive probes land on distinct hues instead of drifting
   * through near-identical ones. Guarded because the ramp is another module's constant. */
  const rampStops = Math.max(2, SPECTRAL_RAMP.length);

  /** Instance colour is black until a ripple is spawned; writing every slot once here also
   * forces `instanceColor` into existence before the first render, so the material is
   * compiled with per-instance colour from the start. */
  useLayoutEffect(() => {
    const rings = ringsRef.current;
    const cores = coresRef.current;
    instanceProxy.position.set(0, 0, 0);
    instanceProxy.quaternion.identity();
    instanceProxy.scale.setScalar(0);
    instanceProxy.updateMatrix();
    tintColor.setRGB(0, 0, 0);
    for (let i = 0; i < POOL_SIZE; i++) {
      rings?.setMatrixAt(i, instanceProxy.matrix);
      rings?.setColorAt(i, tintColor);
      cores?.setMatrixAt(i, instanceProxy.matrix);
      cores?.setColorAt(i, tintColor);
    }
    if (rings) {
      rings.instanceMatrix.needsUpdate = true;
      if (rings.instanceColor) rings.instanceColor.needsUpdate = true;
    }
    if (cores) {
      cores.instanceMatrix.needsUpdate = true;
      if (cores.instanceColor) cores.instanceColor.needsUpdate = true;
    }
  }, []);

  useEffect(() => {
    function handlePointerDown(event: PointerEvent): void {
      // Primary contact only: a second finger during a pinch is a gesture, not a probe.
      if (!event.isPrimary || event.button > 0) return;
      const rect = domElement.getBoundingClientRect();
      if (rect.width < 1 || rect.height < 1) return;

      picker.ndc.set(
        ((event.clientX - rect.left) / rect.width) * 2 - 1,
        -((event.clientY - rect.top) / rect.height) * 2 + 1,
      );
      picker.raycaster.setFromCamera(picker.ndc, camera);
      camera.getWorldDirection(picker.normal);
      picker.plane.setFromNormalAndCoplanarPoint(picker.normal.negate(), SCENE_ORIGIN);
      if (!picker.raycaster.ray.intersectPlane(picker.plane, picker.hit)) return;
      picker.hit.clampLength(0, MAX_ORIGIN_DISTANCE);

      if (reducedMotion) {
        // One acknowledgement at a time: overlapping static highlights would stack into a
        // brightening blob with nothing to explain it.
        for (const ripple of pool) ripple.active = false;
      }
      const slot = reducedMotion ? 0 : cursorRef.current;
      const ripple = pool[slot];
      if (!ripple) return;
      ripple.active = true;
      ripple.ageSec = 0;
      ripple.colorIndex = cursorRef.current;
      ripple.position.copy(picker.hit);
      cursorRef.current = (cursorRef.current + 1) % POOL_SIZE;
      invalidate();
    }

    domElement.addEventListener('pointerdown', handlePointerDown, { passive: true });
    return () => domElement.removeEventListener('pointerdown', handlePointerDown);
  }, [camera, domElement, invalidate, picker, pool, reducedMotion]);

  useFrame((_, delta) => {
    const rings = ringsRef.current;
    const cores = coresRef.current;
    if (!rings || !cores) return;

    const lifetime = reducedMotion ? STATIC_LIFETIME_SEC : LIFETIME_SEC;
    let anyActive = false;
    // A tab returning from the background hands back one enormous delta; clamping it retires
    // stale ripples in a single step instead of letting them hang in the scene.
    const step = Math.min(delta, 0.1);

    for (let i = 0; i < POOL_SIZE; i++) {
      const ripple = pool[i];
      if (!ripple) continue;

      if (!ripple.active) {
        instanceProxy.scale.setScalar(0);
        // Fall through to write a zero-scale matrix; an inactive slot must be cleared rather
        // than left holding the last frame of the ripple that used it.
        instanceProxy.updateMatrix();
        rings.setMatrixAt(i, instanceProxy.matrix);
        cores.setMatrixAt(i, instanceProxy.matrix);
        continue;
      }

      anyActive = true;
      ripple.ageSec += step;
      const u = ripple.ageSec / lifetime;
      if (u >= 1) {
        ripple.active = false;
        instanceProxy.scale.setScalar(0);
        instanceProxy.updateMatrix();
        rings.setMatrixAt(i, instanceProxy.matrix);
        cores.setMatrixAt(i, instanceProxy.matrix);
        continue;
      }

      const tint = spectralAt((ripple.colorIndex % rampStops) / (rampStops - 1));
      instanceProxy.position.copy(ripple.position);
      // Billboarded to the camera: a ring seen edge-on is a line, and the probe has to read
      // as a ring from wherever the viewer happens to be.
      instanceProxy.quaternion.copy(camera.quaternion);

      const ringRadius = reducedMotion
        ? RING_STATIC_RADIUS
        : RING_MIN_RADIUS + (RING_MAX_RADIUS - RING_MIN_RADIUS) * easeOutCubic(u);
      instanceProxy.scale.setScalar(ringRadius);
      instanceProxy.updateMatrix();
      rings.setMatrixAt(i, instanceProxy.matrix);
      // Additive blending turns a scaled-down colour into an exact fade, which is how these
      // get per-instance opacity without a custom shader.
      const ringFade = (1 - u) * (1 - u) * (reducedMotion ? 0.75 : 1);
      tintColor.setRGB(tint.r * ringFade, tint.g * ringFade, tint.b * ringFade);
      rings.setColorAt(i, tintColor);

      const coreFade = Math.max(0, 1 - u / CORE_LIFETIME_FRACTION);
      instanceProxy.scale.setScalar(CORE_RADIUS * (0.55 + 0.45 * coreFade));
      instanceProxy.updateMatrix();
      cores.setMatrixAt(i, instanceProxy.matrix);
      const coreIntensity = coreFade * coreFade;
      tintColor.setRGB(
        tint.r * coreIntensity,
        tint.g * coreIntensity,
        tint.b * coreIntensity,
      );
      cores.setColorAt(i, tintColor);
    }

    rings.instanceMatrix.needsUpdate = true;
    cores.instanceMatrix.needsUpdate = true;
    if (rings.instanceColor) rings.instanceColor.needsUpdate = true;
    if (cores.instanceColor) cores.instanceColor.needsUpdate = true;
    // Keeps the demand-mode loop alive for exactly as long as a ripple is on screen, and no
    // longer. Under `frameloop="always"` this is a counter bump and costs nothing.
    if (anyActive) invalidate();
  });

  return (
    <group>
      <instancedMesh
        ref={ringsRef}
        args={[undefined, undefined, POOL_SIZE]}
        frustumCulled={false}
        renderOrder={6}
      >
        <ringGeometry args={[0.94, 1, 72]} />
        <meshBasicMaterial
          transparent
          opacity={1}
          blending={AdditiveBlending}
          depthWrite={false}
          side={DoubleSide}
          toneMapped={false}
        />
      </instancedMesh>

      <instancedMesh
        ref={coresRef}
        args={[undefined, undefined, POOL_SIZE]}
        frustumCulled={false}
        renderOrder={7}
      >
        <circleGeometry args={[1, 24]} />
        <meshBasicMaterial
          transparent
          opacity={1}
          blending={AdditiveBlending}
          depthWrite={false}
          side={DoubleSide}
          toneMapped={false}
        />
      </instancedMesh>
    </group>
  );
}
