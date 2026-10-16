/**
 * The entry surface's router. Hash-based and hand-written, because adding `react-router` for
 * four static views would be a dependency this repo does not need.
 *
 * The hash is the source of truth, so browser back/forward, a bookmarked `#/signin` and a
 * reload all behave. `navigate()` also sets local state directly: navigating to the route you
 * are already on (landing -> landing with a section target) writes the same hash, which fires
 * no `hashchange`, and the section request would otherwise be dropped.
 *
 * Every `window`/`document` access is guarded. The workspace's test suite renders these
 * components under jsdom, where `scrollTo`/`scrollIntoView` may be absent or unimplemented.
 */

import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react';
import type { ReactNode } from 'react';

import { usePrefersReducedMotion } from '../hooks/usePrefersReducedMotion';

export type RouteName = 'landing' | 'signin' | 'signup' | 'workspace';

export interface RouteContextValue {
  readonly route: RouteName;
  readonly navigate: (route: RouteName, options?: { readonly section?: string }) => void;
  /** id of a landing section to scroll to once the landing view is mounted; consumed via
   * `useLandingScrollTarget()`. */
  readonly pendingSection: string | null;
  readonly clearPendingSection: () => void;
}

const ROUTE_HASH: Readonly<Record<RouteName, string>> = {
  landing: '#/',
  signin: '#/signin',
  signup: '#/signup',
  workspace: '#/workspace',
};

/** Tolerant on the way in, canonical on the way out: `#signin`, `#/signin` and `#/signin/`
 * all resolve, but `navigate()` only ever writes the canonical form from `ROUTE_HASH`. An
 * unrecognised hash is the landing page rather than an error view -- a stale link should land
 * somewhere useful. */
function routeFromHash(rawHash: string): RouteName {
  const key = rawHash
    .replace(/^#/, '')
    .replace(/^\/+/, '')
    .replace(/\/+$/, '')
    .toLowerCase();
  if (key === 'signin') return 'signin';
  if (key === 'signup') return 'signup';
  if (key === 'workspace') return 'workspace';
  return 'landing';
}

function readRouteFromLocation(): RouteName {
  if (typeof window === 'undefined') return 'landing';
  return routeFromHash(window.location.hash);
}

function scrollWindowToTop(): void {
  if (typeof window === 'undefined' || typeof window.scrollTo !== 'function') return;
  try {
    window.scrollTo({ top: 0, left: 0, behavior: 'auto' });
  } catch {
    // jsdom reports `scrollTo` as not implemented; a failed scroll is cosmetic, never fatal.
  }
}

const RouteContext = createContext<RouteContextValue | null>(null);

export function RouteProvider({ children }: { readonly children: ReactNode }): JSX.Element {
  const [route, setRoute] = useState<RouteName>(readRouteFromLocation);
  const [pendingSection, setPendingSection] = useState<string | null>(null);

  // Mirrors `pendingSection` for the scroll-reset effect below, which needs the value that was
  // set during this navigation rather than the one from the render that scheduled it.
  const pendingSectionRef = useRef<string | null>(null);

  useEffect(() => {
    if (typeof window === 'undefined') return undefined;

    function handleHashChange(): void {
      setRoute(readRouteFromLocation());
    }

    // A hash written between first render and this effect (or by a redirect) would otherwise
    // be missed, so the current location is re-read on mount.
    handleHashChange();

    window.addEventListener('hashchange', handleHashChange);
    return () => window.removeEventListener('hashchange', handleHashChange);
  }, []);

  // A new view starts at the top -- except when the navigation was itself a request for a
  // specific section, where scrolling to the top first would produce a visible double jump.
  useEffect(() => {
    if (pendingSectionRef.current !== null) return;
    scrollWindowToTop();
  }, [route]);

  const navigate = useCallback((next: RouteName, options?: { readonly section?: string }) => {
    const section = next === 'landing' ? (options?.section ?? null) : null;
    pendingSectionRef.current = section;
    setPendingSection(section);
    setRoute(next);

    if (typeof window === 'undefined') return;
    const target = ROUTE_HASH[next];
    if (window.location.hash !== target) {
      window.location.hash = target;
    }
  }, []);

  const clearPendingSection = useCallback(() => {
    pendingSectionRef.current = null;
    setPendingSection(null);
  }, []);

  const value = useMemo<RouteContextValue>(
    () => ({ route, navigate, pendingSection, clearPendingSection }),
    [route, navigate, pendingSection, clearPendingSection],
  );

  return <RouteContext.Provider value={value}>{children}</RouteContext.Provider>;
}

export function useRoute(): RouteContextValue {
  const value = useContext(RouteContext);
  if (!value) {
    throw new Error('useRoute() requires a <RouteProvider> ancestor');
  }
  return value;
}

/**
 * Consumes a pending section request. Called once by the landing page: by the time its effects
 * run, every section it renders is already committed to the DOM, so a single lookup is enough
 * and the request is cleared whether or not the id resolved -- an unresolvable id must not be
 * left queued to fire on some later, unrelated render.
 */
export function useLandingScrollTarget(): void {
  const { pendingSection, clearPendingSection } = useRoute();
  const reducedMotion = usePrefersReducedMotion();

  useEffect(() => {
    if (pendingSection === null) return;

    if (typeof document !== 'undefined') {
      const element = document.getElementById(pendingSection);
      if (element && typeof element.scrollIntoView === 'function') {
        try {
          element.scrollIntoView({
            behavior: reducedMotion ? 'auto' : 'smooth',
            block: 'start',
          });
        } catch {
          // Not implemented under jsdom; the section is still mounted and reachable.
        }
      }
    }

    clearPendingSection();
  }, [pendingSection, reducedMotion, clearPendingSection]);
}
