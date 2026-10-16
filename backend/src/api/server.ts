import { execFile } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';

import express, { type Express, type Request, type Response } from 'express';

import { CatalogStore } from '../catalog/CatalogStore.js';
import { createCatalogRouter } from '../catalog/routes.js';
import type { EngineProcess } from '../engine/EngineProcess.js';
import type { EngineCommandName } from '../ipc/protocol.js';

const startedAt = Date.now();
const execFileAsync = promisify(execFile);
const here = path.dirname(fileURLToPath(import.meta.url));
const DEFAULT_BENCHMARKS_DIR = path.resolve(here, '..', '..', '..', 'benchmarks', 'results');

/** Phase 12 brief §17: the real Phase 11 CSV filenames `orca_engine --bench` actually
 * writes (`cpp/app/main.cpp`'s `runBenchmarkSuite()`) -- a fixed whitelist, not a general
 * file-read endpoint, so this route can never be pointed at an arbitrary path. */
const BENCHMARK_FILES: Record<string, string> = {
  search: 'phase11_search_comparison.csv',
  assignment: 'phase11_assignment_comparison.csv',
};

/** The exact family names `cpp/simulation/ScenarioGenerator.hpp`'s `scenario_family`
 * namespace defines -- a fixed whitelist (brief §55's established "never an arbitrary
 * string" rule), not because `execFile` (argument array, no shell) is injectable, but
 * because an unknown family is a real user error the engine would reject anyway, and a
 * 400 here is a clearer answer than a 502 from a spawned process. */
const VISUAL_SCENARIO_FAMILIES = new Set([
  'sparse',
  'normal',
  'dense',
  'multiEmergency',
  'fuelConstrained',
  'highDebris',
  'tradeoffRich',
  'stress',
  'visualShell',
]);

/**
 * Builds the REST API — brief §21's "commands flow frontend -> Node -> engine, state flows
 * engine -> Node -> frontend" split, with every mutating command over a POST/response pair
 * (brief §55: only a fixed whitelist of commands, never an arbitrary-string execute).
 *
 * Factored out from process bootstrap (`index.ts`) so tests can build an app around a real
 * or fake `EngineProcess` without binding a port.
 */
