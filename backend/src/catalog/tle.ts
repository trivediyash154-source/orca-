/**
 * NORAD two-line element set parsing.
 *
 * ## Why fixed columns rather than splitting on whitespace
 *
 * The TLE format is a fixed-column punched-card layout, not a delimited one. Fields are allowed
 * to run together — a three-digit RAAN leaves no space before the eccentricity — so
 * `line.split(/\s+/)` silently produces the wrong values for exactly the objects whose elements
 * are most extreme. Every field below is therefore sliced at the column positions given in the
 * format definition (celestrak.org/columns/v1d0/), with the 1-indexed spec columns named in the
 * comments and the 0-indexed JavaScript offsets in the code.
 *
 * ## What this parser does NOT do
 *
 * It does not propagate. The parsed numbers are used for display, derivation and filtering; the
 * position of an object at a time comes from SGP4, which is handed `line1`/`line2` verbatim
 * (`types.ts` explains why the original text is retained).
 */

import type { ElementSet } from './types.js';

/** A line that fails structural validation, with enough context to debug the feed. */
export class TleParseError extends Error {
  constructor(
    message: string,
    readonly line: string,
  ) {
    super(message);
    this.name = 'TleParseError';
  }
}

/**
 * TLE checksum: the sum modulo 10 of every digit in columns 1-68, counting each minus sign as 1
 * and everything else as 0. A mismatch means the line was corrupted in transit, and the right
 * response is to drop that object rather than propagate a garbled element set.
 */
export function tleChecksum(line: string): number {
  let sum = 0;
  for (let i = 0; i < 68 && i < line.length; i++) {
    const ch = line[i];
    if (ch === undefined) continue;
    if (ch >= '0' && ch <= '9') sum += ch.charCodeAt(0) - 48;
    else if (ch === '-') sum += 1;
  }
  return sum % 10;
}

/**
 * Decodes the TLE's implied-decimal exponent notation, e.g. `" 10435-3"` -> 0.10435e-3 and
 * `"-11606-4"` -> -0.11606e-4. The mantissa carries an assumed leading decimal point and the
 * final one or two characters are a signed base-10 exponent.
 */
export function parseExponential(field: string): number {
  const s = field.trim();
  if (s === '' || s === '0' || /^[+-]?0+$/.test(s)) return 0;
  const m = /^([+-]?)(\d+)([+-]\d)$/.exec(s);
  if (!m) {
    // Some feeds emit a plain zero-ish field; anything else genuinely is malformed.
    const plain = Number(s);
    return Number.isFinite(plain) ? plain : 0;
  }
  const sign = m[1] === '-' ? -1 : 1;
  const mantissa = Number(`0.${m[2] ?? '0'}`);
  const exponent = Number(m[3] ?? '0');
  return sign * mantissa * Math.pow(10, exponent);
}

/**
 * TLE epochs are `YYDDD.DDDDDDDD`: a two-digit year and a fractional day-of-year. The two-digit
 * year is resolved by the format's own rule — 57-99 mean 1957-1999 (Sputnik onward) and 00-56
 * mean 2000-2056. Returns an ISO 8601 UTC timestamp.
 */
export function parseEpoch(yearField: string, dayField: string): string {
  const yy = Number(yearField);
  const dayOfYear = Number(dayField);
  if (!Number.isFinite(yy) || !Number.isFinite(dayOfYear)) {
    throw new TleParseError('epoch is not numeric', `${yearField}/${dayField}`);
  }
  const year = yy >= 57 ? 1900 + yy : 2000 + yy;
  // Day-of-year is 1-based, so day 1.0 is January 1 at 00:00 UTC.
  const msSinceStartOfYear = (dayOfYear - 1) * 86400000;
  return new Date(Date.UTC(year, 0, 1) + msSinceStartOfYear).toISOString();
}

/**
 * Expands the TLE's compact international designator (`"98067A"`) into the conventional COSPAR
 * form (`"1998-067A"`). Objects launched before 1957 do not exist, so the same 57/00 pivot the
 * epoch uses applies here.
 */
function formatIntlDesignator(raw: string): string {
  const s = raw.trim();
  if (s === '') return '';
  const yy = Number(s.slice(0, 2));
  if (!Number.isFinite(yy)) return s;
  const year = yy >= 57 ? 1900 + yy : 2000 + yy;
  return `${year}-${s.slice(2)}`;
}

