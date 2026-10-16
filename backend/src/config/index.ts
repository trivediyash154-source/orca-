import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '..', '..', '..');

export interface Config {
  readonly port: number;
  readonly enginePath: string;
  /** How long to wait for the engine's `ready` message after spawning, before giving up. */
  readonly engineStartupTimeoutMs: number;
  /** How long to wait for a command's matching response before treating it as lost. */
  readonly commandTimeoutMs: number;
  /** Phase 12 brief §17: where the real, already-generated Phase 11 benchmark CSVs live --
   * `GET /benchmarks/:name` (`api/server.ts`) reads real files from here, never fabricates
   * research data. */
  readonly benchmarksDir: string;
  /**
   * Which scenario the engine boots. `null` keeps the small scripted demo; a spec such as
   * `dense:200:1` boots a generated population WITH deterministic failures, which is what
   * gives Mission Control a real rescue narrative instead of three static satellites.
   */
  readonly engineScenario: string | null;
  /**
   * Satellite-catalog provider settings. CelesTrak's GP and SATCAT feeds need no credential,
   * which is why they are the default: a keyless source cannot leak a key. `spaceTrackUser`/
   * `spaceTrackPassword` exist as the boundary for a credentialled provider -- they are read
   * from the environment, never written to a response, and never sent to the browser.
   */
  readonly catalog: {
    readonly gpUrl: string;
    readonly satcatUrl: string;
    readonly group: string;
    readonly staleAfterHours: number;
    readonly fetchTimeoutMs: number;
    /** Where downloaded feeds are persisted; see `CatalogStore`'s note on the provider's 403. */
    readonly cacheDir: string;
    readonly minRefetchMinutes: number;
    readonly userAgent: string;
    /** Sync once at boot so the first browser to connect finds a populated catalog. */
    readonly syncOnStart: boolean;
    readonly credentialledProvider: string | null;
  };
}

export function loadConfig(env: NodeJS.ProcessEnv = process.env): Config {
  return {
    port: Number(env.PORT ?? 4000),
    enginePath: env.ORCA_ENGINE_PATH ?? path.join(repoRoot, 'build-direct', 'orca_engine'),
    engineStartupTimeoutMs: Number(env.ORCA_ENGINE_STARTUP_TIMEOUT_MS ?? 5000),
    // 30 s rather than 10: a generated scenario's first `advance` runs the planner over the
    // whole emergency set, and 10 s timed that out at 200 objects even though the engine
    // completed the work correctly a few seconds later.
    commandTimeoutMs: Number(env.ORCA_COMMAND_TIMEOUT_MS ?? 30000),
    benchmarksDir: env.ORCA_BENCHMARKS_DIR ?? path.join(repoRoot, 'benchmarks', 'results'),
    // Defaults to a generated scenario rather than the demo: a real population with a seeded
    // failure schedule exercises emergency generation, fleet assignment, mission execution and
    // replanning, none of which the 3-satellite demo can show.
    //
    // The SIZE is a measured compromise, not a guess. Timed on this machine over a 6-hour
    // simulated advance: 100 objects completes comfortably inside the command timeout and
    // yields 5 emergencies and a completed rescue; 200 objects costs ~34 s, because candidate
    // generation runs one Pareto search per (vehicle, emergency) pair and that cost grows
    // sharply with graph size (docs/FINAL_BENCHMARK_REPORT.md §4 documents the same effect).
    // Larger scenarios remain available by setting this variable explicitly.
    // ORCA_ENGINE_SCENARIO=demo returns to the small scripted narrative.
    engineScenario:
      env.ORCA_ENGINE_SCENARIO === 'demo' ? null : (env.ORCA_ENGINE_SCENARIO ?? 'dense:100:1'),
    catalog: {
      gpUrl: env.ORCA_CATALOG_GP_URL ?? 'https://celestrak.org/NORAD/elements/gp.php',
      satcatUrl: env.ORCA_CATALOG_SATCAT_URL ?? 'https://celestrak.org/pub/satcat.csv',
      group: env.ORCA_CATALOG_GROUP ?? 'active',
      staleAfterHours: Number(env.ORCA_CATALOG_STALE_HOURS ?? 24),
      fetchTimeoutMs: Number(env.ORCA_CATALOG_TIMEOUT_MS ?? 90000),
      cacheDir: env.ORCA_CATALOG_CACHE_DIR ?? path.join(repoRoot, '.catalog-cache'),
      // Matches CelesTrak's stated two-hour GP update cycle: requesting more often cannot
      // return anything new, and the provider answers 403 to say so.
      minRefetchMinutes: Number(env.ORCA_CATALOG_MIN_REFETCH_MINUTES ?? 120),
      userAgent: env.ORCA_CATALOG_USER_AGENT ?? 'ORCA-Bridge/0.9 (orbital rescue mission planning research)',
      // Opt-out rather than opt-in: a bridge that starts with an empty catalog shows an
      // unsynchronised sky to the first user, and the fetch is one request to a public feed.
      syncOnStart: env.ORCA_CATALOG_SYNC_ON_START !== 'false',
      // Presence of a username is what marks a credentialled provider as configured. The
      // password is read where the request is made and is never placed on this object, so it
      // cannot reach a log line that dumps the config.
      credentialledProvider: env.ORCA_SPACETRACK_USER ? 'SPACE-TRACK' : null,
    },
  };
}