export function createApp(
  engine: EngineProcess,
  benchmarksDir: string = DEFAULT_BENCHMARKS_DIR,
  /**
   * The satellite catalog. Optional, and constructed fresh when omitted, so the existing test
   * suite's `createApp(engine)` calls keep working unchanged. A freshly-constructed store holds
   * nothing and performs no network I/O until something calls `POST /catalog/sync` -- so
   * mounting these routes cannot make a test depend on a third party's uptime, and
   * `GET /catalog/status` truthfully reports `NEVER_SYNCHRONISED` rather than an empty sky.
   */
  catalog: CatalogStore = new CatalogStore(),
): Express {
  const app = express();
  app.use(express.json());

  // The frontend dev server and this bridge run on different ports/origins by default
  // (brief §36's split) -- without this, every browser request is silently blocked by CORS
  // before it reaches any route below. Found empirically (Phase 11's real-browser
  // instanced-rendering measurement, `docs/FINAL_BENCHMARK_REPORT.md`): every earlier
  // phase's REST test (`server.test.ts`) uses Node's own `fetch`, which does not enforce
  // CORS, so this gap was invisible to the test suite until an actual browser hit it.
  // Permissive by design -- a local simulation/demo tool with no auth or sensitive data, not
  // a public API (`ARCHITECTURE.md`'s scope).
  app.use((req: Request, res: Response, next) => {
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.header('Access-Control-Allow-Headers', 'Content-Type');
    if (req.method === 'OPTIONS') {
      res.sendStatus(204);
      return;
    }
    next();
  });

  // The catalog is a SECOND source of truth alongside the C++ engine, and deliberately
  // independent of it: element sets come from a public provider, not from the simulation, so
  // these routes answer normally while the engine is down. Namespaced under /catalog so the
  // separation is visible in every URL.
  app.use('/catalog', createCatalogRouter(catalog));

  app.get('/health', (_req: Request, res: Response) => {
    const status = engine.getStatus();
    res.json({
      status: status === 'READY' ? 'ok' : 'degraded',
      engineUp: status === 'READY',
      // The raw process state, distinct from the boolean above -- self-review finding
      // (INTEGRATION_ARCHITECTURE.md #12): a boolean alone cannot tell a caller "still
      // coming up, wait" (STARTING) apart from "will not come back on its own" (CRASHED).
      engineStatus: status,
      schemaVersion: engine.getReady()?.schemaVersion ?? null,
      uptimeSec: (Date.now() - startedAt) / 1000,
    });
  });

  app.get('/snapshot', (_req: Request, res: Response) => {
    const snapshot = engine.getLatestSnapshot();
    if (!snapshot) {
      res.status(503).json(engineDownBody(engine, 'no snapshot has been produced yet'));
      return;
    }
    res.json(snapshot);
  });

  const zeroArgCommand = (name: EngineCommandName) => async (_req: Request, res: Response) => {
    await runCommand(engine, res, name);
  };

  app.post('/sim/start', zeroArgCommand('start'));
  app.post('/sim/pause', zeroArgCommand('pause'));
  app.post('/sim/resume', zeroArgCommand('resume'));
  app.post('/sim/reset', zeroArgCommand('reset'));
  app.post('/sim/step', zeroArgCommand('step'));

  app.post('/sim/advance', async (req: Request, res: Response) => {
    const seconds = Number((req.body as { seconds?: unknown } | undefined)?.seconds);
    if (!Number.isFinite(seconds) || seconds < 0) {
      res.status(400).json({ error: { code: 'INVALID_REQUEST', message: 'seconds must be a non-negative number' } });
      return;
    }
    await runCommand(engine, res, 'advance', { seconds });
  });

  app.post('/sim/speed', async (req: Request, res: Response) => {
    const multiplier = Number((req.body as { multiplier?: unknown } | undefined)?.multiplier);
    if (!Number.isFinite(multiplier) || multiplier <= 0) {
      res.status(400).json({ error: { code: 'INVALID_REQUEST', message: 'multiplier must be a positive number' } });
      return;
    }
    await runCommand(engine, res, 'setSpeed', { multiplier });
  });

  app.post('/sim/rescoreParetoFront', async (req: Request, res: Response) => {
    const body = req.body as { missionId?: unknown; profile?: unknown } | undefined;
    const missionId = body?.missionId;
    const profile = body?.profile;
    if (typeof missionId !== 'string' || missionId.length === 0) {
      res.status(400).json({ error: { code: 'INVALID_REQUEST', message: 'missionId must be a non-empty string' } });
      return;
    }
    if (typeof profile !== 'string' || profile.length === 0) {
      res.status(400).json({ error: { code: 'INVALID_REQUEST', message: 'profile must be a non-empty string' } });
      return;
    }
    await runCommand(engine, res, 'rescoreParetoFront', { missionId, profile });
  });

  const RUNNABLE_ALGORITHMS = new Set(['DIJKSTRA', 'ASTAR', 'PARETO', 'HUNGARIAN']);

  // Phase 12 brief §16: Algorithm Lab must actually RUN an algorithm on click, not only
  // display telemetry captured passively at the last real dispatch.
  app.post('/sim/runAlgorithm', async (req: Request, res: Response) => {
    const body = req.body as { algorithm?: unknown; vehicleId?: unknown; emergencyId?: unknown } | undefined;
    const algorithm = body?.algorithm;
    if (typeof algorithm !== 'string' || !RUNNABLE_ALGORITHMS.has(algorithm)) {
      res.status(400).json({
        error: { code: 'INVALID_REQUEST', message: `algorithm must be one of: ${[...RUNNABLE_ALGORITHMS].join(', ')}` },
      });
      return;
    }
    const vehicleId = typeof body?.vehicleId === 'string' ? body.vehicleId : undefined;
    const emergencyId = typeof body?.emergencyId === 'string' ? body.emergencyId : undefined;
    await runCommand(engine, res, 'runAlgorithm', { algorithm, vehicleId, emergencyId });
  });

  // Phase 12 brief §5/§19: a large VISUAL constellation is a genuinely different layer
  // from the live mission-planning session `engine` owns above -- deliberately a SEPARATE,
  // one-shot `orca_engine --visualScenario` process (`cpp/io/VisualConstellation.hpp`'s own
  // file comment has the full reasoning: the planning graph is O(n^2) and was never meant
  // to run at this scale). This route never touches `engine`'s own session state.
  app.get('/visual/constellation', async (req: Request, res: Response) => {
    const family = String(req.query.family ?? '');
    const size = Number(req.query.size);
    const seed = Number(req.query.seed ?? 1);
    const timeSec = Number(req.query.timeSec ?? 0);

    if (!VISUAL_SCENARIO_FAMILIES.has(family)) {
      res.status(400).json({
        error: {
          code: 'INVALID_REQUEST',
          message: `family must be one of: ${[...VISUAL_SCENARIO_FAMILIES].join(', ')}`,
        },
      });
      return;
    }
    if (!Number.isInteger(size) || size < 1 || size > 20000) {
      res.status(400).json({ error: { code: 'INVALID_REQUEST', message: 'size must be an integer in [1, 20000]' } });
      return;
    }
    if (!Number.isFinite(seed) || !Number.isFinite(timeSec) || timeSec < 0) {
      res
        .status(400)
        .json({ error: { code: 'INVALID_REQUEST', message: 'seed and timeSec must be finite numbers' } });
      return;
    }

    try {
      const spec = `${family}:${size}:${Math.trunc(seed)}`;
      const { stdout } = await execFileAsync(
        engine.getEnginePath(),
        ['--visualScenario', spec, '--time', String(timeSec)],
        { timeout: 15000, maxBuffer: 64 * 1024 * 1024 },
      );
      res.type('application/json').send(stdout.trim());
    } catch (err) {
      res.status(502).json({
        error: {
          code: 'VISUAL_SCENARIO_FAILED',
          message: err instanceof Error ? err.message : String(err),
        },
      });
    }
  });

  // Phase 12 brief §17: "Analytics must consume the real benchmark outputs already
  // generated" -- reads the real Phase 11 CSV files from disk, verbatim. Never regenerates
  // or fabricates a row; if the file is missing (the suite was never run on this machine),
  // says so with a 404 rather than serving something invented.
  app.get('/benchmarks/:name', async (req: Request, res: Response) => {
    const name = req.params.name ?? '';
    const filename = BENCHMARK_FILES[name];
    if (!filename) {
      res.status(400).json({
        error: { code: 'INVALID_REQUEST', message: `unknown benchmark name '${name}'` },
      });
      return;
    }
    try {
      const csv = await readFile(path.join(benchmarksDir, filename), 'utf8');
      res.type('text/csv').send(csv);
    } catch (err) {
      res.status(404).json({
        error: {
          code: 'BENCHMARK_NOT_FOUND',
          message: `${filename} has not been generated on this machine yet (run 'orca_engine --bench')`,
          detail: err instanceof Error ? err.message : String(err),
        },
      });
    }
  });

  return app;
}

function engineDownBody(engine: EngineProcess, message: string) {
  return {
    error: {
      code: 'ENGINE_DOWN',
      message,
      detail: { status: engine.getStatus(), stderrTail: engine.getStderrTail() },
    },
  };
}

async function runCommand(
  engine: EngineProcess,
  res: Response,
  cmd: EngineCommandName,
  params: {
    seconds?: number;
    multiplier?: number;
    missionId?: string;
    profile?: string;
    algorithm?: string;
    vehicleId?: string;
    emergencyId?: string;
  } = {},
): Promise<void> {
  if (engine.getStatus() !== 'READY') {
    res.status(503).json(engineDownBody(engine, `cannot run '${cmd}': engine is not running`));
    return;
  }
  try {
    const snapshot = await engine.sendCommand(cmd, params);
    res.json({ ok: true, snapshot });
  } catch (err) {
    res.status(502).json({
      error: {
        code: 'ENGINE_COMMAND_FAILED',
        message: err instanceof Error ? err.message : String(err),
      },
    });
  }
}
