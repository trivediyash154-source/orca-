/**
 * The bridge's satellite-catalog cache.
 *
 * ## Why the bridge holds the catalog, and not the browser
 *
 * The `active` GP feed is ~7 MB of text and SATCAT is another ~7 MB. Fetching both from every
 * browser tab would be rude to a free public provider and slow for the user. The bridge fetches
 * once, joins and normalises, and serves a compact projection — the same division of labour the
 * rest of ORCA already uses (`docs/INTEGRATION_ARCHITECTURE.md`): the bridge is transport and
 * orchestration, and it computes nothing about the mission.
 *
 * ## CelesTrak answers 403 when you already have the current data
 *
 * Found by actually calling it, not by reading about it. A repeat request inside the provider's
 * two-hour update window returns:
 *
 *     HTTP 403
 *     GP data has not updated since your last successful download of GROUP=active at
 *     2026-09-07 21:38:24 UTC. Data is updated once every 2 hours.
 *
 * That is a courtesy cache-control mechanism, not a failure and not a rate-limit ban — the
 * provider is telling a well-behaved client that its copy is current. Treating it as an error
 * would put a working system into a permanent FAILED state, so it is detected specifically and
 * resolved from the on-disk cache. The corollary is that a client which does not persist what
 * it downloads *cannot* recover: it is refused the data precisely because the provider believes
 * it already has it. Disk caching is therefore a correctness requirement here, not an
 * optimisation.
 *
 * The cache also means a restart costs the provider nothing, and a sync inside the refresh
 * window makes no network request at all.
 *
 * ## Why nothing is fetched in the constructor
 *
 * Constructing a store must not perform I/O. The backend's existing test suite builds an app
 * per test; if construction fetched, the suite would depend on a live internet connection and on
 * a third party's uptime. A freshly-constructed store reports `NEVER_SYNCHRONISED`, which is the
 * truthful state, and the UI renders it as such rather than showing an empty sky that looks like
 * "no satellites exist".
 *
 * ## Staleness is a first-class state
 *
 * A TLE degrades: SGP4 accuracy falls off over days from epoch. Holding a week-old catalog and
 * calling it synchronised would be a quiet lie, so age is tracked and crosses into `STALE` on
 * its own, without anyone having to ask.
 */

import { mkdir, readFile, stat, writeFile } from 'node:fs/promises';
import path from 'node:path';

import { parseSatcat, UNKNOWN_METADATA } from './satcat.js';
import { parseTleCatalog } from './tle.js';
import { classify } from './classify.js';
import { deriveOrbit } from './derive.js';
import type {
  CatalogObject,
  CatalogSourceHealth,
  CatalogStatus,
  SyncState,
} from './types.js';

export interface CatalogConfig {
  /** Base GP endpoint. Keyless for CelesTrak. */
  readonly gpUrl: string;
  /** SATCAT CSV endpoint. */
  readonly satcatUrl: string;
  /** Which GP group to request, e.g. `active`, `starlink`, `stations`. */
  readonly group: string;
  /** Hours after which a synchronised catalog is reported STALE. */
  readonly staleAfterHours: number;
  readonly fetchTimeoutMs: number;
  /**
   * Where downloaded feeds are persisted. See the 403 note above: without this the provider's
   * own cache-control response is unrecoverable.
   */
  readonly cacheDir: string;
  /**
   * Minutes a cached feed is served without re-requesting. Defaults to the provider's stated
   * two-hour update cycle: asking more often cannot return anything new.
   */
  readonly minRefetchMinutes: number;
  /**
   * Sent on every request. Public providers ask clients to identify themselves so they can
   * distinguish a well-behaved integration from a scraper.
   */
  readonly userAgent: string;
  /**
   * Set only when a credentialled provider (Space-Track) has been configured. Its presence
   * flips the reported provider name; its absence is why the default is the keyless feed.
   * Credentials never leave the bridge — no route echoes them and no response contains them.
   */
  readonly credentialledProvider: string | null;
}

export const DEFAULT_CATALOG_CONFIG: CatalogConfig = {
  gpUrl: 'https://celestrak.org/NORAD/elements/gp.php',
  satcatUrl: 'https://celestrak.org/pub/satcat.csv',
  group: 'active',
  staleAfterHours: 24,
  fetchTimeoutMs: 90_000,
  cacheDir: path.resolve(process.cwd(), '.catalog-cache'),
  minRefetchMinutes: 120,
  userAgent: 'ORCA-Bridge/0.9 (orbital rescue mission planning research)',
  credentialledProvider: null,
};

type Fetcher = (
  url: string,
  init?: { signal?: AbortSignal; headers?: Record<string, string> },
) => Promise<{ ok: boolean; status: number; text: () => Promise<string> }>;

/** How a source's text was obtained on this sync — reported so liveness is never guessed. */
type Origin = 'NETWORK' | 'CACHE_FRESH' | 'CACHE_PROVIDER_UNCHANGED' | 'CACHE_AFTER_ERROR' | 'NONE';

interface SourceLoad {
  readonly text: string | null;
  readonly origin: Origin;
  readonly health: CatalogSourceHealth;
}

