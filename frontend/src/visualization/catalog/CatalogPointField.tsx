/**
 * The catalogue point field — every object in the synchronised catalog, as one draw call.
 *
 * ## The rendering contract
 *
 * One `THREE.Points`, one `BufferGeometry`, three typed arrays (position, colour, size),
 * allocated once for the object count and mutated in place. There is no React component per
 * satellite and no per-satellite state: ten thousand objects cost one draw call and one
 * `needsUpdate` flag per propagation tick. This follows the precedent the project already
 * measured — `docs/FINAL_BENCHMARK_REPORT.md` §6 recorded the draw-call collapse from one mesh
 * per marker to one instanced draw, and the same argument applies an order of magnitude larger.
 *
 * ## Why propagation is throttled rather than per-frame
 *
 * Measured on this machine with the real 10,715-object Starlink group: SGP4 costs 0.48 µs per
 * object, so a full re-propagation is ~5.2 ms. That fits inside a 16.7 ms frame, but spending a
 * third of every frame's budget on it would be wasteful for no visible gain — at orbital speed
 * an object moves under a kilometre in 100 ms, which is well below a pixel at any useful zoom.
 * So the field re-propagates on an interval (default 10 Hz) and renders every frame from the
 * positions it already has.
 *
 * ## Custom shader, and why
 *
 * `PointsMaterial` cannot vary point size per vertex, which the selection, hover and
 * mission-role sizing all need. The shader below is the minimum that provides it: per-point
 * size with distance attenuation, per-point colour, and a round soft-edged sprite computed in
 * the fragment shader rather than sampled from a texture.
 */

import { useFrame, useThree } from '@react-three/fiber';
import { useCallback, useEffect, useMemo, useRef } from 'react';
import {
  AdditiveBlending,
  BufferAttribute,
  BufferGeometry,
  PerspectiveCamera,
  ShaderMaterial,
  Vector3,
  type Points,
} from 'three';

import {
  colorFor,
  POINT_SIZE,
  SELECTED_COLOR,
  type CatalogClassName,
  type RelevanceName,
} from './catalogColors';
import { compileElements, propagateInto, PropagationFlag } from '../../catalog/propagation';
import type { CatalogElement } from '../../catalog/types';
import type { SimClockHandle } from '../../state/SimClockContext';

export interface CatalogPointFieldProps {
  readonly elements: readonly CatalogElement[];
  readonly clock: React.MutableRefObject<SimClockHandle>;
  readonly selectedId: number | null;
  readonly hoveredId: number | null;
  readonly onSelect: (id: number | null) => void;
  readonly onHover: (id: number | null) => void;
  /** Skips the animated re-propagation; the field renders one static instant. */
  readonly reducedMotion?: boolean;
  /** Milliseconds between re-propagations. See the note above on why this is not per-frame. */
  readonly propagationIntervalMs?: number;
  /** Reports how many objects SGP4 could and could not place, for an honest status readout. */
  readonly onPropagationResult?: (ok: number, failed: number) => void;
}

const VERTEX_SHADER = /* glsl */ `
  attribute float size;
  attribute vec3 pointColor;
  // Physical pixels a one-unit sphere subtends at one unit of depth:
  //   drawingBufferHeight / (2 * tan(fov / 2))
  // Deriving the scale from the projection is what makes the size attribute a real
  // world-space diameter in Earth radii. An arbitrary constant here instead produced about
  // one physical pixel per point at the default camera distance: the field was being drawn,
  // and was invisible. Found in a browser, not in a test.
  uniform float uPixelsPerUnit;
  varying vec3 vColor;
  varying float vAlpha;
  void main() {
    vColor = pointColor;
    vec4 mv = modelViewMatrix * vec4(position, 1.0);
    // Perspective attenuation: a point twice as far is half as wide, which is what makes the
    // shell structure of a constellation legible instead of a uniform fog of dots. Clamped
    // below so a distant shell thins rather than disappearing, and above so a close pass
    // does not fill the screen with one sprite.
    gl_PointSize = clamp(size * uPixelsPerUnit / max(-mv.z, 0.01), 1.5, 64.0);
    // Objects SGP4 could not place are written as NaN; NaN fails every comparison, so this
    // test is false for them and they are discarded rather than drawn at the origin.
    vAlpha = (size > 0.0) ? 1.0 : 0.0;
    gl_Position = projectionMatrix * mv;
  }
`;

const FRAGMENT_SHADER = /* glsl */ `
  varying vec3 vColor;
  varying float vAlpha;
  void main() {
    // Round sprite with a soft edge, computed rather than sampled: no texture upload, and it
    // stays crisp at any point size.
    vec2 d = gl_PointCoord - vec2(0.5);
    float r = dot(d, d);
    if (r > 0.25) discard;
    float edge = smoothstep(0.25, 0.06, r);
    gl_FragColor = vec4(vColor, edge * vAlpha);
    if (gl_FragColor.a < 0.02) discard;
  }
`;

/** How near the cursor a point must project to be selectable, in CSS pixels. */
const PICK_RADIUS_PX = 14;

