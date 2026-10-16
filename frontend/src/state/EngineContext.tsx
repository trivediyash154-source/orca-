/**
 * Central application state — brief §33: connectionStatus, simulationSnapshot,
 * selectedObject, recentEvents (`snapshot.recentEvents` directly; nothing duplicated here).
 * This is only the RECEIVED representation of engine state — it never independently
 * computes anything the engine could compute (brief §33, §57).
 */

import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react';
import type { ReactNode } from 'react';

import {
  EngineDownError,
  fetchHealth,
  fetchSnapshot,
  sendAdvance,
  sendRescoreParetoFront,
  sendRunAlgorithm,
  sendSetSpeed,
  sendSimCommand,
  type RunnableAlgorithm,
  type SimCommand,
} from '../services/engineService';
import { demoSnapshot } from '../services/demoEngine';
import type { SelectableEntity, SimulationSnapshot } from '../types/engine';

/** Brief §37's five states, verbatim. */
export type ConnectionStatus = 'Connecting' | 'Connected' | 'EngineStarting' | 'Disconnected' | 'Error';

/** Must match `cpp/io/JsonSerializer.cpp`'s `kSchemaVersion` / `main.cpp`'s `ready` message
 * (`INTEGRATION_ARCHITECTURE.md` §3). A mismatch means this build of the frontend cannot
 * safely assume the snapshot shape it expects -- brief §54: "the frontend should be able to
 * detect incompatibility rather than rendering corrupted data." */
export const EXPECTED_SCHEMA_VERSION = 1;

/** How many consecutive unanswered probes mean "there is no bridge here", rather than "the
 * engine is still starting". At the default 500 ms interval this is a ~3 s wait, which is long
 * enough for a local `npm run dev` backend to come up and short enough that a static visitor
 * is not left staring at a spinner. */
const DEMO_FALLBACK_ATTEMPTS = 6;

/** `VITE_DEMO_MODE=1` skips probing entirely -- the setting a static deployment uses, so no
 * visitor pays for six requests to a host that is not there. */
const FORCE_DEMO = import.meta.env.VITE_DEMO_MODE === '1';

/** The demo clock does not start at zero: T+0 has no emergencies, no missions and no events,
 * and a dashboard of empty panels is a poor first impression of work that is not empty. This
 * lands mid-scenario, with missions in flight and some already completed. */
const DEMO_START_SEC = 7_200;

const DEMO_TICK_MS = 1_000;

interface EngineContextValue {
  /** Exposed (not just used internally) so pages that need a REST call outside the
   * standard command set -- e.g. the Scenario page's `GET /visual/constellation` (Phase 12
   * brief §5) -- don't need a second way to learn the backend's own address. */
  readonly baseUrl: string;
  readonly connectionStatus: ConnectionStatus;
  readonly snapshot: SimulationSnapshot | null;
  readonly selected: SelectableEntity | null;
  readonly select: (entity: SelectableEntity | null) => void;
  readonly sendCommand: (cmd: SimCommand) => Promise<void>;
  readonly advance: (seconds: number) => Promise<void>;
  readonly setSpeed: (multiplier: number) => Promise<void>;
  readonly rescoreParetoFront: (missionId: string, profile: string) => Promise<void>;
  /** Phase 12 brief §16: runs a real algorithm now. Unlike the other commands here, this
   * one resolves to `true`/`false` (not just void) so a caller (Algorithm Lab's per-button
   * UI) can show its OWN result inline without racing another component's `lastError`. */
  readonly runAlgorithm: (algorithm: RunnableAlgorithm, vehicleId?: string, emergencyId?: string) => Promise<boolean>;
  readonly lastError: string | null;
  /**
   * Which process produced `snapshot`. `'engine'` is the real C++ engine behind the Node
   * bridge; `'demo'` is `services/demoEngine.ts`'s in-browser kinematic model, used when no
   * bridge is reachable (a static deployment, or a checkout with no build toolchain).
   *
   * Exposed so the UI can SAY which one it is showing. A silent fallback would be the worst
   * of the three options: better a labelled model than either a dead workspace or an
   * unlabelled imitation of engine output.
   */
  readonly dataSource: 'engine' | 'demo';
}

// Exported (not just used internally) so tests can render components against an injected
// context value directly, without going through the fetch-polling `EngineProvider` --
// `test/testUtils.tsx` is the one place that does this.
export const EngineContext = createContext<EngineContextValue | null>(null);
export type { EngineContextValue };

