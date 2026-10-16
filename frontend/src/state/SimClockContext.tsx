/**
 * The orbital simulation clock — the single time source every propagated object reads.
 *
 * ## Why the authoritative time is a mutable ref, not React state
 *
 * Ten thousand satellites are re-propagated whenever this clock moves. If the current instant
 * lived in `useState`, every advance would re-render the tree that owns the point cloud, sixty
 * times a second. So the clock's truth is a mutable object read imperatively inside `useFrame`,
 * and React state carries only what a human actually needs to see change: whether it is running,
 * the rate, and a timestamp published at a readable cadence rather than per frame.
 *
 * That split is the same one `docs/VISUAL_DESIGN.md` §3 already draws between simulation state
 * and visualisation state — this is visualisation state, and it is never written back to the
 * engine. The C++ engine keeps its own simulation time; this clock drives catalogue propagation
 * only, and the two are deliberately not the same value.
 *
 * ## What this clock is NOT
 *
 * It is not telemetry. Advancing it does not observe anything — it evaluates SGP4 at a chosen
 * instant. `provenance` on every catalogue readout says exactly that, so a fast-forwarded scene
 * can never be mistaken for a live downlink.
 */

import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react';
import type { ReactNode } from 'react';

/** Rates offered in the UI. 1x is wall-clock; the rest are multiples of it. */
export const CLOCK_RATES = [-3600, -60, -1, 0, 1, 60, 600, 3600] as const;
export type ClockRate = number;

/**
 * The imperative half. Held in a ref and mutated in place, so reading the current instant inside
 * an animation frame costs a property access and allocates nothing.
 */
export interface SimClockHandle {
  /** Milliseconds since the Unix epoch for the instant currently being displayed. */
  epochMs: number;
  rate: number;
  playing: boolean;
  /** Advances the clock by one real frame. Returns the new instant. */
  advance: (deltaSeconds: number) => number;
}

export interface SimClockValue {
  /** Read this inside `useFrame`; never subscribe a component to it. */
  readonly handle: React.MutableRefObject<SimClockHandle>;
  /** Published at a readable cadence for display, NOT every frame. */
  readonly displayEpochMs: number;
  readonly playing: boolean;
  readonly rate: number;
  readonly play: () => void;
  readonly pause: () => void;
  readonly setRate: (rate: number) => void;
  /** Jump by a fixed number of seconds, in either direction, without changing the rate. */
  readonly step: (seconds: number) => void;
  /** Return to the wall-clock present. */
  readonly reset: () => void;
  /** True when the displayed instant is within a second of now. */
  readonly isLive: boolean;
}

const SimClockContext = createContext<SimClockValue | null>(null);

/** How often the human-readable timestamp is published. Four times a second reads as smooth
 * without turning the clock into a per-frame re-render engine. */
const DISPLAY_INTERVAL_MS = 250;

export function SimClockProvider({ children }: { readonly children: ReactNode }) {
  const handle = useRef<SimClockHandle>({
    epochMs: Date.now(),
    rate: 1,
    playing: true,
    advance(deltaSeconds: number) {
      if (this.playing) this.epochMs += deltaSeconds * 1000 * this.rate;
      return this.epochMs;
    },
  });

  const [displayEpochMs, setDisplayEpochMs] = useState(() => handle.current.epochMs);
  const [playing, setPlaying] = useState(true);
  const [rate, setRateState] = useState(1);

  // The display timestamp is polled from the authoritative ref rather than pushed to it. A
  // pull keeps the animation frame free of React entirely.
  useEffect(() => {
    const id = window.setInterval(() => {
      setDisplayEpochMs(handle.current.epochMs);
    }, DISPLAY_INTERVAL_MS);
    return () => window.clearInterval(id);
  }, []);

  const play = useCallback(() => {
    handle.current.playing = true;
    setPlaying(true);
  }, []);

  const pause = useCallback(() => {
    handle.current.playing = false;
    setPlaying(false);
  }, []);

  const setRate = useCallback((next: number) => {
    handle.current.rate = next;
    setRateState(next);
    // A rate of zero is a pause expressed as a rate; keeping `playing` true here would leave
    // the transport controls claiming the clock is running while it visibly is not.
    if (next === 0) {
      handle.current.playing = false;
      setPlaying(false);
    }
  }, []);

  const step = useCallback((seconds: number) => {
    handle.current.epochMs += seconds * 1000;
    setDisplayEpochMs(handle.current.epochMs);
  }, []);

  const reset = useCallback(() => {
    handle.current.epochMs = Date.now();
    setDisplayEpochMs(handle.current.epochMs);
  }, []);

  const value = useMemo<SimClockValue>(
    () => ({
      handle,
      displayEpochMs,
      playing,
      rate,
      play,
      pause,
      setRate,
      step,
      reset,
      // A second of tolerance: the display publishes at 4 Hz, so an exact comparison would
      // flicker between live and not-live while the clock is simply running at 1x.
      isLive: Math.abs(displayEpochMs - Date.now()) < 1000,
    }),
    [displayEpochMs, playing, rate, play, pause, setRate, step, reset],
  );

  return <SimClockContext.Provider value={value}>{children}</SimClockContext.Provider>;
}

export function useSimClock(): SimClockValue {
  const ctx = useContext(SimClockContext);
  if (!ctx) throw new Error('useSimClock() must be called within a <SimClockProvider>');
  return ctx;
}

/** `2026-09-08 03:41:02Z` — unambiguous, sortable, and explicitly UTC, as orbital work requires. */
export function formatSimTime(epochMs: number): string {
  const d = new Date(epochMs);
  if (!Number.isFinite(epochMs)) return '—';
  const pad = (n: number, w = 2) => String(n).padStart(w, '0');
  return (
    `${d.getUTCFullYear()}-${pad(d.getUTCMonth() + 1)}-${pad(d.getUTCDate())} ` +
    `${pad(d.getUTCHours())}:${pad(d.getUTCMinutes())}:${pad(d.getUTCSeconds())}Z`
  );
}

/** "1x", "60x", "3600x reverse", "held" — the rate as an operator would say it. */
export function formatRate(rate: number, playing: boolean): string {
  if (!playing || rate === 0) return 'held';
  if (rate === 1) return 'real time';
  const magnitude = Math.abs(rate) >= 3600 ? `${Math.abs(rate) / 3600}h/s` : `${Math.abs(rate)}x`;
  return rate < 0 ? `${magnitude} reverse` : magnitude;
}
