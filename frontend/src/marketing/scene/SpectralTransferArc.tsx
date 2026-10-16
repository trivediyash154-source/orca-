import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef } from 'react';
import {
  AdditiveBlending,
  BufferAttribute,
  BufferGeometry,
  Color,
  DynamicDrawUsage,
  InstancedMesh,
  Mesh,
  MeshBasicMaterial,
  Object3D,
  Vector3,
} from 'three';

import {
  DOMINATED_SHAPES,
  sceneColor,
  writeTransferPoint,
  type MissionFrame,
  type SolveState,
  type TransferShape,
} from './heroOrbits';
import { spectralAt } from './spectral';

/** Samples along the adopted arc. High enough that the ribbon reads as a curve at 2560 px,
 * low enough that rebuilding it every frame is free — 96 samples is roughly 1,200 trig
 * evaluations per frame, two orders of magnitude below anything that would show up. */
const CORE_SAMPLES = 96;
const ALTERNATIVE_SAMPLES = 56;

/** Ribbon half-widths in scene units (Earth radius = 1). The core is a hairline; the halo
 * behind it supplies the bloom, because this project has no post-processing pass and is not
 * adding one. The halo is kept tight for a reason found in a browser check: both spacecraft
 * sit at an END of the ribbon, so a generous halo buries exactly the two objects the scene
 * exists to contrast. */
const CORE_HALF_WIDTH = 0.0055;
const HALO_HALF_WIDTH = 0.014;
const ALTERNATIVE_HALF_WIDTH = 0.0032;

const PULSE_COUNT = 4;
const PULSE_SPEED = 0.34;
const PULSE_RADIUS = 0.0105;

const pointA = new Vector3();
const pointB = new Vector3();
const tangent = new Vector3();
const toCamera = new Vector3();
const side = new Vector3();
const pulseColor = new Color();
const instanceProxy = new Object3D();

interface Ribbon {
  readonly geometry: BufferGeometry;
  readonly positionAttr: BufferAttribute;
  readonly positions: Float32Array;
  readonly samples: number;
  readonly halfWidth: number;
}

/**
 * A camera-facing triangle strip along the transfer, allocated once.
 *
 * Vertex colours are baked at construction because they depend only on position ALONG the
 * arc, never on time — that is what maps the spectral ramp onto the path rather than onto
 * the clock. Positions are rewritten in place each frame so the arc stays anchored to two
 * moving spacecraft; nothing is allocated after this call.
 */
function createRibbon(
  samples: number,
  halfWidth: number,
  colorAt: (t: number) => Color,
): Ribbon {
  const vertexCount = samples * 2;
  const positions = new Float32Array(vertexCount * 3);
  const colors = new Float32Array(vertexCount * 3);
  const indices = new Uint16Array((samples - 1) * 6);

  const last = samples - 1;
  for (let i = 0; i < samples; i++) {
    const c = colorAt(i / last);
    for (let edge = 0; edge < 2; edge++) {
      const o = (i * 2 + edge) * 3;
      colors[o] = c.r;
      colors[o + 1] = c.g;
      colors[o + 2] = c.b;
    }
  }
  for (let i = 0; i < last; i++) {
    const a = i * 2;
    const o = i * 6;
    indices[o] = a;
    indices[o + 1] = a + 1;
    indices[o + 2] = a + 2;
    indices[o + 3] = a + 1;
    indices[o + 4] = a + 3;
    indices[o + 5] = a + 2;
  }

  const positionAttr = new BufferAttribute(positions, 3);
  positionAttr.setUsage(DynamicDrawUsage);
  const geometry = new BufferGeometry();
  geometry.setAttribute('position', positionAttr);
  geometry.setAttribute('color', new BufferAttribute(colors, 3));
  geometry.setIndex(new BufferAttribute(indices, 1));
  geometry.setDrawRange(0, 0);
  return { geometry, positionAttr, positions, samples, halfWidth };
}

/** Rewrites one ribbon's vertices for the current pose. `progress` drives `setDrawRange`,
 * which is how the arc appears to be solved forward: the geometry is complete the whole
 * time and only the drawn span grows, so nothing is rebuilt mid-animation. */
