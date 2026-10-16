/**
 * One-shot enter-on-scroll observer.
 *
 * "One-shot" is the whole design: content that fades back out when it leaves the viewport
 * makes a long document feel unstable and forces a repeat animation on every scroll pass. Once
 * an element has been seen it stays revealed and the observer disconnects, so a page with
 * thirty revealed blocks holds zero live observers.
 *
 * The failure mode is chosen deliberately: whenever the mechanism is unavailable -- reduced
 * motion, no `IntersectionObserver` (jsdom), no element -- the hook reports `revealed: true`.
 * Content must never be left invisible because an enhancement did not load.
 */

import { useEffect, useRef, useState } from 'react';
import type { RefObject } from 'react';

import { usePrefersReducedMotion } from './usePrefersReducedMotion';

/** Fires a little before the element is fully in view, so the motion resolves as the reader
 * arrives rather than after they have already started reading. */
const OBSERVER_OPTIONS: IntersectionObserverInit = {
  rootMargin: '0px 0px -10% 0px',
  threshold: 0.08,
};

export function useScrollReveal<T extends HTMLElement>(): {
  readonly ref: RefObject<T>;
  readonly revealed: boolean;
} {
  const ref = useRef<T>(null);
  const reducedMotion = usePrefersReducedMotion();
  const [revealed, setRevealed] = useState(false);

  useEffect(() => {
    if (revealed) return undefined;

    if (reducedMotion || typeof IntersectionObserver === 'undefined') {
      setRevealed(true);
      return undefined;
    }

    const element = ref.current;
    if (!element) {
      setRevealed(true);
      return undefined;
    }

    const observer = new IntersectionObserver((entries) => {
      const entry = entries[0];
      if (entry?.isIntersecting) {
        setRevealed(true);
        observer.disconnect();
      }
    }, OBSERVER_OPTIONS);

    observer.observe(element);
    return () => observer.disconnect();
  }, [reducedMotion, revealed]);

  return { ref, revealed };
}
