/**
 * Catalog ingestion tests.
 *
 * Nothing here touches the network: `CatalogStore` takes an injected fetcher precisely so the
 * parse/join/classify pipeline can be exercised deterministically. The fixtures are REAL
 * records copied verbatim from the providers (a genuine ISS element set, genuine SATCAT rows),
 * because a hand-written TLE would not have caught the column-alignment and checksum bugs these
 * tests exist to prevent.
 *
 * The provider behaviour asserted in "already current" is also real and was found by calling
 * the live endpoint: CelesTrak answers 403 with an explanatory body when a client re-requests
 * data it already has, and the body hard-wraps mid-sentence. Matching that message on a literal
 * space reported a healthy system as FAILED — hence the explicit test.
 */

import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { afterAll, describe, expect, it } from 'vitest';

import { CatalogStore, DEFAULT_CATALOG_CONFIG } from '../src/catalog/CatalogStore.js';
import { classify, CLASSIFICATION_RULES, buildLegend } from '../src/catalog/classify.js';
import { classifyRegime, deriveOrbit, semiMajorAxisKm } from '../src/catalog/derive.js';
import { parseSatcat, splitCsvLine } from '../src/catalog/satcat.js';
import { parseTleCatalog, parseTleSet, parseExponential, tleChecksum } from '../src/catalog/tle.js';
import type { SatcatMetadata } from '../src/catalog/types.js';

/** A real ISS element set, exactly as CelesTrak served it. */
const ISS_NAME = 'ISS (ZARYA)             ';
const ISS_L1 = '1 25544U 98067A   26250.49846501  .00005306  00000+0  10435-3 0  9991';
const ISS_L2 = '2 25544  51.6306 252.7093 0004983 115.8922 244.2580 15.49018229584512';

const SATCAT_CSV = [
  'OBJECT_NAME,OBJECT_ID,NORAD_CAT_ID,OBJECT_TYPE,OPS_STATUS_CODE,OWNER,LAUNCH_DATE,LAUNCH_SITE,DECAY_DATE,PERIOD,INCLINATION,APOGEE,PERIGEE,RCS,DATA_STATUS_CODE,ORBIT_CENTER,ORBIT_TYPE',
  'ISS (ZARYA),1998-067A,25544,PAY,+,ISS,1998-11-20,TYMSC,,92.96,51.63,423,416,399.0524,,EA,ORB',
  '"SL-16 R/B, DEB",1988-039B,19121,DEB,,CIS,1988-05-15,TTMTR,,,,,,,,EA,ORB',
  'THOR ABLESTAR DEB,1960-007C,00047,R/B,-,US,1960-06-22,AFETR,,103.5,66.7,1050,600,,,EA,ORB',
  'DECAYED SAT,2001-001A,90001,PAY,D,US,2001-01-01,AFETR,2010-05-05,,,,,,,EA,ORB',
].join('\n');

function fixedFetcher(responses: Record<string, { ok: boolean; status: number; body: string }>) {
  return async (url: string) => {
    const key = Object.keys(responses).find((k) => url.includes(k));
    const r = key === undefined ? undefined : responses[key];
    if (r === undefined) return { ok: false, status: 404, text: async () => 'no fixture' };
    return { ok: r.ok, status: r.status, text: async () => r.body };
  };
}

