import { useFrame } from '@react-three/fiber';
import { useEffect, useMemo, useRef } from 'react';
import { AdditiveBlending, BufferAttribute, Color, type Points } from 'three';

import { SCENE_COLORS } from './spectral';
import { getMarkerGlowTexture } from '../../visualization/satellites/markerGlowTexture';

/**
 * Orbital debris and distant particulate for the entry surface.
 *
 * This is scenery, and it is labelled as such: nothing here is a tracked object, a catalogue
 * entry, or engine output. Its job is to make the volume around Earth read as populated, which
 * is the actual premise of the project — a rescue vehicle has to cross a congested shell to
 * reach a disabled satellite — without inventing a single figure.
 *
 * ## One draw call
 *
 * Every point shares one `BufferGeometry` and one `PointsMaterial`.
 * `docs/FINAL_BENCHMARK_REPORT.md` §6 measured what the alternative costs in this codebase:
 * one mesh per marker produced 1,128 draw calls per frame at a 500-satellite scenario against
 * 568 once markers were instanced. The same reasoning applies here, and `THREE.Points`
 * billboards toward the camera for free, so a soft round particle needs no per-object rotation
 * work at all.
 *
 * The distant particulate is folded into that SAME buffer rather than getting a second
 * `Points` object. `PointsMaterial` carries one `size` for the whole draw, but with
 * `sizeAttenuation` on, a particle three times further away already renders at a third the
 * apparent size — so distance does the work a second material would have done, and the far
 * field only needs its own (dimmer) vertex colour.
 */

export interface DebrisFieldProps {
  /** Caller-controlled so the mobile tier can ask for a fraction of the desktop count. */
  readonly count: number;
  readonly reducedMotion: boolean;
  /** Inner and outer radius of the POPULATED shells, in scene units (Earth = 1). The diffuse
   * halo is placed beyond the outer value. */
  readonly radiusRange: readonly [number, number];
}

/**
 * A 32-bit LCG (Numerical Recipes constants), seeded from a fixed value.
 *
 * `Math.random()` is not used anywhere in this project's visualisation layer, and this file
 * keeps that rule for the same reason `earthTexture.ts` states for its own PRNG: a scene that
 * re-rolls its layout on every reload cannot be reviewed, screenshotted for a regression
 * comparison, or reasoned about when someone reports "the debris is clipping the limb". Given
 * a seed, this field is byte-identical on every machine and every mount.
 */
function createRandom(seed: number): () => number {
  let state = seed >>> 0;
  return () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    return state / 4294967296;
  };
}

const SEED = 0x0d3b715;

/** Fraction of the budget spent on the populated shells; the rest becomes the diffuse halo. */
const SHELL_FRACTION = 0.66;

/** How far past the outermost populated shell the halo reaches, as a multiple of that radius. */
const HALO_REACH = 2.7;

/** Base angular rate at r = 1, in rad/s of wall clock. Openly a presentation pace: at a real
 * LEO rate the whole field would blur, and at zero it would read as a static texture. */
const BASE_RATE_RAD_PER_SEC = 0.09;

/** See `HeroEarth.tsx` — a restored tab hands the first frame the whole hidden interval. */
const MAX_FRAME_DELTA_SEC = 0.05;

interface DebrisElement {
  readonly radius: number;
  /** Angle in the object's own orbital plane at t = 0. */
  readonly phase: number;
  readonly rate: number;
  readonly cosInclination: number;
  readonly sinInclination: number;
  /** Right ascension of the ascending node — which way the plane is turned about the pole. */
  readonly cosNode: number;
  readonly sinNode: number;
}

/**
 * Builds the field once. Elements are plain records rather than parallel typed arrays: at the
 * few hundred to ~1k objects this component is ever asked for, the per-frame cost is identical,
 * and a record survives `noUncheckedIndexedAccess` without a cast at every read.
 */
function buildElements(count: number, innerRadius: number, outerRadius: number): DebrisElement[] {
  const random = createRandom(SEED);
  const elements: DebrisElement[] = [];
  const shellCount = Math.round(count * SHELL_FRACTION);
  const haloOuter = outerRadius * HALO_REACH;

  for (let i = 0; i < count; i += 1) {
    const onShell = i < shellCount;
    // Cube-root of a uniform sample gives a volumetrically even halo; a linear sample would
    // pile the far field up against the inner boundary and leave the outside empty.
    const radius = onShell
      ? innerRadius + (outerRadius - innerRadius) * random()
      : outerRadius + (haloOuter - outerRadius) * Math.cbrt(random());

    // Shell objects cluster around a handful of inclinations, the way a real catalogue does;
    // the halo is isotropic. `acos` of a uniform sample is what keeps the isotropic case from
    // bunching at the poles.
    const inclination = onShell
      ? (random() < 0.55 ? 0.28 : 1.22) + (random() - 0.5) * 0.7
      : Math.acos(2 * random() - 1);

    const node = random() * Math.PI * 2;

    // Higher shells lag lower ones — the qualitative relation Kepler's third law gives, used
    // here only so the field has visible differential motion. It is not a propagation, and no
    // value derived from it is ever displayed.
    const rate = (BASE_RATE_RAD_PER_SEC / Math.pow(radius, 1.5)) * (onShell ? 1 : 0.35);

    elements.push({
      radius,
      phase: random() * Math.PI * 2,
      rate,
      cosInclination: Math.cos(inclination),
      sinInclination: Math.sin(inclination),
      cosNode: Math.cos(node),
      sinNode: Math.sin(node),
    });
  }
  return elements;
}