/**
 * CelesTrak's "already current" response. Matched on the body rather than the bare 403 so a
 * genuine authorisation failure is still reported as one.
 */
function isProviderUnchanged(status: number, body: string): boolean {
  // `\s+` rather than literal spaces: the provider hard-wraps the sentence, so the body
  // actually contains "your last successful\ndownload of GROUP=...". Matching on a literal
  // space silently failed here and reported a working system as FAILED.
  return status === 403 && /has\s+not\s+updated\s+since\s+your\s+last\s+successful\s+download/i.test(body);
}

export class CatalogStore {
  private objects: CatalogObject[] = [];
  private byNorad = new Map<number, CatalogObject>();
  private state: SyncState = 'NEVER_SYNCHRONISED';
  private lastSyncStartedAt: string | null = null;
  private lastSyncCompletedAt: string | null = null;
  private lastError: string | null = null;
  private sourceHealth: CatalogSourceHealth[] = [];
  private inFlight: Promise<CatalogStatus> | null = null;

  constructor(
    private readonly config: CatalogConfig = DEFAULT_CATALOG_CONFIG,
    /** Injected so tests exercise parsing and the join without touching the network. */
    private readonly fetcher: Fetcher = globalThis.fetch as unknown as Fetcher,
  ) {}

  all(): readonly CatalogObject[] {
    return this.objects;
  }

  get(noradId: number): CatalogObject | undefined {
    return this.byNorad.get(noradId);
  }

  get count(): number {
    return this.objects.length;
  }

  status(): CatalogStatus {
    const ageHours = this.ageHours();
    // Staleness is evaluated on read rather than on a timer: a store nobody is asking about
    // does not need to wake up to change its own mind.
    let state = this.state;
    if (state === 'SYNCHRONISED' && ageHours !== null && ageHours > this.config.staleAfterHours) {
      state = 'STALE';
    }
    return {
      state,
      objectCount: this.objects.length,
      lastSyncStartedAt: this.lastSyncStartedAt,
      lastSyncCompletedAt: this.lastSyncCompletedAt,
      lastError: this.lastError,
      ageHours,
      staleAfterHours: this.config.staleAfterHours,
      sources: this.sourceHealth,
      group: this.config.group,
    };
  }

  /**
   * Age of the ELEMENT SETS, not of the last sync attempt. A sync that resolved entirely from
   * cache must not reset this to zero: the data is exactly as old as it was, and reporting it
   * as fresh would defeat the staleness signal the UI depends on.
   */
  private ageHours(): number | null {
    if (this.lastSyncCompletedAt === null) return null;
    const gp = this.sourceHealth[0]?.fetchedAt ?? this.lastSyncCompletedAt;
    const at = Date.parse(gp);
    if (!Number.isFinite(at)) return null;
    return (Date.now() - at) / 3_600_000;
  }

  /**
   * Fetches both sources (or serves them from cache), joins them and replaces the catalog.
   *
   * Concurrent callers share one in-flight sync rather than starting a second: two browser tabs
   * loading at once should cost the provider one pair of requests, not two.
   *
   * A failure leaves the previous catalog intact and reports `FAILED` alongside it. Dropping
   * good-but-old data because a refresh failed would turn a recoverable network blip into an
   * empty sky.
   */
  sync(): Promise<CatalogStatus> {
    if (this.inFlight !== null) return this.inFlight;
    this.inFlight = this.runSync().finally(() => {
      this.inFlight = null;
    });
    return this.inFlight;
  }

  private async runSync(): Promise<CatalogStatus> {
    this.state = 'SYNCHRONISING';
    this.lastSyncStartedAt = new Date().toISOString();
    this.lastError = null;

    const gpUrl = `${this.config.gpUrl}?GROUP=${encodeURIComponent(this.config.group)}&FORMAT=tle`;
    const [gp, satcat] = await Promise.all([
      this.loadSource(gpUrl, `gp-${this.config.group}.tle`, 'CelesTrak GP (element sets)'),
      this.loadSource(this.config.satcatUrl, 'satcat.csv', 'CelesTrak SATCAT (object metadata)'),
    ]);

    if (gp.text === null) {
      // Without element sets there is no catalog at all; SATCAT alone describes nothing that
      // can be propagated.
      this.state = 'FAILED';
      this.lastError = gp.health.lastError ?? 'element-set feed unavailable';
      this.sourceHealth = [gp.health, satcat.health];
      return this.status();
    }

    const parsed = parseTleCatalog(gp.text);
    // SATCAT is optional by design: elements still propagate without it, and every descriptive
    // field simply reports as unavailable.
    const metadata = satcat.text === null ? new Map() : parseSatcat(satcat.text);
    const joinedAt = new Date().toISOString();

    const objects: CatalogObject[] = [];
    for (const elements of parsed.elements) {
      const meta = metadata.get(elements.noradId) ?? UNKNOWN_METADATA;
      const classification = classify(meta);
      objects.push({
        elements,
        derived: deriveOrbit(elements),
        metadata: meta,
        catalogClass: classification.catalogClass,
        classReason: classification.reason,
        source: 'CELESTRAK',
        // When the BYTES were obtained, which is what "how old is this data" means. A sync
        // served from cache reports the original download time, not now.
        fetchedAt: gp.health.fetchedAt ?? joinedAt,
      });
    }

    this.objects = objects;
    this.byNorad = new Map(objects.map((o) => [o.elements.noradId, o]));
    this.state = 'SYNCHRONISED';
    this.lastSyncCompletedAt = joinedAt;
    this.sourceHealth = [
      { ...gp.health, recordCount: parsed.elements.length },
      { ...satcat.health, recordCount: metadata.size },
    ];

    if (parsed.rejected.length > 0) {
      // Surfaced rather than swallowed: a feed that starts failing checksums is a real signal.
      console.warn(
        `[catalog] ${parsed.rejected.length} element set(s) rejected during parse; ` +
          `first: ${parsed.rejected[0]?.reason ?? 'unknown'}`,
      );
    }

    return this.status();
  }