describe('TLE parsing (fixed-column, against a real element set)', () => {
  it('parses every field of the real ISS element set', () => {
    const e = parseTleSet(ISS_NAME, ISS_L1, ISS_L2);
    expect(e.noradId).toBe(25544);
    expect(e.name).toBe('ISS (ZARYA)');
    expect(e.intlDesignator).toBe('1998-067A');
    expect(e.inclinationDeg).toBeCloseTo(51.6306, 4);
    expect(e.raanDeg).toBeCloseTo(252.7093, 4);
    // Assumed leading decimal point: "0004983" is 0.0004983, not 4983.
    expect(e.eccentricity).toBeCloseTo(0.0004983, 7);
    expect(e.argPerigeeDeg).toBeCloseTo(115.8922, 4);
    expect(e.meanAnomalyDeg).toBeCloseTo(244.258, 3);
    expect(e.meanMotionRevPerDay).toBeCloseTo(15.49018229, 8);
    // BSTAR "10435-3" is 0.10435e-3.
    expect(e.bstar).toBeCloseTo(0.00010435, 9);
    expect(e.revAtEpoch).toBe(58451);
    // Epoch 26250.49846501 -> day 250 of 2026.
    expect(e.epoch.startsWith('2026-09-07')).toBe(true);
  });

  it('retains the original lines verbatim for the propagator', () => {
    const e = parseTleSet(ISS_NAME, ISS_L1, ISS_L2);
    expect(e.line1).toBe(ISS_L1);
    expect(e.line2).toBe(ISS_L2);
  });

  it('computes the published checksum for both real lines', () => {
    expect(tleChecksum(ISS_L1)).toBe(Number(ISS_L1[68]));
    expect(tleChecksum(ISS_L2)).toBe(Number(ISS_L2[68]));
  });

  it('rejects a line whose checksum does not match rather than propagating garbage', () => {
    // Flip a digit in the inclination; the published checksum no longer describes the line.
    const corrupted = `${ISS_L2.slice(0, 9)}9${ISS_L2.slice(10)}`;
    expect(() => parseTleSet(ISS_NAME, ISS_L1, corrupted)).toThrow(/checksum/i);
  });

  it('rejects a set whose two lines disagree about the catalog number', () => {
    const otherL2 = `2 99999${ISS_L2.slice(7)}`;
    expect(() => parseTleSet(ISS_NAME, ISS_L1, otherL2, false)).toThrow(/catalog numbers disagree/i);
  });

  it('decodes the implied-decimal exponent notation in both signs', () => {
    expect(parseExponential(' 10435-3')).toBeCloseTo(0.00010435, 10);
    expect(parseExponential('-11606-4')).toBeCloseTo(-0.000011606, 12);
    expect(parseExponential(' 00000+0')).toBe(0);
  });

  it('collects malformed sets instead of discarding the whole feed', () => {
    const text = [ISS_NAME, ISS_L1, ISS_L2, 'BROKEN', '1 nonsense', '2 nonsense'].join('\n');
    const { elements, rejected } = parseTleCatalog(text);
    expect(elements).toHaveLength(1);
    expect(rejected).toHaveLength(1);
  });
});

describe('orbital derivation', () => {
  it('recovers a physically correct orbit for the real ISS element set', () => {
    const e = parseTleSet(ISS_NAME, ISS_L1, ISS_L2);
    const d = deriveOrbit(e);
    // The ISS orbits at roughly 415-425 km; SATCAT independently reports 416 x 423 km.
    expect(d.perigeeAltitudeKm).toBeGreaterThan(390);
    expect(d.perigeeAltitudeKm).toBeLessThan(430);
    expect(d.apogeeAltitudeKm).toBeGreaterThan(390);
    expect(d.apogeeAltitudeKm).toBeLessThan(440);
    // ~92.9 minutes, which SATCAT also reports as 92.96.
    expect(d.periodMinutes).toBeCloseTo(92.9, 0);
    expect(d.regime).toBe('LEO');
  });

  it('returns NaN semi-major axis for a non-positive mean motion rather than a bogus radius', () => {
    expect(Number.isNaN(semiMajorAxisKm(0))).toBe(true);
    expect(Number.isNaN(semiMajorAxisKm(-1))).toBe(true);
  });

  it('classifies a Molniya-type orbit by eccentricity, not by either apsis alone', () => {
    // Perigee sits at LEO altitude and apogee beyond GEO; only eccentricity characterises it.
    expect(classifyRegime(500, 39000, 0.74)).toBe('HEO');
  });

  it('separates LEO, MEO and GEO at the published boundaries', () => {
    expect(classifyRegime(400, 420, 0.001)).toBe('LEO');
    expect(classifyRegime(20000, 20200, 0.001)).toBe('MEO');
    expect(classifyRegime(35786, 35786, 0.0001)).toBe('GEO');
  });
});

