/**
 * The one module that talks to the Node bridge over HTTP — brief §36: "React components
 * should not directly manipulate process streams." No component calls `fetch()` directly
 * against the backend; everything goes through the functions here, so there is exactly one
 * place that knows the backend's base URL and REST shape.
 */

import type { SimulationSnapshot, VisualConstellationSnapshot } from '../types/engine';

export interface HealthStatus {
  readonly status: 'ok' | 'degraded';
  readonly engineUp: boolean;
  /** The engine process's own raw lifecycle state -- distinct from `engineUp`, which
   * cannot by itself distinguish "still starting" from "crashed and not coming back."
   * Self-review finding (`INTEGRATION_ARCHITECTURE.md`'s adversarial review). */
  readonly engineStatus: 'STOPPED' | 'STARTING' | 'READY' | 'CRASHED';
  readonly schemaVersion: number | null;
  readonly uptimeSec: number;
}

export class EngineDownError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'EngineDownError';
  }
}

export class EngineRequestError extends Error {
  constructor(
    message: string,
    public readonly status: number,
  ) {
    super(message);
    this.name = 'EngineRequestError';
  }
}

async function parseErrorBody(res: Response): Promise<string> {
  try {
    const body = (await res.json()) as { error?: { message?: string } };
    return body.error?.message ?? res.statusText;
  } catch {
    return res.statusText;
  }
}

export async function fetchHealth(baseUrl: string): Promise<HealthStatus> {
  const res = await fetch(`${baseUrl}/health`);
  if (!res.ok) {
    throw new EngineRequestError(`health check failed: ${res.status}`, res.status);
  }
  return (await res.json()) as HealthStatus;
}

export async function fetchSnapshot(baseUrl: string): Promise<SimulationSnapshot> {
  const res = await fetch(`${baseUrl}/snapshot`);
  if (res.status === 503) {
    throw new EngineDownError(await parseErrorBody(res));
  }
  if (!res.ok) {
    throw new EngineRequestError(await parseErrorBody(res), res.status);
  }
  return (await res.json()) as SimulationSnapshot;
}

/** The exact, fixed whitelist of controls this project's engine supports (brief §46, §55 --
 * never an arbitrary command string). */
export type SimCommand = 'start' | 'pause' | 'resume' | 'reset' | 'step';

async function postCommand(
  baseUrl: string,
  path: string,
  body?: Record<string, number | string>,
): Promise<SimulationSnapshot> {
  const res = await fetch(`${baseUrl}${path}`, {
    method: 'POST',
    headers: body ? { 'Content-Type': 'application/json' } : undefined,
    body: body ? JSON.stringify(body) : undefined,
  });
  if (res.status === 503) {
    throw new EngineDownError(await parseErrorBody(res));
  }
  if (!res.ok) {
    throw new EngineRequestError(await parseErrorBody(res), res.status);
  }
  const parsed = (await res.json()) as { ok: boolean; snapshot: SimulationSnapshot };
  return parsed.snapshot;
}

export function sendSimCommand(baseUrl: string, command: SimCommand): Promise<SimulationSnapshot> {
  return postCommand(baseUrl, `/sim/${command}`);
}

export function sendAdvance(baseUrl: string, seconds: number): Promise<SimulationSnapshot> {
  return postCommand(baseUrl, '/sim/advance', { seconds });
}

export function sendSetSpeed(baseUrl: string, multiplier: number): Promise<SimulationSnapshot> {
  return postCommand(baseUrl, '/sim/speed', { multiplier });
}

/** Phase 11 brief #50: re-select the recommended point on a mission's ALREADY-CAPTURED
 * Pareto front under a different profile -- no new search, `cpp/app/main.cpp`'s
 * `applyRescoreCommand()` applies `MissionScorer::selectRecommendedPlan()` directly. */
export function sendRescoreParetoFront(
  baseUrl: string,
  missionId: string,
  profile: string,
): Promise<SimulationSnapshot> {
  return postCommand(baseUrl, '/sim/rescoreParetoFront', { missionId, profile });
}

/** The exact algorithm names `cpp/app/main.cpp`'s `applyRunAlgorithmCommand()` recognizes
 * (Phase 12 brief §16). */
export type RunnableAlgorithm = 'DIJKSTRA' | 'ASTAR' | 'PARETO' | 'HUNGARIAN';

/** Runs a real algorithm NOW, on demand -- distinct from the passive telemetry Phase 11
 * captures at each real dispatch. `vehicleId`/`emergencyId` are optional (DIJKSTRA/ASTAR/
 * PARETO only); the engine picks a real fallback when omitted. */
export function sendRunAlgorithm(
  baseUrl: string,
  algorithm: RunnableAlgorithm,
  vehicleId?: string,
  emergencyId?: string,
): Promise<SimulationSnapshot> {
  return postCommand(baseUrl, '/sim/runAlgorithm', {
    algorithm,
    ...(vehicleId ? { vehicleId } : {}),
    ...(emergencyId ? { emergencyId } : {}),
  });
}

/** Phase 12 brief §17: the real, already-generated Phase 11 benchmark CSVs
 * (`GET /benchmarks/:name`), read from disk verbatim by the backend -- never regenerated or
 * fabricated by this call. */
export async function fetchBenchmarkCsv(baseUrl: string, name: 'search' | 'assignment'): Promise<string> {
  const res = await fetch(`${baseUrl}/benchmarks/${name}`);
  if (!res.ok) {
    throw new EngineRequestError(await parseErrorBody(res), res.status);
  }
  return res.text();
}

/** The exact family names `cpp/simulation/ScenarioGenerator.hpp`'s `scenario_family`
 * namespace defines -- mirrors the backend's own whitelist (`api/server.ts`). */
export type ScenarioFamily =
  | 'sparse'
  | 'normal'
  | 'dense'
  | 'multiEmergency'
  | 'fuelConstrained'
  | 'highDebris'
  | 'tradeoffRich'
  | 'stress'
  /** Visualization-only family (`ScenarioGenerator.hpp`): full inclination spread + LEO/MEO/
   * GEO mix, so the rendered constellation reads as a shell rather than a flat band. Never
   * used for a benchmark or experiment. */
  | 'visualShell';

/** Phase 12 brief §5/§19: a large VISUAL constellation, fetched from a real one-shot engine
 * process (`GET /visual/constellation`) -- deliberately not part of `SimulationSnapshot`'s
 * polled live-session shape, since it is a different layer (see the type's own doc comment). */
export async function fetchVisualConstellation(
  baseUrl: string,
  family: ScenarioFamily,
  size: number,
  seed: number,
  timeSec: number,
): Promise<VisualConstellationSnapshot> {
  const params = new URLSearchParams({
    family,
    size: String(size),
    seed: String(seed),
    timeSec: String(timeSec),
  });
  const res = await fetch(`${baseUrl}/visual/constellation?${params.toString()}`);
  if (!res.ok) {
    throw new EngineRequestError(await parseErrorBody(res), res.status);
  }
  return (await res.json()) as VisualConstellationSnapshot;
}