export interface EngineProviderProps {
  readonly baseUrl: string;
  readonly pollIntervalMs?: number;
  readonly children: ReactNode;
}

export function EngineProvider({ baseUrl, pollIntervalMs = 500, children }: EngineProviderProps) {
  const [status, setStatus] = useState<ConnectionStatus>('Connecting');
  const [snapshot, setSnapshot] = useState<SimulationSnapshot | null>(null);
  const [selected, setSelected] = useState<SelectableEntity | null>(null);
  const [lastError, setLastError] = useState<string | null>(null);
  const hasConnectedOnce = useRef(false);

  /** `'engine'` until the bridge has been given a fair chance to answer. Flipping to `'demo'`
   * is one-way for the lifetime of the provider: a bridge that appears later must not yank the
   * floor out from under a workspace someone is already reading. */
  const [dataSource, setDataSource] = useState<'engine' | 'demo'>(() =>
    FORCE_DEMO ? 'demo' : 'engine',
  );

  /** Demo-mode clock, in simulation seconds. A ref rather than state because the ticker writes
   * it every frame-ish interval and only the derived snapshot needs to re-render. */
  const demoTimeRef = useRef(DEMO_START_SEC);
  const demoStatusRef = useRef<'RUNNING' | 'PAUSED'>('RUNNING');
  const demoSpeedRef = useRef(1);

  const publishDemo = useCallback(() => {
    setSnapshot(
      demoSnapshot({
        timeSec: demoTimeRef.current,
        status: demoStatusRef.current,
        speedMultiplier: demoSpeedRef.current,
      }),
    );
    setStatus('Connected');
    setLastError(null);
  }, []);

  // The demo ticker. Mounted only once `dataSource` is `'demo'`, so the real path pays nothing
  // for it.
  useEffect(() => {
    if (dataSource !== 'demo') return undefined;

    publishDemo();
    const interval = setInterval(() => {
      if (demoStatusRef.current === 'RUNNING') {
        demoTimeRef.current += (DEMO_TICK_MS / 1000) * demoSpeedRef.current;
      }
      publishDemo();
    }, DEMO_TICK_MS);

    return () => clearInterval(interval);
  }, [dataSource, publishDemo]);

  useEffect(() => {
    if (dataSource === 'demo') return undefined;

    let cancelled = false;
    let timer: ReturnType<typeof setTimeout> | undefined;
    let failures = 0;

    async function poll(): Promise<void> {
      try {
        const health = await fetchHealth(baseUrl);
        if (cancelled) return;
        if (health.schemaVersion !== null && health.schemaVersion !== EXPECTED_SCHEMA_VERSION) {
          // Self-review finding (brief §54/§79 "can schema changes silently break the UI?"):
          // previously nothing compared this at all. A mismatched engine build is reported
          // as an explicit error rather than trusted to still match this frontend's types.
          setStatus('Error');
          setLastError(
            `engine schema v${health.schemaVersion} does not match this frontend's expected v${EXPECTED_SCHEMA_VERSION}`,
          );
        } else if (health.engineStatus === 'CRASHED') {
          // Distinct from STARTING (self-review finding): a crashed engine will not become
          // ready on its own, so it must not read as "wait, it's coming up."
          setStatus(hasConnectedOnce.current ? 'Disconnected' : 'Error');
          setLastError('engine process crashed');
        } else if (!health.engineUp) {
          setStatus('EngineStarting');
        } else {
          const snap = await fetchSnapshot(baseUrl);
          if (cancelled) return;
          // Brief §82: a fresh snapshot always REPLACES the old one wholesale -- never
          // merged field-by-field, so a backend restart's new authoritative state can never
          // be blended with stale data from before the disconnect.
          setSnapshot(snap);
          setStatus('Connected');
          hasConnectedOnce.current = true;
          setLastError(null);
        }
      } catch (err) {
        if (cancelled) return;

        failures += 1;
        // Nothing has ever answered on this base URL, and it has now been asked often enough
        // that "still starting" is not a credible explanation. Rather than show an offline
        // overlay forever -- which is what a static deployment with no bridge would do -- hand
        // the workspace the in-browser model, clearly labelled as such.
        if (!hasConnectedOnce.current && failures >= DEMO_FALLBACK_ATTEMPTS) {
          setDataSource('demo');
          return;
        }

        if (err instanceof EngineDownError) {
          setStatus('EngineStarting');
        } else {
          setStatus(hasConnectedOnce.current ? 'Disconnected' : 'Connecting');
          setLastError(err instanceof Error ? err.message : String(err));
        }
      } finally {
        if (!cancelled) {
          timer = setTimeout(() => void poll(), pollIntervalMs);
        }
      }
    }

    void poll();
    return () => {
      cancelled = true;
      if (timer) clearTimeout(timer);
    };
  }, [baseUrl, pollIntervalMs, dataSource]);

  const sendCommand = useCallback(
    async (cmd: SimCommand) => {
      if (dataSource === 'demo') {
        // The demo model's clock is the only state a command can touch; everything else in the
        // snapshot is a pure function of it.
        if (cmd === 'pause') demoStatusRef.current = 'PAUSED';
        else if (cmd === 'resume' || cmd === 'start') demoStatusRef.current = 'RUNNING';
        else if (cmd === 'reset') demoTimeRef.current = DEMO_START_SEC;
        else if (cmd === 'step') demoTimeRef.current += 1;
        publishDemo();
        return;
      }
      try {
        const snap = await sendSimCommand(baseUrl, cmd);
        setSnapshot(snap);
        setLastError(null);
      } catch (err) {
        setLastError(err instanceof Error ? err.message : String(err));
      }
    },
    [baseUrl, dataSource, publishDemo],
  );

  const advance = useCallback(
    async (seconds: number) => {
      if (dataSource === 'demo') {
        demoTimeRef.current = Math.max(0, demoTimeRef.current + seconds);
        publishDemo();
        return;
      }
      try {
        const snap = await sendAdvance(baseUrl, seconds);
        setSnapshot(snap);
        setLastError(null);
      } catch (err) {
        setLastError(err instanceof Error ? err.message : String(err));
      }
    },
    [baseUrl, dataSource, publishDemo],
  );

  const setSpeed = useCallback(
    async (multiplier: number) => {
      if (dataSource === 'demo') {
        demoSpeedRef.current = multiplier;
        publishDemo();
        return;
      }
      try {
        const snap = await sendSetSpeed(baseUrl, multiplier);
        setSnapshot(snap);
        setLastError(null);
      } catch (err) {
        setLastError(err instanceof Error ? err.message : String(err));
      }
    },
    [baseUrl, dataSource, publishDemo],
  );

  const rescoreParetoFront = useCallback(
    async (missionId: string, profile: string) => {
      if (dataSource === 'demo') {
        // Re-scoring is a property of the engine's planner. The model has no second front to
        // offer, and inventing one would misrepresent what a profile change does.
        setLastError(
          `Pareto re-scoring for ${missionId} under the "${profile}" profile needs the C++ ` +
            'planner; the browser model cannot re-derive a front. Nothing was changed.',
        );
        return;
      }
      try {
        const snap = await sendRescoreParetoFront(baseUrl, missionId, profile);
        setSnapshot(snap);
        setLastError(null);
      } catch (err) {
        setLastError(err instanceof Error ? err.message : String(err));
      }
    },
    [baseUrl, dataSource],
  );

  const runAlgorithm = useCallback(
    async (algorithm: RunnableAlgorithm, vehicleId?: string, emergencyId?: string) => {
      if (dataSource === 'demo') {
        setLastError(
          `${algorithm} runs inside the C++ engine. The browser model reports representative ` +
            'telemetry for the runs already in the snapshot, but cannot execute a new search.',
        );
        return false;
      }
      try {
        const snap = await sendRunAlgorithm(baseUrl, algorithm, vehicleId, emergencyId);
        setSnapshot(snap);
        setLastError(null);
        return true;
      } catch (err) {
        setLastError(err instanceof Error ? err.message : String(err));
        return false;
      }
    },
    [baseUrl, dataSource],
  );

  const value = useMemo<EngineContextValue>(
    () => ({
      baseUrl,
      connectionStatus: status,
      snapshot,
      selected,
      select: setSelected,
      sendCommand,
      advance,
      setSpeed,
      rescoreParetoFront,
      runAlgorithm,
      lastError,
      dataSource,
    }),
    [
      baseUrl,
      status,
      snapshot,
      selected,
      sendCommand,
      advance,
      setSpeed,
      rescoreParetoFront,
      runAlgorithm,
      lastError,
      dataSource,
    ],
  );

  return <EngineContext.Provider value={value}>{children}</EngineContext.Provider>;
}

export function useEngine(): EngineContextValue {
  const ctx = useContext(EngineContext);
  if (!ctx) {
    throw new Error('useEngine() must be called within an <EngineProvider>');
  }
  return ctx;
}