describe('SATCAT parsing', () => {
  it('handles a quoted field containing a comma without shifting every later column', () => {
    expect(splitCsvLine('"SL-16 R/B, DEB",1988-039B,19121,DEB')).toEqual([
      'SL-16 R/B, DEB',
      '1988-039B',
      '19121',
      'DEB',
    ]);
  });

  it('maps real type and status codes, and distinguishes absent from unknown', () => {
    const map = parseSatcat(SATCAT_CSV);
    const iss = map.get(25544);
    expect(iss?.objectType).toBe('PAYLOAD');
    expect(iss?.opsStatus).toBe('OPERATIONAL');
    expect(iss?.owner).toBe('ISS');
    expect(iss?.rcsM2).toBeCloseTo(399.0524, 4);
    expect(iss?.decayDate).toBeNull();

    // An empty status cell is "the catalog does not state one" -> null, NOT 'UNKNOWN'.
    expect(map.get(19121)?.opsStatus).toBeNull();
    expect(map.get(19121)?.objectType).toBe('DEBRIS');
    expect(map.get(47)?.objectType).toBe('ROCKET BODY');
    expect(map.get(90001)?.decayDate).toBe('2010-05-05');
  });
});

describe('classification rules', () => {
  const base: SatcatMetadata = {
    objectType: null,
    opsStatus: null,
    owner: null,
    launchDate: null,
    launchSite: null,
    decayDate: null,
    rcsM2: null,
  };

  it('marks an operational payload nominal, with a stated reason', () => {
    const c = classify({ ...base, objectType: 'PAYLOAD', opsStatus: 'OPERATIONAL' });
    expect(c.catalogClass).toBe('NOMINAL');
    expect(c.reason.length).toBeGreaterThan(0);
  });

  it('marks debris and spent stages as requiring attention', () => {
    expect(classify({ ...base, objectType: 'DEBRIS' }).catalogClass).toBe('ATTENTION');
    expect(classify({ ...base, objectType: 'ROCKET BODY' }).catalogClass).toBe('ATTENTION');
  });

  it('puts re-entry ahead of payload status, so a decayed payload is not shown as operational', () => {
    const c = classify({
      ...base,
      objectType: 'PAYLOAD',
      opsStatus: 'OPERATIONAL',
      decayDate: '2010-05-05',
    });
    expect(c.catalogClass).toBe('ATTENTION');
    expect(c.ruleId).toBe('decayed');
  });

  it('reports unknown rather than guessing when there is no SATCAT row', () => {
    expect(classify(base).catalogClass).toBe('UNKNOWN');
  });

  it('every rule contributes a legend entry, so a colour cannot exist without an explanation', () => {
    const legend = buildLegend();
    for (const rule of CLASSIFICATION_RULES) {
      expect(legend.some((l) => l.label === rule.label)).toBe(true);
    }
    // Plus the mission-relevance entry, which is applied at query time rather than by classify().
    expect(legend.some((l) => l.key === 'MISSION:relevant')).toBe(true);
  });
});

