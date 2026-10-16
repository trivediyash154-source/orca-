/**
 * One-shot WebGL capability probe for the entry surface.
 *
 * The landing hero is a real 3D scene, so it needs an answer before it mounts a renderer that
 * would otherwise throw on a machine with WebGL disabled, blocklisted or software-blocked. The
 * probe runs in an effect rather than during render so the first paint is never blocked, which
 * is why `'probing'` exists as a distinct state: callers must render a neutral placeholder for
 * one frame instead of flashing the fallback and then replacing it.
 */

import { useEffect, useState } from 'react';

export type WebGLSupport = 'probing' | 'available' | 'unavailable';

interface LoseContextExtension {
  loseContext(): void;
}

/**
 * Browsers cap the number of simultaneously live WebGL contexts (commonly 16) and evict the
 * oldest when the cap is hit -- which would kill the hero's own renderer. Releasing the probe
 * context immediately keeps the probe from competing for that budget.
 *
 * The lookup is structural rather than through the `WebGLRenderingContext | WebGL2RenderingContext`
 * union: both declare `getExtension` with multiple overloads, and TypeScript cannot resolve a
 * call on a union where more than one member is overloaded.
 */
function releaseProbeContext(context: object): void {
  try {
    const holder = context as { getExtension?: (name: string) => unknown };
    const extension = holder.getExtension?.('WEBGL_lose_context') as LoseContextExtension | null;
    extension?.loseContext();
  } catch {
    // A context that refuses to release is still a context that exists -- nothing to report.
  }
}

function probeWebGL(): boolean {
  if (typeof document === 'undefined' || typeof document.createElement !== 'function') {
    return false;
  }
  try {
    const canvas = document.createElement('canvas');
    const gl2 = canvas.getContext('webgl2');
    if (gl2) {
      releaseProbeContext(gl2);
      return true;
    }
    const gl1 = canvas.getContext('webgl');
    if (gl1) {
      releaseProbeContext(gl1);
      return true;
    }
    return false;
  } catch {
    // jsdom's `getContext` throws "not implemented"; a hard failure here is the same answer
    // as a missing context, so it is treated as one instead of propagating.
    return false;
  }
}

export function useWebGLSupport(): WebGLSupport {
  const [support, setSupport] = useState<WebGLSupport>('probing');

  useEffect(() => {
    setSupport(probeWebGL() ? 'available' : 'unavailable');
  }, []);

  return support;
}