  private cachePath(fileName: string): string {
    return path.join(this.config.cacheDir, fileName);
  }

  private async readCache(
    fileName: string,
  ): Promise<{ text: string; fetchedAt: string; ageMinutes: number } | null> {
    try {
      const p = this.cachePath(fileName);
      const [text, info] = await Promise.all([readFile(p, 'utf8'), stat(p)]);
      if (text.trim() === '') return null;
      return {
        text,
        fetchedAt: new Date(info.mtimeMs).toISOString(),
        ageMinutes: (Date.now() - info.mtimeMs) / 60_000,
      };
    } catch {
      return null;
    }
  }

  private async writeCache(fileName: string, text: string): Promise<void> {
    try {
      await mkdir(this.config.cacheDir, { recursive: true });
      await writeFile(this.cachePath(fileName), text, 'utf8');
    } catch (err) {
      // A cache that cannot be written is a degraded-performance problem, not a correctness
      // one for this sync — the data is already in hand. It IS a correctness problem for the
      // next one (see the 403 note), so it is logged rather than ignored.
      console.warn(
        `[catalog] could not write cache ${fileName}: ${err instanceof Error ? err.message : String(err)}`,
      );
    }
  }

  /**
   * Resolves one feed, preferring the cheapest correct source: a fresh cache, then the network,
   * then a stale cache when the network cannot improve on it.
   */
  private async loadSource(url: string, cacheFile: string, name: string): Promise<SourceLoad> {
    const cached = await this.readCache(cacheFile);

    if (cached !== null && cached.ageMinutes < this.config.minRefetchMinutes) {
      return {
        text: cached.text,
        origin: 'CACHE_FRESH',
        health: {
          name,
          url,
          ok: true,
          recordCount: 0,
          lastError: null,
          fetchedAt: cached.fetchedAt,
        },
      };
    }

    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), this.config.fetchTimeoutMs);
    try {
      const res = await this.fetcher(url, {
        signal: controller.signal,
        headers: { 'User-Agent': this.config.userAgent, Accept: 'text/plain, text/csv, */*' },
      });
      const body = await res.text();

      if (res.ok) {
        await this.writeCache(cacheFile, body);
        return {
          text: body,
          origin: 'NETWORK',
          health: {
            name,
            url,
            ok: true,
            recordCount: 0,
            lastError: null,
            fetchedAt: new Date().toISOString(),
          },
        };
      }

      if (isProviderUnchanged(res.status, body) && cached !== null) {
        // The provider is confirming our copy is current. Not an error, and the cached bytes
        // keep their original timestamp so staleness stays truthful.
        return {
          text: cached.text,
          origin: 'CACHE_PROVIDER_UNCHANGED',
          health: {
            name,
            url,
            ok: true,
            recordCount: 0,
            lastError: null,
            fetchedAt: cached.fetchedAt,
          },
        };
      }

      const error = isProviderUnchanged(res.status, body)
        ? `${name}: provider reports our copy is current, but no cached copy exists ` +
          `(cache directory ${this.config.cacheDir} is empty or unreadable). ` +
          `The feed updates every ${this.config.minRefetchMinutes} minutes; retry after that.`
        : `${name} responded HTTP ${res.status}`;
      return this.failOrFallBack(name, url, error, cached);
    } catch (err) {
      const error = err instanceof Error ? err.message : String(err);
      return this.failOrFallBack(name, url, `${name}: ${error}`, cached);
    } finally {
      clearTimeout(timer);
    }
  }

  /** A stale cache beats nothing, provided the response says which it is. */
  private failOrFallBack(
    name: string,
    url: string,
    error: string,
    cached: { text: string; fetchedAt: string } | null,
  ): SourceLoad {
    if (cached !== null) {
      return {
        text: cached.text,
        origin: 'CACHE_AFTER_ERROR',
        health: { name, url, ok: true, recordCount: 0, lastError: error, fetchedAt: cached.fetchedAt },
      };
    }
    return {
      text: null,
      origin: 'NONE',
      health: { name, url, ok: false, recordCount: 0, lastError: error, fetchedAt: null },
    };
  }
}