describe('CatalogStore honesty', () => {
  // A fresh cache directory per store. The store persists every successful fetch by design
  // (see its note on the provider's 403), so a shared directory would let one test's download
  // satisfy the next test's "the feed is unavailable" scenario and quietly pass it.
  const tempDirs: string[] = [];
  async function freshCacheDir(): Promise<string> {
    const dir = await mkdtemp(path.join(tmpdir(), 'orca-catalog-test-'));
    tempDirs.push(dir);
    return dir;
  }
  afterAll(async () => {
    await Promise.all(tempDirs.map((d) => rm(d, { recursive: true, force: true })));
  });

  it('reports NEVER_SYNCHRONISED before anything is fetched, and holds nothing', async () => {
    const store = new CatalogStore({ ...DEFAULT_CATALOG_CONFIG, cacheDir: await freshCacheDir() });
    const s = store.status();
    expect(s.state).toBe('NEVER_SYNCHRONISED');
    expect(s.objectCount).toBe(0);
    expect(s.lastSyncCompletedAt).toBeNull();
    expect(store.all()).toHaveLength(0);
  });

  it('joins real elements with real metadata and states why each object is coloured', async () => {
    const store = new CatalogStore(
      { ...DEFAULT_CATALOG_CONFIG, cacheDir: await freshCacheDir(), group: 'test' },
      fixedFetcher({
        'gp.php': { ok: true, status: 200, body: [ISS_NAME, ISS_L1, ISS_L2].join('\n') },
        'satcat.csv': { ok: true, status: 200, body: SATCAT_CSV },
      }),
    );
    const status = await store.sync();
    expect(status.state).toBe('SYNCHRONISED');
    expect(status.objectCount).toBe(1);

    const iss = store.get(25544);
    expect(iss?.elements.name).toBe('ISS (ZARYA)');
    expect(iss?.metadata.owner).toBe('ISS');
    expect(iss?.catalogClass).toBe('NOMINAL');
    expect(iss?.classReason).toMatch(/operational payload/i);
    expect(iss?.source).toBe('CELESTRAK');
  });

  it('keeps real elements and reports metadata as unavailable when SATCAT has no row', async () => {
    const store = new CatalogStore(
      { ...DEFAULT_CATALOG_CONFIG, cacheDir: await freshCacheDir(), group: 'test' },
      fixedFetcher({
        'gp.php': { ok: true, status: 200, body: [ISS_NAME, ISS_L1, ISS_L2].join('\n') },
        'satcat.csv': { ok: false, status: 500, body: 'upstream error' },
      }),
    );
    await store.sync();
    const iss = store.get(25544);
    // The orbit is still real and propagatable; the descriptive fields are honestly empty.
    expect(iss?.elements.inclinationDeg).toBeCloseTo(51.6306, 4);
    expect(iss?.metadata.owner).toBeNull();
    expect(iss?.metadata.objectType).toBeNull();
    expect(iss?.catalogClass).toBe('UNKNOWN');
  });

  it('fails honestly, with no objects, when the element feed is unavailable and nothing is cached', async () => {
    const store = new CatalogStore(
      { ...DEFAULT_CATALOG_CONFIG, cacheDir: await freshCacheDir(), group: 'test' },
      fixedFetcher({
        'gp.php': { ok: false, status: 503, body: 'upstream down' },
        'satcat.csv': { ok: true, status: 200, body: SATCAT_CSV },
      }),
    );
    const status = await store.sync();
    expect(status.state).toBe('FAILED');
    expect(status.objectCount).toBe(0);
    expect(status.lastError).toMatch(/503/);
  });

  it("explains the provider's 'already current' 403 instead of reporting a bare failure", async () => {
    // The real body, including the hard wrap that broke a literal-space match.
    const body =
      'GP data has not updated since your last successful\ndownload of GROUP=active at ' +
      '2026-09-07 21:38:24 UTC.\nData is updated once every 2 hours.';
    const store = new CatalogStore(
      { ...DEFAULT_CATALOG_CONFIG, cacheDir: await freshCacheDir(), group: 'test' },
      fixedFetcher({
        'gp.php': { ok: false, status: 403, body },
        'satcat.csv': { ok: true, status: 200, body: SATCAT_CSV },
      }),
    );
    const status = await store.sync();
    expect(status.state).toBe('FAILED');
    // With no cache the situation is genuinely unrecoverable until the window passes, and the
    // message has to say so rather than leaving an operator staring at "403".
    expect(status.lastError).toMatch(/provider reports our copy is current/i);
    expect(status.lastError).toMatch(/retry after/i);
  });

  it('shares one in-flight sync between concurrent callers', async () => {
    let calls = 0;
    const store = new CatalogStore({ ...DEFAULT_CATALOG_CONFIG, cacheDir: await freshCacheDir(), group: 'test' }, async (url) => {
      calls++;
      await new Promise((r) => setTimeout(r, 10));
      return {
        ok: true,
        status: 200,
        text: async () => (url.includes('satcat') ? SATCAT_CSV : [ISS_NAME, ISS_L1, ISS_L2].join('\n')),
      };
    });
    await Promise.all([store.sync(), store.sync(), store.sync()]);
    // Two sources, fetched once — not six requests for three concurrent callers.
    expect(calls).toBe(2);
  });
});