function num(field: string, what: string, line: string): number {
  const v = Number(field.trim());
  if (!Number.isFinite(v)) throw new TleParseError(`${what} is not numeric`, line);
  return v;
}

/**
 * Parses one three-line set (name line + the two element lines). `verifyChecksum` defaults on;
 * it is the cheapest available integrity check on a feed the bridge did not author.
 */
export function parseTleSet(
  nameLine: string,
  line1: string,
  line2: string,
  verifyChecksum = true,
): ElementSet {
  if (!line1.startsWith('1 ')) throw new TleParseError('line 1 does not begin with "1 "', line1);
  if (!line2.startsWith('2 ')) throw new TleParseError('line 2 does not begin with "2 "', line2);
  if (line1.length < 68) throw new TleParseError('line 1 is too short', line1);
  if (line2.length < 68) throw new TleParseError('line 2 is too short', line2);

  if (verifyChecksum) {
    const declared1 = Number(line1[68]);
    const declared2 = Number(line2[68]);
    if (Number.isFinite(declared1) && tleChecksum(line1) !== declared1) {
      throw new TleParseError('line 1 checksum mismatch', line1);
    }
    if (Number.isFinite(declared2) && tleChecksum(line2) !== declared2) {
      throw new TleParseError('line 2 checksum mismatch', line2);
    }
  }

  const noradId = num(line1.slice(2, 7), 'catalog number', line1); // cols 3-7
  const noradId2 = num(line2.slice(2, 7), 'catalog number', line2);
  if (noradId !== noradId2) {
    throw new TleParseError(`catalog numbers disagree (${noradId} vs ${noradId2})`, line2);
  }

  return {
    noradId,
    // The name line is the catalog's own designation. It is padded to a fixed width in the
    // feed, and some entries carry a leading "0 " line-number marker in 3LE form.
    name: nameLine.replace(/^0\s+/, '').trim(),
    intlDesignator: formatIntlDesignator(line1.slice(9, 17)), // cols 10-17
    epoch: parseEpoch(line1.slice(18, 20), line1.slice(20, 32)), // cols 19-32
    // Eccentricity has an assumed leading decimal point: "0004983" means 0.0004983.
    eccentricity: num(`0.${line2.slice(26, 33).trim()}`, 'eccentricity', line2), // cols 27-33
    inclinationDeg: num(line2.slice(8, 16), 'inclination', line2), // cols 9-16
    raanDeg: num(line2.slice(17, 25), 'RAAN', line2), // cols 18-25
    argPerigeeDeg: num(line2.slice(34, 42), 'argument of perigee', line2), // cols 35-42
    meanAnomalyDeg: num(line2.slice(43, 51), 'mean anomaly', line2), // cols 44-51
    meanMotionRevPerDay: num(line2.slice(52, 63), 'mean motion', line2), // cols 53-63
    bstar: parseExponential(line1.slice(53, 61)), // cols 54-61
    revAtEpoch: num(line2.slice(63, 68) || '0', 'revolution number', line2), // cols 64-68
    line1,
    line2,
  };
}

export interface TleParseOutcome {
  readonly elements: readonly ElementSet[];
  /** Objects the feed supplied that could not be parsed. Surfaced, never silently dropped. */
  readonly rejected: readonly { readonly line: string; readonly reason: string }[];
}

/**
 * Parses a whole CelesTrak `FORMAT=tle` response. Malformed sets are collected rather than
 * thrown, so one corrupt record in sixteen thousand does not discard the entire sync — but the
 * count is reported, because silently returning 15,999 of 16,000 objects while claiming success
 * is the kind of quiet data loss this project's architecture rules exist to prevent.
 */
export function parseTleCatalog(text: string, verifyChecksum = true): TleParseOutcome {
  const lines = text.split(/\r?\n/).filter((l) => l.trim() !== '');
  const elements: ElementSet[] = [];
  const rejected: { line: string; reason: string }[] = [];

  for (let i = 0; i + 2 < lines.length + 1; ) {
    const nameLine = lines[i];
    const l1 = lines[i + 1];
    const l2 = lines[i + 2];
    if (nameLine === undefined || l1 === undefined || l2 === undefined) break;
    try {
      elements.push(parseTleSet(nameLine, l1, l2, verifyChecksum));
    } catch (err) {
      rejected.push({
        line: `${nameLine} | ${l1.slice(0, 30)}`,
        reason: err instanceof Error ? err.message : String(err),
      });
    }
    i += 3;
  }

  return { elements, rejected };
}
