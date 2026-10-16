/**
 * UI-only state — deliberately separate from `EngineContext` (brief §75: "keep engine
 * connection state / simulation snapshot / selection / UI settings separate"). Nothing
 * here is derived from or feeds back into simulation truth; it is purely presentation
 * preference (current page, presentation mode, scene toggles).
 *
 * Every setting below is REAL: each one is read by a specific component and visibly changes
 * the scene (Phase 12 brief §21's "every enabled setting must visibly affect the system").
 * The consumer is named in each field's own comment so a stale/dead setting is obvious.
 */

import { createContext, useContext, useMemo, useState } from 'react';
import type { ReactNode } from 'react';

export type PageId =
  | 'overview'
  | 'mission-control'
  | 'orbital-catalog'
  | 'mission-planner'
  | 'orbital-network'
  | 'optimization-lab'
  | 'algorithm-lab'
  | 'analytics'
  | 'mission-history'
  | 'scenario'
  | 'settings';

/** Visual constellation size tiers (Phase 12 brief §1/§19). `0` means "off" -- Mission
 * Control then shows only the real planning entities, no synthetic backdrop. */
export type ConstellationSize = 0 | 2000 | 5000 | 10000;

interface UIContextValue {
  readonly page: PageId;
  readonly setPage: (page: PageId) => void;
  readonly presentationMode: boolean;
  readonly setPresentationMode: (on: boolean) => void;

  /** `EntityMarkerField` / `ConstellationLayer`: skips animated pulse rings. */
  readonly reducedMotion: boolean;
  readonly setReducedMotion: (on: boolean) => void;
  /** `Earth`: adds the presentational idle drift on top of the real sidereal angle. */
  readonly earthRotation: boolean;
  readonly setEarthRotation: (on: boolean) => void;
  /** `Earth`: renders the cloud shell. */
  readonly showClouds: boolean;
  readonly setShowClouds: (on: boolean) => void;
  /** `Earth`: renders the two additive atmosphere shells. */
  readonly showAtmosphere: boolean;
  readonly setShowAtmosphere: (on: boolean) => void;
  /** `Scene`: renders drei's `<Stars>` deep-space backdrop. */
  readonly showStarfield: boolean;
  readonly setShowStarfield: (on: boolean) => void;
  /** `Scene`: renders per-entity orbit rings for the real planning entities. */
  readonly showOrbitRings: boolean;
  readonly setShowOrbitRings: (on: boolean) => void;
  /** `ConstellationLayer`: renders the batched orbital-shell ribbons. */
  readonly showConstellationRibbons: boolean;
  readonly setShowConstellationRibbons: (on: boolean) => void;
  /** `Scene`: renders mission transfer trajectories. */
  readonly showTrajectories: boolean;
  readonly setShowTrajectories: (on: boolean) => void;
  /** `Scene`/`ConstellationLayer`: how large the synthetic visual constellation is. */
  readonly constellationSize: ConstellationSize;
  readonly setConstellationSize: (n: ConstellationSize) => void;
  /** `ConstellationLayer`/`EntityMarkerField`: colour objects by real distance from the
   * selection instead of by status (Phase 12 brief §8). */
  readonly proximityMode: boolean;
  readonly setProximityMode: (on: boolean) => void;
  /** `coordinates.ts`'s `applyAltitudeExaggeration()`: how much to push orbital shells apart
   * radially so they are distinguishable. 1.0 = true scale. A VISUALIZATION transform only --
   * never applied to any value sent to or received from the engine (brief §23). */
  readonly altitudeExaggeration: number;
  readonly setAltitudeExaggeration: (x: number) => void;
  /** `ConstellationLayer`: caps instance count and drops ribbons for weaker GPUs. */
  readonly performanceMode: boolean;
  readonly setPerformanceMode: (on: boolean) => void;
}

const UIContext = createContext<UIContextValue | null>(null);

/** The OS-level "reduce motion" accessibility preference, if the environment reports one --
 * `false` in any environment without `matchMedia`. Read once at mount; the Settings toggle is
 * the ongoing source of truth after that, so a later OS-level change never silently
 * overrides a choice the user already made in-app. */
function prefersReducedMotion(): boolean {
  if (typeof window === 'undefined' || typeof window.matchMedia !== 'function') return false;
  return window.matchMedia('(prefers-reduced-motion: reduce)').matches;
}

export function UIProvider({ children }: { children: ReactNode }) {
  // The dashboard, not the 3-D view: a reader arriving at the workspace should first see what
  // the system knows, then go looking at the globe. Mission Control is one click away.
  const [page, setPage] = useState<PageId>('overview');
  const [presentationMode, setPresentationMode] = useState(false);
  const [reducedMotion, setReducedMotion] = useState(prefersReducedMotion);
  const [earthRotation, setEarthRotation] = useState(true);
  const [showClouds, setShowClouds] = useState(true);
  const [showAtmosphere, setShowAtmosphere] = useState(true);
  const [showStarfield, setShowStarfield] = useState(true);
  const [showOrbitRings, setShowOrbitRings] = useState(true);
  const [showConstellationRibbons, setShowConstellationRibbons] = useState(true);
  const [showTrajectories, setShowTrajectories] = useState(true);
  const [constellationSize, setConstellationSize] = useState<ConstellationSize>(5000);
  const [proximityMode, setProximityMode] = useState(false);
  const [altitudeExaggeration, setAltitudeExaggeration] = useState(6);
  const [performanceMode, setPerformanceMode] = useState(false);

  const value = useMemo<UIContextValue>(
    () => ({
      page,
      setPage,
      presentationMode,
      setPresentationMode,
      reducedMotion,
      setReducedMotion,
      earthRotation,
      setEarthRotation,
      showClouds,
      setShowClouds,
      showAtmosphere,
      setShowAtmosphere,
      showStarfield,
      setShowStarfield,
      showOrbitRings,
      setShowOrbitRings,
      showConstellationRibbons,
      setShowConstellationRibbons,
      showTrajectories,
      setShowTrajectories,
      constellationSize,
      setConstellationSize,
      proximityMode,
      setProximityMode,
      altitudeExaggeration,
      setAltitudeExaggeration,
      performanceMode,
      setPerformanceMode,
    }),
    [
      page,
      presentationMode,
      reducedMotion,
      earthRotation,
      showClouds,
      showAtmosphere,
      showStarfield,
      showOrbitRings,
      showConstellationRibbons,
      showTrajectories,
      constellationSize,
      proximityMode,
      altitudeExaggeration,
      performanceMode,
    ],
  );

  return <UIContext.Provider value={value}>{children}</UIContext.Provider>;
}

export function useUI(): UIContextValue {
  const ctx = useContext(UIContext);
  if (!ctx) throw new Error('useUI() must be called within a <UIProvider>');
  return ctx;
}
