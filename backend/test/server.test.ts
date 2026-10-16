import { fileURLToPath } from 'node:url';
import path from 'node:path';
import type { AddressInfo } from 'node:net';
import type { Server } from 'node:http';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';

import { createApp } from '../src/api/server.js';
import { EngineProcess } from '../src/engine/EngineProcess.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '..', '..');
const enginePath = path.join(repoRoot, 'build-direct', 'orca_engine');

/**
 * The end-to-end smoke path brief §49 calls "the most important Phase 9 test": a real Node
 * process spawning the real compiled C++ engine, talked to over the real REST surface a
 * browser would use. Nothing here is mocked.
 */
describe('REST API against the real engine (integration)', () => {
  let engine: EngineProcess;
  let server: Server;
  let baseUrl: string;

  beforeAll(async () => {
    engine = new EngineProcess(enginePath, 5000, 10000);
    await engine.start();
    // Mirrors `index.ts`'s real bootstrap (brief §81/Phase 11 CORS fix's sibling bug fix):
    // populates `getLatestSnapshot()` before any client ever connects.
    await engine.sendCommand('getSnapshot');
    const app = createApp(engine);
    server = app.listen(0);
    await new Promise<void>((resolve) => server.once('listening', resolve));
    const port = (server.address() as AddressInfo).port;
    baseUrl = `http://127.0.0.1:${port}`;
  });

  afterAll(async () => {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await engine.shutdown(500);
  });

  it('1. engine launches and 2. reports healthy', async () => {
    const res = await fetch(`${baseUrl}/health`);
    expect(res.status).toBe(200);
    const body = (await res.json()) as { engineUp: boolean; schemaVersion: number };
    expect(body.engineUp).toBe(true);
    expect(body.schemaVersion).toBe(1);
  });

  it('responds with a permissive CORS header, so a real browser on a different origin can call it', async () => {
    // Regression for a real bug found empirically during Phase 11's instanced-rendering
    // measurement: Node's own `fetch` (every other test in this file) does not enforce
    // CORS, so a missing header was invisible here until an actual browser hit it.
    const res = await fetch(`${baseUrl}/health`);
    expect(res.headers.get('access-control-allow-origin')).toBe('*');
  });

  it('GET /snapshot succeeds immediately at boot, before any client has sent a command', async () => {
    // Regression for a real bootstrap deadlock found empirically (Phase 11): without the
    // `getSnapshot` bootstrap command in `index.ts`, `/snapshot` 503s until SOME command has
    // been sent -- but the frontend's Start button is disabled until `/snapshot` succeeds,
    // so nothing could ever send that first command from a real browser.
    const res = await fetch(`${baseUrl}/snapshot`);
    expect(res.status).toBe(200);
    const body = (await res.json()) as { type: string };
    expect(body.type).toBe('snapshot');
  });

  it('3. command accepted and 4. snapshot returned', async () => {
    const res = await fetch(`${baseUrl}/sim/start`, { method: 'POST' });
    expect(res.status).toBe(200);
    const body = (await res.json()) as { ok: boolean; snapshot: { type: string } };
    expect(body.ok).toBe(true);
    expect(body.snapshot.type).toBe('snapshot');
  });

  it('5. simulation advances, 6. snapshot time changes, 7. positions change', async () => {
    const beforeRes = await fetch(`${baseUrl}/snapshot`);
    const before = (await beforeRes.json()) as {
      simulationTimeSec: number;
      satellites: Array<{ position: { x: number; y: number; z: number } }>;
    };

    const advanceRes = await fetch(`${baseUrl}/sim/advance`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ seconds: 100 }),
    });
    expect(advanceRes.status).toBe(200);
    const after = (await advanceRes.json()) as {
      snapshot: { simulationTimeSec: number; satellites: Array<{ position: { x: number; y: number; z: number } }> };
    };

    expect(after.snapshot.simulationTimeSec).toBeGreaterThan(before.simulationTimeSec);
    const b = before.satellites[0]!.position;
    const a = after.snapshot.satellites[0]!.position;
    expect(a.x !== b.x || a.y !== b.y || a.z !== b.z).toBe(true);
  });

  it('rescoreParetoFront re-selects the recommended plan on the real engine without a new search', async () => {
    // Runs while the scripted demo mission dispatched by test 5-7's advance is still live --
    // must come BEFORE test 8's reset, since `reset` clears the dispatched mission and the
    // one-time scripted failures that produced it are only scheduled at engine startup
    // (`main.cpp`), never re-scheduled by `reset`.
    const snapshotRes = await fetch(`${baseUrl}/snapshot`);
    const current = (await snapshotRes.json()) as {
      missions: Array<{ id: string; paretoFront: Array<{ planId: string; recommended: boolean }> }>;
    };
    const missionId = current.missions[0]!.id;
    const before = current.missions[0]!.paretoFront;

    const rescoreRes = await fetch(`${baseUrl}/sim/rescoreParetoFront`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ missionId, profile: 'FASTEST' }),
    });
    expect(rescoreRes.status).toBe(200);
    const rescored = (await rescoreRes.json()) as {
      snapshot: {
        missions: Array<{
          id: string;
          activeRescoreProfile: string | null;
          paretoFront: Array<{ planId: string; cost: unknown; recommended: boolean }>;
        }>;
      };
    };
    const mission = rescored.snapshot.missions[0]!;
    expect(mission.activeRescoreProfile).toBe('FASTEST');
    // Same points/costs as before the rescore -- only which one is marked recommended
    // may change (brief §51: re-scoring never re-derives the front).
    expect(mission.paretoFront.map((p) => p.planId)).toEqual(before.map((p) => p.planId));
  });

  it('runAlgorithm actually runs Dijkstra/A*/Pareto on the real engine and appends real telemetry', async () => {
    for (const algorithm of ['DIJKSTRA', 'ASTAR', 'PARETO']) {
      const before = await (await fetch(`${baseUrl}/snapshot`)).json();
      const beforeCount = (before as { algorithmRuns: unknown[] }).algorithmRuns.length;

      const res = await fetch(`${baseUrl}/sim/runAlgorithm`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ algorithm }),
      });
      expect(res.status).toBe(200);
      const body = (await res.json()) as {
        snapshot: { algorithmRuns: Array<{ algorithm: string; executionTimeMs: number; found: boolean }> };
      };
      expect(body.snapshot.algorithmRuns.length).toBe(beforeCount + 1);
      const latest = body.snapshot.algorithmRuns[body.snapshot.algorithmRuns.length - 1]!;
      expect(latest.algorithm).toBe(algorithm);
      expect(latest.executionTimeMs).toBeGreaterThanOrEqual(0);
    }
  });

  it('runAlgorithm HUNGARIAN reports a real, honest error when nothing is available to assign', async () => {
    // The demo scenario's one vehicle is already dispatched by this point in the test file's
    // own real narrative -- HUNGARIAN genuinely has nothing to assign, and must say so
    // rather than fabricate an assignment.
    const res = await fetch(`${baseUrl}/sim/runAlgorithm`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ algorithm: 'HUNGARIAN' }),
    });
    expect(res.status).toBe(502);
    const body = (await res.json()) as { error: { code: string; message: string } };
    expect(body.error.message).toMatch(/available vehicle/);
  });

  it('rejects runAlgorithm with an unknown algorithm name with 400', async () => {
    const res = await fetch(`${baseUrl}/sim/runAlgorithm`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ algorithm: 'BOGUS' }),
    });
    expect(res.status).toBe(400);
    const body = (await res.json()) as { error: { code: string } };
    expect(body.error.code).toBe('INVALID_REQUEST');
  });

  it('GET /benchmarks/search returns the real Phase 11 CSV file from disk', async () => {
    const res = await fetch(`${baseUrl}/benchmarks/search`);
    expect(res.status).toBe(200);
    expect(res.headers.get('content-type')).toContain('text/csv');
    const csv = await res.text();
    const lines = csv.trim().split('\n');
    expect(lines[0]).toContain('scenarioFamily');
    expect(lines[0]).toContain('algorithm');
    expect(lines.length).toBeGreaterThan(100); // real file has 972 data rows + header
  });

  it('GET /benchmarks/assignment returns the real Phase 11 CSV file from disk', async () => {
    const res = await fetch(`${baseUrl}/benchmarks/assignment`);
    expect(res.status).toBe(200);
    const csv = await res.text();
    expect(csv.trim().split('\n').length).toBeGreaterThan(50); // real file has 144 data rows + header
  });

  it('rejects an unknown benchmark name with 400', async () => {
    const res = await fetch(`${baseUrl}/benchmarks/bogus`);
    expect(res.status).toBe(400);
  });

  it('rejects rescoreParetoFront with a missing missionId with 400, not a crash', async () => {
    const res = await fetch(`${baseUrl}/sim/rescoreParetoFront`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ profile: 'FASTEST' }),
    });
    expect(res.status).toBe(400);
    const body = (await res.json()) as { error: { code: string } };
    expect(body.error.code).toBe('INVALID_REQUEST');
  });

  it('8. reset returns the scene to its initial state', async () => {
    const res = await fetch(`${baseUrl}/sim/reset`, { method: 'POST' });
    const body = (await res.json()) as { snapshot: { simulationTimeSec: number; missions: unknown[] } };
    expect(body.snapshot.simulationTimeSec).toBe(0);
    expect(body.snapshot.missions).toHaveLength(0);
  });

  it('survives 10 repeated start/advance/reset cycles without crashing or accumulating stale state', async () => {
    // Hardening check (Phase 11 brief §8's "reset/restart repeated-cycle" requirement) --
    // real risk class: state left over from a previous cycle (a growing event log, a stuck
    // simulationStatus, a metrics counter that does not zero) surviving `reset()`.
    for (let cycle = 0; cycle < 10; cycle++) {
      const startRes = await fetch(`${baseUrl}/sim/start`, { method: 'POST' });
      expect(startRes.status).toBe(200);

      const advanceRes = await fetch(`${baseUrl}/sim/advance`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ seconds: 20 }),
      });
      expect(advanceRes.status).toBe(200);
      const advanced = (await advanceRes.json()) as { snapshot: { simulationTimeSec: number } };
      expect(advanced.snapshot.simulationTimeSec).toBeGreaterThan(0);

      const resetRes = await fetch(`${baseUrl}/sim/reset`, { method: 'POST' });
      expect(resetRes.status).toBe(200);
      const reset = (await resetRes.json()) as {
        snapshot: {
          simulationTimeSec: number;
          simulationStatus: string;
          missions: unknown[];
          recentEvents: unknown[];
          metrics: { ticks: number };
        };
      };
      expect(reset.snapshot.simulationTimeSec).toBe(0);
      expect(reset.snapshot.simulationStatus).toBe('PAUSED');
      expect(reset.snapshot.missions).toHaveLength(0);
      expect(reset.snapshot.metrics.ticks).toBe(0);
      // The event log itself must not grow across cycles -- a fresh `reset()` produces a
      // fresh, empty `recentEvents`, not one still carrying entries from cycle N-1.
      expect(reset.snapshot.recentEvents).toHaveLength(0);
    }
  });

  it('rejects an invalid advance request with 400, not a crash', async () => {
    const res = await fetch(`${baseUrl}/sim/advance`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ seconds: 'soon' }),
    });
    expect(res.status).toBe(400);
    const body = (await res.json()) as { error: { code: string } };
    expect(body.error.code).toBe('INVALID_REQUEST');
  });

  it('GET /visual/constellation returns a real large generated constellation from a one-shot engine process', async () => {
    const res = await fetch(`${baseUrl}/visual/constellation?family=dense&size=250&seed=1&timeSec=0`);
    expect(res.status).toBe(200);
    const body = (await res.json()) as {
      type: string;
      satelliteCount: number;
      satellites: Array<{ id: string; position: { x: number; y: number; z: number } }>;
    };
    expect(body.type).toBe('visualConstellation');
    expect(body.satelliteCount).toBe(250);
    expect(body.satellites).toHaveLength(250);
    expect(body.satellites[0]!.position.x).not.toBe(0);
  });

  it('rejects an unknown scenario family for /visual/constellation with 400', async () => {
    const res = await fetch(`${baseUrl}/visual/constellation?family=madeUp&size=100`);
    expect(res.status).toBe(400);
    const body = (await res.json()) as { error: { code: string } };
    expect(body.error.code).toBe('INVALID_REQUEST');
  });

  it('rejects an out-of-range size for /visual/constellation with 400', async () => {
    const res = await fetch(`${baseUrl}/visual/constellation?family=dense&size=999999`);
    expect(res.status).toBe(400);
    const body = (await res.json()) as { error: { code: string } };
    expect(body.error.code).toBe('INVALID_REQUEST');
  });
});

describe('REST API when the engine is down', () => {
  it('reports 503 with a structured ENGINE_DOWN error, not a hang', async () => {
    const deadEngine = new EngineProcess('/nonexistent/orca_engine_binary', 500, 500);
    const app = createApp(deadEngine);
    const server = app.listen(0);
    await new Promise<void>((resolve) => server.once('listening', resolve));
    const port = (server.address() as AddressInfo).port;

    try {
      const res = await fetch(`http://127.0.0.1:${port}/snapshot`);
      expect(res.status).toBe(503);
      const body = (await res.json()) as { error: { code: string } };
      expect(body.error.code).toBe('ENGINE_DOWN');

      const startRes = await fetch(`http://127.0.0.1:${port}/sim/start`, { method: 'POST' });
      expect(startRes.status).toBe(503);
    } finally {
      await new Promise<void>((resolve) => server.close(() => resolve()));
    }
  });
});