/** Movement beyond this between press and release is a camera drag, not a click. */
const DRAG_SLOP_PX = 4;

/** Hover picking is throttled: it is the same search as a click and does not need 60 Hz. */
const HOVER_INTERVAL_MS = 60;

/** Reused across the whole pick loop so a ten-thousand-object search allocates nothing. */
const projected = new Vector3();
const toPoint = new Vector3();

export function CatalogPointField({
  elements,
  clock,
  selectedId,
  hoveredId,
  onSelect,
  onHover,
  reducedMotion = false,
  propagationIntervalMs = 100,
  onPropagationResult,
}: CatalogPointFieldProps) {
  const pointsRef = useRef<Points>(null);
  const downAt = useRef<{ x: number; y: number } | null>(null);
  const lastHoverAt = useRef(0);
  const { camera, size, gl } = useThree();

  // Compiling ten thousand element sets is the expensive part (SGP4 initialisation, including
  // deep-space terms), so it happens once per catalogue load and never per frame.
  const compiled = useMemo(() => compileElements(elements), [elements]);

  const buffers = useMemo(() => {
    const n = Math.max(elements.length, 1);
    return {
      positions: new Float32Array(n * 3),
      colors: new Float32Array(n * 3),
      sizes: new Float32Array(n),
      flags: new Uint8Array(n),
      count: elements.length,
    };
  }, [elements.length]);

  const geometry = useMemo(() => {
    const g = new BufferGeometry();
    g.setAttribute('position', new BufferAttribute(buffers.positions, 3));
    g.setAttribute('pointColor', new BufferAttribute(buffers.colors, 3));
    g.setAttribute('size', new BufferAttribute(buffers.sizes, 1));
    // The field spans the whole scene and every point moves; a bounding sphere computed once
    // from a single instant would frustum-cull objects that have since moved back into view.
    g.boundingSphere = null;
    g.setDrawRange(0, buffers.count);
    return g;
  }, [buffers]);

  const material = useMemo(
    () =>
      new ShaderMaterial({
        uniforms: { uPixelsPerUnit: { value: 1000 } },
        vertexShader: VERTEX_SHADER,
        fragmentShader: FRAGMENT_SHADER,
        transparent: true,
        depthWrite: false,
        blending: AdditiveBlending,
      }),
    [],
  );

  // Recomputed whenever the viewport or the camera's field of view changes, so a resize or a
  // lens change rescales the field instead of silently shifting every point's apparent size.
  useEffect(() => {
    if (!(camera instanceof PerspectiveCamera)) return;
    const uniform = material.uniforms.uPixelsPerUnit;
    if (uniform === undefined) return;
    const physicalHeight = size.height * gl.getPixelRatio();
    uniform.value = physicalHeight / (2 * Math.tan((camera.fov * Math.PI) / 360));
  }, [camera, material, size.height, gl]);

  // Three.js resources are not garbage-collected with the React tree: without this the GPU
  // keeps every buffer from every catalogue the user ever loaded.
  useEffect(() => () => geometry.dispose(), [geometry]);
  useEffect(() => () => material.dispose(), [material]);

  /** Colour and size depend on role, not on time, so they are rewritten only when a role
   * changes — not on every propagation tick. */
  useEffect(() => {
    const { colors, sizes } = buffers;
    for (let i = 0; i < elements.length; i++) {
      const e = elements[i];
      if (e === undefined) continue;
      const isSelected = e.id === selectedId;
      const isHovered = e.id === hoveredId;
      const color = isSelected
        ? SELECTED_COLOR
        : colorFor(e.cls as CatalogClassName, e.rel as RelevanceName);
      const base = i * 3;
      colors[base] = color.r;
      colors[base + 1] = color.g;
      colors[base + 2] = color.b;

      sizes[i] = isSelected
        ? POINT_SIZE.selected
        : isHovered
          ? POINT_SIZE.hovered
          : e.rel === 'TARGET' || e.rel === 'RESCUE_VEHICLE'
            ? POINT_SIZE.mission
            : e.rel === 'CANDIDATE'
              ? POINT_SIZE.candidate
              : POINT_SIZE.base;
    }
    const colorAttr = geometry.getAttribute('pointColor');
    const sizeAttr = geometry.getAttribute('size');
    colorAttr.needsUpdate = true;
    sizeAttr.needsUpdate = true;
  }, [elements, selectedId, hoveredId, buffers, geometry]);

  const lastPropagationMs = useRef(0);
  const hasPropagated = useRef(false);

  useFrame((_state, delta) => {
    const handle = clock.current;
    const nowMs = handle.advance(delta);

    // Always propagate once so a paused or reduced-motion scene still shows real positions
    // rather than an empty sky.
    const due =
      !hasPropagated.current ||
      (!reducedMotion && Math.abs(nowMs - lastPropagationMs.current) >= propagationIntervalMs);
    if (!due) return;

    lastPropagationMs.current = nowMs;
    hasPropagated.current = true;

    const result = propagateInto(compiled, new Date(nowMs), buffers.positions, buffers.flags);

    // A point SGP4 could not place gets zero size, which the vertex shader turns into zero
    // alpha. Leaving it at its previous size would draw a satellite that is not there.
    for (let i = 0; i < buffers.count; i++) {
      if (buffers.flags[i] === PropagationFlag.Failed) buffers.sizes[i] = 0;
    }

    const positionAttr = geometry.getAttribute('position');
    positionAttr.needsUpdate = true;
    geometry.getAttribute('size').needsUpdate = true;
    onPropagationResult?.(result.ok, result.failed);
  });

  /**
   * Picking, done in screen space rather than through the Points raycaster.
   *
   * Three's Points raycasting selects by proximity to the ray in WORLD units, via a single
   * `threshold`. On a shell of ten thousand objects that threshold is impossible to tune: below
   * the ~0.037-unit mean spacing most clicks land in a gap and select nothing, and above it the
   * ray starts preferring whichever object is nearest along its length rather than nearest to
   * the cursor. Both were observed in the browser.
   *
   * Projecting every point to pixels and taking the nearest within a pixel radius gives the
   * behaviour a user actually expects — "the dot I clicked on" — at a cost of one matrix
   * transform per object, about half a millisecond for ten thousand.
   */
  const pick = useCallback(
    (clientX: number, clientY: number): number | null => {
      const canvas = gl.domElement;
      const rect = canvas.getBoundingClientRect();
      const px = clientX - rect.left;
      const py = clientY - rect.top;

      const camPos = camera.position;
      const positions = buffers.positions;
      let bestId: number | null = null;
      let bestDistance = PICK_RADIUS_PX;

      for (let i = 0; i < buffers.count; i++) {
        if (buffers.flags[i] === PropagationFlag.Failed) continue;
        const base = i * 3;
        const x = positions[base] ?? Number.NaN;
        const y = positions[base + 1] ?? Number.NaN;
        const z = positions[base + 2] ?? Number.NaN;
        if (!Number.isFinite(x)) continue;

        projected.set(x, y, z).project(camera);
        // Behind the camera, or outside the frustum.
        if (projected.z < -1 || projected.z > 1) continue;
        const sx = (projected.x * 0.5 + 0.5) * rect.width;
        const sy = (-projected.y * 0.5 + 0.5) * rect.height;
        const d = Math.hypot(sx - px, sy - py);
        if (d >= bestDistance) continue;

        // Occlusion: an object on the far side of Earth is drawn (additively, faintly) but
        // must not be selectable, or a click near the limb picks something the user cannot
        // see. Test whether the camera-to-object segment passes through the unit sphere.
        toPoint.set(x, y, z).sub(camPos);
        const segmentLength = toPoint.length();
        toPoint.divideScalar(segmentLength || 1);
        // Closest approach of the ray to Earth's centre.
        const along = -camPos.dot(toPoint);
        if (along > 0 && along < segmentLength) {
          const missDistanceSq = camPos.lengthSq() - along * along;
          if (missDistanceSq < 1) continue; // the segment clips the globe before arriving
        }

        bestDistance = d;
        bestId = elements[i]?.id ?? null;
      }
      return bestId;
    },
    [buffers, camera, elements, gl],
  );

  // Listeners live on the canvas rather than on the object, because the object is exactly what
  // cannot be hit reliably. Registered once and cleaned up on unmount.
  useEffect(() => {
    const canvas = gl.domElement;
    const onPointerDown = (event: PointerEvent) => {
      // Only a plain primary click selects; a drag is the camera's, not a selection gesture.
      if (event.button !== 0) return;
      downAt.current = { x: event.clientX, y: event.clientY };
    };
    const onPointerUp = (event: PointerEvent) => {
      const start = downAt.current;
      downAt.current = null;
      if (event.button !== 0 || start === null) return;
      // A pointer that moved more than a few pixels was an orbit drag; selecting on it would
      // change the selection every time the user rotated the globe.
      if (Math.hypot(event.clientX - start.x, event.clientY - start.y) > DRAG_SLOP_PX) return;
      onSelect(pick(event.clientX, event.clientY));
    };
    const onPointerMove = (event: PointerEvent) => {
      const now = event.timeStamp;
      if (now - lastHoverAt.current < HOVER_INTERVAL_MS) return;
      lastHoverAt.current = now;
      onHover(pick(event.clientX, event.clientY));
    };
    const onPointerLeave = () => onHover(null);

    canvas.addEventListener('pointerdown', onPointerDown);
    canvas.addEventListener('pointerup', onPointerUp);
    canvas.addEventListener('pointermove', onPointerMove);
    canvas.addEventListener('pointerleave', onPointerLeave);
    return () => {
      canvas.removeEventListener('pointerdown', onPointerDown);
      canvas.removeEventListener('pointerup', onPointerUp);
      canvas.removeEventListener('pointermove', onPointerMove);
      canvas.removeEventListener('pointerleave', onPointerLeave);
    };
  }, [gl, pick, onSelect, onHover]);

  return <points ref={pointsRef} geometry={geometry} material={material} frustumCulled={false} />;
}