function fillRibbon(
  ribbon: Ribbon,
  frame: MissionFrame,
  shape: TransferShape,
  exaggeration: number,
  cameraPosition: Vector3,
  progress: number,
): void {
  const { positions, samples, halfWidth } = ribbon;
  const last = samples - 1;
  const step = 0.5 / last;

  for (let i = 0; i < samples; i++) {
    const s = i / last;
    writeTransferPoint(frame, shape, s, exaggeration, pointA);
    writeTransferPoint(frame, shape, i === last ? s - step : s + step, exaggeration, pointB);
    if (i === last) {
      tangent.copy(pointA).sub(pointB);
    } else {
      tangent.copy(pointB).sub(pointA);
    }
    toCamera.copy(cameraPosition).sub(pointA);
    side.crossVectors(tangent, toCamera);
    if (side.lengthSq() < 1e-14) {
      // Looking straight down the arc: fall back to the radial direction so the ribbon keeps
      // a defined width instead of collapsing to NaN.
      side.crossVectors(tangent, pointA);
    }
    side.normalize().multiplyScalar(halfWidth);
    const o = i * 6;
    positions[o] = pointA.x - side.x;
    positions[o + 1] = pointA.y - side.y;
    positions[o + 2] = pointA.z - side.z;
    positions[o + 3] = pointA.x + side.x;
    positions[o + 4] = pointA.y + side.y;
    positions[o + 5] = pointA.z + side.z;
  }

  ribbon.positionAttr.needsUpdate = true;
  const drawnSegments = Math.max(0, Math.min(last, Math.round(progress * last)));
  ribbon.geometry.setDrawRange(0, drawnSegments * 6);
}

export interface SpectralTransferArcProps {
  readonly frame: MissionFrame;
  readonly solve: SolveState;
  readonly exaggeration: number;
  readonly reducedMotion: boolean;
  /** The authentication plate drops the dominated alternatives: behind a form the extra
   * geometry is noise, and the Pareto argument is made in the landing page's copy. */
  readonly showAlternatives?: boolean;
}

/**
 * The adopted transfer, and the candidates it beat.
 *
 * Three claims are made visually here, and each has a limit worth stating:
 *
 * 1. **The arc is generated, not drawn.** Its points come from interpolating orbital
 *    elements between the two shells and evaluating the engine's own position expression
 *    (`heroOrbits.ts`), so it departs tangentially, rolls out of one plane into the other,
 *    and lands exactly on the casualty. It is NOT an engine-computed trajectory: the landing
 *    page makes no engine call, and no delta-v, duration or risk figure anywhere on the page
 *    is derived from this geometry.
 * 2. **It is solved, then re-solved.** The sweep and the periodic change of geometry stand
 *    for replanning — the idea the whole project turns on. The cadence is presentational.
 * 3. **It won against alternatives.** The grey arcs behind it are illustrative candidate
 *    geometries, not points from a computed Pareto front.
 *
 * The spectral ramp is mapped along the arc's LENGTH, which is the only thing on this
 * surface allowed to carry it: the ramp encodes progress along the optimal transfer and is
 * not decoration applied to whatever needed brightening.
 */
