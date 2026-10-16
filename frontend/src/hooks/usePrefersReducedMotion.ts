/**
 * Live subscription to `prefers-reduced-motion`, not a one-shot read. A reader can flip the OS
 * setting while the page is open -- typically because the page just made them uncomfortable --
 * and a hook that only sampled at mount would ignore exactly the request that matters most.
 *
 * Every animated surface on the entry pages consults this: CSS handles what CSS can, and this
 * hook covers the rest (WebGL frame loops, pointer parallax, scroll reveal), which no media
 * query can reach.
 */

import { useEffect, useState } from 'react';

const REDUCED_MOTION_QUERY = '(prefers-reduced-motion: reduce)';

/** jsdom builds without `matchMedia`, and some embedded webviews throw on an unknown feature
 * query, so every access is funnelled through here rather than guarded at each call site. */
function safeMatchMedia(): MediaQueryList | null {
  if (typeof window === 'undefined' || typeof window.matchMedia !== 'function') {
    return null;
  }
  try {
    return window.matchMedia(REDUCED_MOTION_QUERY);
  } catch {
    return null;
  }
}

export function usePrefersReducedMotion(): boolean {
  const [reduced, setReduced] = useState<boolean>(() => safeMatchMedia()?.matches ?? false);

  useEffect(() => {
    const query = safeMatchMedia();
    if (!query) return undefined;

    // Re-read on mount: the initial state ran before hydration on any server-rendered pass.
    setReduced(query.matches);

    function handleChange(event: MediaQueryListEvent): void {
      setReduced(event.matches);
    }

    if (typeof query.addEventListener === 'function') {
      query.addEventListener('change', handleChange);
      return () => query.removeEventListener('change', handleChange);
    }

    // Safari below 14 and older jsdom shims expose only the deprecated listener pair.
    if (typeof query.addListener === 'function') {
      query.addListener(handleChange);
      return () => query.removeListener(handleChange);
    }

    return undefined;
  }, []);

  return reduced;
}
