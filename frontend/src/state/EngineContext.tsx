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
import type { SelectableEntity, SimulationSnapshot } from '../types/engine';

/** Brief §37's five states, verbatim. */
export type ConnectionStatus = 'Connecting' | 'Connected' | 'EngineStarting' | 'Disconnected' | 'Error';

/** Must match `cpp/io/JsonSerializer.cpp`'s `kSchemaVersion` / `main.cpp`'s `ready` message
 * (`INTEGRATION_ARCHITECTURE.md` §3). A mismatch means this build of the frontend cannot
 * safely assume the snapshot shape it expects -- brief §54: "the frontend should be able to
 * detect incompatibility rather than rendering corrupted data." */
export const EXPECTED_SCHEMA_VERSION = 1;

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

  useEffect(() => {
    let cancelled = false;
    let timer: ReturnType<typeof setTimeout> | undefined;

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
  }, [baseUrl, pollIntervalMs]);

  const sendCommand = useCallback(
    async (cmd: SimCommand) => {
      try {
        const snap = await sendSimCommand(baseUrl, cmd);
        setSnapshot(snap);
        setLastError(null);
      } catch (err) {
        setLastError(err instanceof Error ? err.message : String(err));
      }
    },
    [baseUrl],
  );

  const advance = useCallback(
    async (seconds: number) => {
      try {
        const snap = await sendAdvance(baseUrl, seconds);
        setSnapshot(snap);
        setLastError(null);
      } catch (err) {
        setLastError(err instanceof Error ? err.message : String(err));
      }
    },
    [baseUrl],
  );

  const setSpeed = useCallback(
    async (multiplier: number) => {
      try {
        const snap = await sendSetSpeed(baseUrl, multiplier);
        setSnapshot(snap);
        setLastError(null);
      } catch (err) {
        setLastError(err instanceof Error ? err.message : String(err));
      }
    },
    [baseUrl],
  );

  const rescoreParetoFront = useCallback(
    async (missionId: string, profile: string) => {
      try {
        const snap = await sendRescoreParetoFront(baseUrl, missionId, profile);
        setSnapshot(snap);
        setLastError(null);
      } catch (err) {
        setLastError(err instanceof Error ? err.message : String(err));
      }
    },
    [baseUrl],
  );

  const runAlgorithm = useCallback(
    async (algorithm: RunnableAlgorithm, vehicleId?: string, emergencyId?: string) => {
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
    [baseUrl],
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
