/**
 * CelesTrak SATCAT (`pub/satcat.csv`) parsing — the descriptive half of a catalog record.
 *
 * The GP feed carries orbital elements and nothing else: no object type, no owner, no
 * operational status. Those come from SATCAT and are joined on the NORAD catalog number. The
 * join is why a record can be honest about partial knowledge — an object present in GP but
 * absent from SATCAT keeps its real elements and reports `null` for every descriptive field,
 * which the UI renders as "Not available" rather than inventing an operator for it.
 *
 * SATCAT's own orbital columns (`PERIOD`, `INCLINATION`, `APOGEE`, `PERIGEE`) are parsed for
 * nothing: see `derive.ts` for why the element set is authoritative instead.
 */

import type { ObjectType, OpsStatus, SatcatMetadata } from './types.js';

/**
 * Minimal RFC 4180 field splitter: handles quoted fields and doubled quotes inside them.
 * SATCAT contains object names with commas ("SL-16 R/B, DEB"), so splitting on bare commas
 * shifts every subsequent column for exactly those rows.
 */
export function splitCsvLine(line: string): string[] {
  const out: string[] = [];
  let field = '';
  let inQuotes = false;
  for (let i = 0; i < line.length; i++) {
    const ch = line[i];
    if (inQuotes) {
      if (ch === '"') {
        if (line[i + 1] === '"') {
          field += '"';
          i++;
        } else {
          inQuotes = false;
        }
      } else {
        field += ch;
      }
    } else if (ch === '"') {
      inQuotes = true;
    } else if (ch === ',') {
      out.push(field);
      field = '';
    } else {
      field += ch;
    }
  }
  out.push(field);
  return out;
}

/** SATCAT `OBJECT_TYPE` codes. Anything else — including an empty cell — is UNKNOWN. */
function toObjectType(code: string): ObjectType | null {
  switch (code.trim().toUpperCase()) {
    case 'PAY':
      return 'PAYLOAD';
    case 'R/B':
      return 'ROCKET BODY';
    case 'DEB':
      return 'DEBRIS';
    case 'UNK':
      return 'UNKNOWN';
    case '':
      return null;
    default:
      return 'UNKNOWN';
  }
}

/**
 * SATCAT `OPS_STATUS_CODE`, per celestrak.org/satcat/status.php. An empty cell means the
 * catalog does not state a status, which is `null` — distinct from 'UNKNOWN', which is the
 * catalog explicitly recording that the status is not known.
 */
function toOpsStatus(code: string): OpsStatus | null {
  switch (code.trim()) {
    case '+':
      return 'OPERATIONAL';
    case 'P':
      return 'PARTIALLY_OPERATIONAL';
    case 'B':
      return 'BACKUP_STANDBY';
    case 'S':
      return 'SPARE';
    case 'X':
      return 'EXTENDED_MISSION';
    case '-':
      return 'NONOPERATIONAL';
    case 'D':
      return 'DECAYED';
    case '?':
      return 'UNKNOWN';
    case '':
      return null;
    default:
      return 'UNKNOWN';
  }
}

function nullableText(v: string | undefined): string | null {
  const s = (v ?? '').trim();
  return s === '' ? null : s;
}

function nullableNumber(v: string | undefined): number | null {
  const s = (v ?? '').trim();
  if (s === '') return null;
  const n = Number(s);
  return Number.isFinite(n) ? n : null;
}

/**
 * Parses the whole SATCAT CSV into a lookup keyed by NORAD catalog number. Columns are resolved
 * by header name rather than by position, so a provider adding a column does not silently
 * shift every field by one.
 */
export function parseSatcat(csv: string): Map<number, SatcatMetadata> {
  const out = new Map<number, SatcatMetadata>();
  const lines = csv.split(/\r?\n/);
  const headerLine = lines[0];
  if (headerLine === undefined) return out;

  const header = splitCsvLine(headerLine).map((h) => h.trim().toUpperCase());
  const col = (name: string): number => header.indexOf(name);
  const iNorad = col('NORAD_CAT_ID');
  if (iNorad < 0) return out;
  const iType = col('OBJECT_TYPE');
  const iStatus = col('OPS_STATUS_CODE');
  const iOwner = col('OWNER');
  const iLaunch = col('LAUNCH_DATE');
  const iSite = col('LAUNCH_SITE');
  const iDecay = col('DECAY_DATE');
  const iRcs = col('RCS');

  for (let i = 1; i < lines.length; i++) {
    const line = lines[i];
    if (line === undefined || line.trim() === '') continue;
    const f = splitCsvLine(line);
    const norad = Number((f[iNorad] ?? '').trim());
    if (!Number.isFinite(norad)) continue;
    out.set(norad, {
      objectType: iType >= 0 ? toObjectType(f[iType] ?? '') : null,
      opsStatus: iStatus >= 0 ? toOpsStatus(f[iStatus] ?? '') : null,
      owner: iOwner >= 0 ? nullableText(f[iOwner]) : null,
      launchDate: iLaunch >= 0 ? nullableText(f[iLaunch]) : null,
      launchSite: iSite >= 0 ? nullableText(f[iSite]) : null,
      decayDate: iDecay >= 0 ? nullableText(f[iDecay]) : null,
      rcsM2: iRcs >= 0 ? nullableNumber(f[iRcs]) : null,
    });
  }
  return out;
}

/** What a record looks like when SATCAT has no row for it: real elements, no invented metadata. */
export const UNKNOWN_METADATA: SatcatMetadata = {
  objectType: null,
  opsStatus: null,
  owner: null,
  launchDate: null,
  launchSite: null,
  decayDate: null,
  rcsM2: null,
};
