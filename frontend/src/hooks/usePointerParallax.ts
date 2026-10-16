/**
 * Normalised, smoothed pointer offset from the centre of an element, in [-1, 1] on both axes.
 *
 * Used for the small depth response on the hero frame. Three deliberate constraints:
 *
 *  - Under reduced motion it returns a constant {0, 0} and attaches no listener at all, so the
 *    cost is not merely hidden, it is not paid.
 *  - On a coarse pointer (touch) it also attaches nothing: a touch "hover" is a tap, and
 *    parallax driven by taps reads as a glitch rather than as depth.
 *  - The value is lerped toward the pointer inside a rAF loop that stops itself once settled,
 *    rather than re-rendering on every raw pointermove. React state updates at pointer-event
 *    rate would dominate the frame budget of the very scene this is meant to enhance.
 */

import { useEffect, useRef, useState } from 'react';
import type { RefObject } from 'react';

import { usePrefersReducedMotion } from './usePrefersReducedMotion';

/** Fraction of the remaining distance covered per frame. 0.12 settles in ~20 frames: slow
 * enough to read as inertia, fast enough that it never feels like lag. */
const SMOOTHING = 0.12;

/** Below this the loop stops. Roughly a quarter of a pixel of visible travel at hero scale. */
const SETTLE_EPSILON = 0.0015;

interface Offset {
  x: number;
  y: number;
}

function clampUnit(value: number): number {
  return Math.min(1, Math.max(-1, value));
}

function isCoarsePointer(): boolean {
  if (typeof window === 'undefined' || typeof window.matchMedia !== 'function') return false;
  try {
    return window.matchMedia('(pointer: coarse)').matches;
  } catch {
    return false;
  }
}

export function usePointerParallax<T extends HTMLElement>(): {
  readonly ref: RefObject<T>;
  readonly x: number;
  readonly y: number;
} {
  const ref = useRef<T>(null);
  const reducedMotion = usePrefersReducedMotion();
  const [offset, setOffset] = useState<Offset>({ x: 0, y: 0 });

  const targetRef = useRef<Offset>({ x: 0, y: 0 });
  const currentRef = useRef<Offset>({ x: 0, y: 0 });
  const frameRef = useRef<number | null>(null);

  useEffect(() => {
    const element = ref.current;
    if (reducedMotion || typeof window === 'undefined' || !element || isCoarsePointer()) {
      // Reset rather than freeze: if the reader turns reduced motion on mid-session the frame
      // must return to its neutral position instead of keeping its last tilt forever.
      targetRef.current = { x: 0, y: 0 };
      currentRef.current = { x: 0, y: 0 };
      setOffset({ x: 0, y: 0 });
      return undefined;
    }

    function requestFrame(): void {
      if (frameRef.current === null) {
        frameRef.current = window.requestAnimationFrame(step);
      }
    }

    function step(): void {
      frameRef.current = null;
      const current = currentRef.current;
      const target = targetRef.current;
      const nextX = current.x + (target.x - current.x) * SMOOTHING;
      const nextY = current.y + (target.y - current.y) * SMOOTHING;
      currentRef.current = { x: nextX, y: nextY };
      setOffset({ x: nextX, y: nextY });

      const settled =
        Math.abs(target.x - nextX) < SETTLE_EPSILON && Math.abs(target.y - nextY) < SETTLE_EPSILON;
      if (!settled) requestFrame();
    }

    function handlePointerMove(event: PointerEvent): void {
      const rect = element?.getBoundingClientRect();
      if (!rect || rect.width === 0 || rect.height === 0) return;
      targetRef.current = {
        x: clampUnit(((event.clientX - rect.left) / rect.width) * 2 - 1),
        y: clampUnit(((event.clientY - rect.top) / rect.height) * 2 - 1),
      };
      requestFrame();
    }

    function handlePointerLeave(): void {
      targetRef.current = { x: 0, y: 0 };
      requestFrame();
    }

    element.addEventListener('pointermove', handlePointerMove);
    element.addEventListener('pointerleave', handlePointerLeave);
    element.addEventListener('pointercancel', handlePointerLeave);

    return () => {
      element.removeEventListener('pointermove', handlePointerMove);
      element.removeEventListener('pointerleave', handlePointerLeave);
      element.removeEventListener('pointercancel', handlePointerLeave);
      if (frameRef.current !== null) {
        window.cancelAnimationFrame(frameRef.current);
        frameRef.current = null;
      }
    };
  }, [reducedMotion]);

  return { ref, x: offset.x, y: offset.y };
}