export function SpectralTransferArc({
  frame,
  solve,
  exaggeration,
  reducedMotion,
  showAlternatives = true,
}: SpectralTransferArcProps) {
  // Dominated candidates take the palette's dimmest neutral: they have to be visible enough
  // to be seen losing and quiet enough that they never read as a second route.
  const neutral = useMemo(() => new Color(sceneColor('debrisFar', 0.99)), []);
  const frontColor = sceneColor('arcEnd', 1);

  const core = useMemo(() => createRibbon(CORE_SAMPLES, CORE_HALF_WIDTH, spectralAt), []);
  const halo = useMemo(() => createRibbon(CORE_SAMPLES, HALO_HALF_WIDTH, spectralAt), []);
  const alternatives = useMemo(
    () =>
      DOMINATED_SHAPES.map((shape) => ({
        shape,
        ribbon: createRibbon(ALTERNATIVE_SAMPLES, ALTERNATIVE_HALF_WIDTH, () => neutral),
      })),
    [neutral],
  );

  useEffect(() => {
    const ribbons = [core, halo, ...alternatives.map((a) => a.ribbon)];
    return () => ribbons.forEach((r) => r.geometry.dispose());
  }, [core, halo, alternatives]);

  const coreMaterialRef = useRef<MeshBasicMaterial>(null);
  const haloMaterialRef = useRef<MeshBasicMaterial>(null);
  const frontRef = useRef<Mesh>(null);
  const pulsesRef = useRef<InstancedMesh>(null);

  useFrame((state) => {
    const camera = state.camera.position;
    fillRibbon(core, frame, solve.shape, exaggeration, camera, solve.progress);
    fillRibbon(halo, frame, solve.shape, exaggeration, camera, solve.progress);
    // Gated on the same flag as the meshes below. The auth variant passes `false`, and without
    // this the sign-in page rewrote two vertex buffers every frame for geometry it never
    // submits -- 224 trig evaluations and two `needsUpdate` flags per frame, for as long as the
    // page is open. The ribbons stay allocated: only the fill is conditional, so toggling the
    // flag needs no reallocation.
    if (showAlternatives) {
      for (const alternative of alternatives) {
        // Alternatives are always drawn in full: a candidate that is being animated competes
        // with the adopted route for attention, which is precisely backwards.
        fillRibbon(alternative.ribbon, frame, alternative.shape, exaggeration, camera, 1);
      }
    }

    if (coreMaterialRef.current) coreMaterialRef.current.opacity = 0.95 * solve.alpha;
    if (haloMaterialRef.current) haloMaterialRef.current.opacity = 0.22 * solve.alpha;

    const front = frontRef.current;
    if (front) {
      const solving = !reducedMotion && solve.progress < 1;
      front.visible = solving;
      if (solving) {
        writeTransferPoint(frame, solve.shape, solve.progress, exaggeration, pointA);
        front.position.copy(pointA);
        // Brightest at the instant of departure, easing off as the front slows into its
        // settled position.
        const flare = 1 - solve.progress * 0.55;
        front.scale.setScalar(flare);
      }
    }

    const pulses = pulsesRef.current;
    if (pulses) {
      pulses.visible = !reducedMotion && solve.progress > 0.55;
      if (pulses.visible) {
        for (let i = 0; i < PULSE_COUNT; i++) {
          const offset = i / PULSE_COUNT;
          const s = (frame.elapsed * PULSE_SPEED + offset) % 1;
          const visible = s <= solve.progress;
          writeTransferPoint(frame, solve.shape, s, exaggeration, pointA);
          instanceProxy.position.copy(pointA);
          instanceProxy.scale.setScalar(visible ? 1 : 0);
          instanceProxy.updateMatrix();
          pulses.setMatrixAt(i, instanceProxy.matrix);
          // Additive blending makes a scaled-down colour an exact fade, which is how a
          // per-instance opacity is expressed without a custom shader.
          const sample = spectralAt(s);
          const fade = solve.alpha * (0.55 + 0.45 * Math.sin(s * Math.PI));
          pulseColor.setRGB(sample.r * fade, sample.g * fade, sample.b * fade);
          pulses.setColorAt(i, pulseColor);
        }
        pulses.instanceMatrix.needsUpdate = true;
        if (pulses.instanceColor) pulses.instanceColor.needsUpdate = true;
      }
    }
  });

  return (
    <group>
      {showAlternatives &&
        alternatives.map((alternative) => (
          <mesh
            key={alternative.shape.id}
            geometry={alternative.ribbon.geometry}
            frustumCulled={false}
            renderOrder={1}
          >
            <meshBasicMaterial
              vertexColors
              transparent
              opacity={0.3}
              blending={AdditiveBlending}
              depthWrite={false}
              toneMapped={false}
            />
          </mesh>
        ))}

      <mesh geometry={halo.geometry} frustumCulled={false} renderOrder={2}>
        <meshBasicMaterial
          ref={haloMaterialRef}
          vertexColors
          transparent
          opacity={0.22}
          blending={AdditiveBlending}
          depthWrite={false}
          toneMapped={false}
        />
      </mesh>

      <mesh geometry={core.geometry} frustumCulled={false} renderOrder={3}>
        <meshBasicMaterial
          ref={coreMaterialRef}
          vertexColors
          transparent
          opacity={0.95}
          blending={AdditiveBlending}
          depthWrite={false}
          toneMapped={false}
        />
      </mesh>

      {/* The solving front: the point the search has reached. It exists because a growing
          line alone reads as a wipe, and a bright head reads as a computation. */}
      <mesh ref={frontRef} visible={false} frustumCulled={false} renderOrder={4}>
        <sphereGeometry args={[0.018, 12, 12]} />
        <meshBasicMaterial
          color={frontColor}
          transparent
          opacity={0.85}
          blending={AdditiveBlending}
          depthWrite={false}
          toneMapped={false}
        />
      </mesh>

      {/* Data travelling the adopted route once it is settled. */}
      <instancedMesh
        ref={pulsesRef}
        args={[undefined, undefined, PULSE_COUNT]}
        frustumCulled={false}
        renderOrder={5}
      >
        <sphereGeometry args={[PULSE_RADIUS, 10, 10]} />
        {/* No `vertexColors` here on purpose: `instanceColor` is multiplied into the base
            colour by the renderer on its own, and turning `vertexColors` on would make the
            shader read a `color` attribute this geometry does not have — which resolves to
            black, not to the instance colour. */}
        <meshBasicMaterial
          transparent
          opacity={0.9}
          blending={AdditiveBlending}
          depthWrite={false}
          toneMapped={false}
        />
      </instancedMesh>
    </group>
  );
}