const nearColor = new Color(SCENE_COLORS.debrisNear);
const farColor = new Color(SCENE_COLORS.debrisFar);
const haloColor = new Color(SCENE_COLORS.particulate);
const scratchColor = new Color();

export function DebrisField({ count, reducedMotion, radiusRange }: DebrisFieldProps) {
  const pointsRef = useRef<Points>(null);
  const texture = useMemo(() => getMarkerGlowTexture(), []);
  const elapsedRef = useRef(0);

  const innerRadius = radiusRange[0];
  const outerRadius = radiusRange[1];
  const safeCount = Math.max(1, Math.floor(count));

  const elements = useMemo(
    () => buildElements(safeCount, innerRadius, outerRadius),
    [safeCount, innerRadius, outerRadius],
  );

  const positions = useMemo(() => new Float32Array(elements.length * 3), [elements]);

  /** Colour encodes shell radius: shell debris fades from `debrisNear` to `debrisFar` across
   * the populated band, and the halo sits below both. That is the entire depth cue — no extra
   * geometry, no second pass — and it keeps the spectral ramp reserved for the mission layer,
   * where a colour actually means something. */
  const colors = useMemo(() => {
    const buffer = new Float32Array(elements.length * 3);
    const span = Math.max(1e-6, outerRadius - innerRadius);
    elements.forEach((element, i) => {
      if (element.radius <= outerRadius) {
        const depth = Math.min(1, Math.max(0, (element.radius - innerRadius) / span));
        scratchColor.copy(nearColor).lerp(farColor, depth);
      } else {
        scratchColor.copy(haloColor);
      }
      buffer[i * 3] = scratchColor.r;
      buffer[i * 3 + 1] = scratchColor.g;
      buffer[i * 3 + 2] = scratchColor.b;
    });
    return buffer;
  }, [elements, innerRadius, outerRadius]);

  /** Position at `time`, written straight into the shared buffer. The transform is the usual
   * two rotations: the orbit is traced in its own plane, tilted by inclination about the scene
   * X axis, then swung to its node about the scene Y (polar) axis — Y-up being the convention
   * `coordinates.ts` fixes for the whole project. */
  function writePositions(time: number): void {
    elements.forEach((element, i) => {
      const angle = element.phase + element.rate * time;
      const planarX = Math.cos(angle) * element.radius;
      const planarZ = Math.sin(angle) * element.radius;

      const tiltedY = -planarZ * element.sinInclination;
      const tiltedZ = planarZ * element.cosInclination;

      positions[i * 3] = planarX * element.cosNode + tiltedZ * element.sinNode;
      positions[i * 3 + 1] = tiltedY;
      positions[i * 3 + 2] = -planarX * element.sinNode + tiltedZ * element.cosNode;
    });
  }

  // The field must be correct in a single static frame too: under reduced motion, and under
  // `frameloop="demand"`, `useFrame` may never run a second time. `writePositions` is a plain
  // closure over `elements` and `positions`, so those two are the real dependencies -- naming
  // the function itself would re-run this on every render for no change in output.
  useEffect(() => {
    writePositions(elapsedRef.current);
    const attribute = pointsRef.current?.geometry.getAttribute('position');
    if (attribute) (attribute as BufferAttribute).needsUpdate = true;
  }, [elements, positions]);

  useFrame((_state, delta) => {
    if (reducedMotion) return;
    elapsedRef.current += Math.min(delta, MAX_FRAME_DELTA_SEC);
    writePositions(elapsedRef.current);
    const attribute = pointsRef.current?.geometry.getAttribute('position');
    if (attribute) (attribute as BufferAttribute).needsUpdate = true;
  });

  return (
    /* `raycast` is disabled outright: the only pointer interaction on this canvas is
       click-to-probe, and a debris particle silently swallowing that click would make the
       interaction feel broken in exactly the places the field is densest. */
    <points ref={pointsRef} raycast={() => null}>
      <bufferGeometry>
        <bufferAttribute attach="attributes-position" args={[positions, 3]} />
        <bufferAttribute attach="attributes-color" args={[colors, 3]} />
      </bufferGeometry>
      <pointsMaterial
        map={texture}
        size={0.021}
        vertexColors
        transparent
        opacity={0.8}
        depthWrite={false}
        blending={AdditiveBlending}
        sizeAttenuation
        toneMapped={false}
      />
    </points>
  );
}
