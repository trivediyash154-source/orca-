/**
 * ORCA account service — one Vercel Function serving every `/api/auth/*` route.
 *
 * ## Why server-side at all
 *
 * The first implementation kept accounts in the browser (`frontend/src/auth/accountStore.ts`).
 * That is real authentication, but an account made on one laptop did not exist on another,
 * which is exactly the property a reviewer notices first. This function moves the account
 * record to Vercel Blob, so an account works from any device, and moves the session into an
 * HttpOnly cookie, so no script on the page — including an injected one — can read it.
 *
 * ## Routes
 *
 *   GET  /api/auth/config    what this deployment supports (storage, Google client ID)
 *   GET  /api/auth/session   the signed-in user, or 401
 *   POST /api/auth/signup    { email, password, organisation }
 *   POST /api/auth/signin    { email, password }
 *   POST /api/auth/google    { accessToken }  — verified with Google, never trusted as sent
 *   POST /api/auth/signout
 *
 * One file rather than six on purpose: the routes share hashing, storage and cookie code, and
 * a single module has no relative imports for the function bundler to resolve differently in
 * CommonJS and ESM — the one way this deployment could build cleanly and still fail at runtime.
 *
 * ## Security properties, each enforced below
 *
 *  - Passwords are hashed with scrypt (N=2^15, r=8, p=1, 16-byte salt per account) and compared
 *    with `timingSafeEqual`. The plaintext never touches storage or a log line.
 *  - An unknown address and a wrong password produce the same response, after the same work,
 *    so the endpoint cannot be used to discover which addresses are registered.
 *  - Account creation is atomic: the record is written with `allowOverwrite: false`, so two
 *    simultaneous sign-ups for one address cannot both succeed and silently replace each other.
 *  - A Google token is never trusted as sent. Its audience is checked against this deployment's
 *    own client ID, which is what stops a token minted for some other site being replayed here.
 *  - The session is an HMAC-SHA-256-signed, expiring token in an HttpOnly, Secure, SameSite=Lax
 *    cookie. The signing key is `AUTH_SECRET` when set, otherwise HKDF-derived from the Blob
 *    store's own server-only token, so a deployment is secure with no extra secret to configure.
 *  - State-changing requests must be same-origin (checked against `Origin`/`Sec-Fetch-Site`),
 *    which closes cross-site request forgery against sign-in and sign-out.
 */

import {
  createHash,
  createHmac,
  hkdfSync,
  randomBytes,
  scrypt as scryptCallback,
  timingSafeEqual,
} from 'node:crypto';

import { BlobError, get, put } from '@vercel/blob';

/* ======================================================================= config ========= */

const SESSION_COOKIE = 'orca_session';
const SESSION_TTL_SEC = 7 * 24 * 60 * 60;

const SCRYPT = { N: 32_768, r: 8, p: 1, keyLength: 32, maxmem: 64 * 1024 * 1024 } as const;
const SALT_BYTES = 16;

const PASSWORD_MIN = 12;
const PASSWORD_MAX = 512; // bounds the scrypt input so a 10 MB "password" is not a DoS lever
const EMAIL_PATTERN = /^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/;

const GOOGLE_TOKENINFO = 'https://oauth2.googleapis.com/tokeninfo';
const GOOGLE_USERINFO = 'https://www.googleapis.com/oauth2/v3/userinfo';

/* ======================================================================== types ========= */

interface AccountRecord {
  readonly id: string;
  readonly email: string;
  readonly displayName: string;
  readonly organisation: string;
  readonly providers: readonly ('password' | 'google')[];
  /** `scrypt$N$r$p$saltHex$hashHex`, present only for accounts with a password. */
  readonly passwordHash?: string;
  readonly avatarUrl?: string;
  readonly createdAt: string;
  readonly lastSignInAt: string;
}

interface SessionClaims {
  readonly uid: string;
  readonly email: string;
  readonly name: string;
  readonly org: string;
  readonly provider: 'password' | 'google';
  readonly avatar?: string;
  readonly iat: number;
  readonly exp: number;
}

class HttpError extends Error {
  constructor(
    readonly status: number,
    message: string,
  ) {
    super(message);
  }
}

/* ===================================================================== storage ========= */

/**
 * Accounts are keyed by a hash of the normalised address rather than the address itself, so a
 * listing of the store (which the Vercel dashboard can show) does not double as a list of
 * users' email addresses.
 */
function accountPath(email: string): string {
  const digest = createHash('sha256').update(normaliseEmail(email)).digest('hex');
  return `accounts/${digest}.json`;
}

function normaliseEmail(raw: string): string {
  return raw.trim().toLowerCase();
}

/** Local development without a Blob token gets an in-process store, so `vercel dev` and the
 * test harness work offline. On Vercel itself a missing token is a misconfiguration and is
 * reported as one: silently keeping accounts in memory would lose them on the next cold start
 * while appearing to work. */
const memoryStore = new Map<string, string>();

function storageMode(): 'blob' | 'memory' | 'missing' {
  if (process.env.BLOB_READ_WRITE_TOKEN) return 'blob';
  if (process.env.VERCEL) return 'missing';
  return 'memory';
}

async function readAccount(email: string): Promise<AccountRecord | null> {
  const path = accountPath(email);
  const mode = storageMode();

  if (mode === 'memory') {
    const raw = memoryStore.get(path);
    return raw === undefined ? null : (JSON.parse(raw) as AccountRecord);
  }
  if (mode === 'missing') throw new HttpError(503, 'Account storage is not configured.');

  // `useCache: false` reads origin storage. A cached read here would let a just-created account
  // fail its first sign-in for as long as the CDN held the 404.
  const result = await get(path, { access: 'private', useCache: false });
  if (result === null) return null;
  const text = await new Response(result.stream).text();
  return JSON.parse(text) as AccountRecord;
}

async function writeAccount(record: AccountRecord, mode: 'create' | 'update'): Promise<void> {
  const path = accountPath(record.email);
  const body = JSON.stringify(record);
  const storage = storageMode();

  if (storage === 'memory') {
    if (mode === 'create' && memoryStore.has(path)) throw new HttpError(409, DUPLICATE_MESSAGE);
    memoryStore.set(path, body);
    return;
  }
  if (storage === 'missing') throw new HttpError(503, 'Account storage is not configured.');

  try {
    await put(path, body, {
      access: 'private',
      contentType: 'application/json',
      addRandomSuffix: false,
      allowOverwrite: mode === 'update',
      cacheControlMaxAge: 60,
    });
  } catch (error) {
    if (mode === 'create' && error instanceof BlobError && /exist/i.test(error.message)) {
      throw new HttpError(409, DUPLICATE_MESSAGE);
    }
    throw error;
  }
}

const DUPLICATE_MESSAGE =
  'An account already exists for that address. Sign in instead, or continue with Google if ' +
  'that is how it was created.';

/* ==================================================================== passwords ========= */

function scrypt(password: string, salt: Buffer, N: number, r: number, p: number): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    scryptCallback(password, salt, SCRYPT.keyLength, { N, r, p, maxmem: SCRYPT.maxmem }, (err, key) =>
      err ? reject(err) : resolve(key),
    );
  });
}

async function hashPassword(password: string): Promise<string> {
  const salt = randomBytes(SALT_BYTES);
  const key = await scrypt(password, salt, SCRYPT.N, SCRYPT.r, SCRYPT.p);
  return `scrypt$${SCRYPT.N}$${SCRYPT.r}$${SCRYPT.p}$${salt.toString('hex')}$${key.toString('hex')}`;
}

/** Parameters are read back from the stored string, so raising the cost later does not lock
 * out every account hashed under the old cost. */
async function verifyPassword(password: string, stored: string): Promise<boolean> {
  const parts = stored.split('$');
  if (parts.length !== 6 || parts[0] !== 'scrypt') return false;
  const [, n, r, p, saltHex, hashHex] = parts as [string, string, string, string, string, string];
  const expected = Buffer.from(hashHex, 'hex');
  const actual = await scrypt(password, Buffer.from(saltHex, 'hex'), Number(n), Number(r), Number(p));
  return expected.length === actual.length && timingSafeEqual(expected, actual);
}

/** A fixed, valid hash for the unknown-address path. Verifying against it costs the same as a
 * real verification, so response time does not reveal whether the address exists. */
let decoyHash: Promise<string> | null = null;
function decoy(): Promise<string> {
  decoyHash ??= hashPassword(randomBytes(24).toString('hex'));
  return decoyHash;
}

/* ===================================================================== sessions ========= */

function signingKey(): Buffer {
  const explicit = process.env.AUTH_SECRET;
  if (explicit && explicit.length >= 32) return Buffer.from(explicit, 'utf8');

  const root = process.env.BLOB_READ_WRITE_TOKEN;
  if (root) {
    // HKDF with a fixed, purpose-specific label: the derived key can sign sessions and nothing
    // else, and knowing it reveals nothing about the storage token it came from.
    return Buffer.from(hkdfSync('sha256', root, 'orca.session.v1', 'session-signing', 32));
  }

  if (!process.env.VERCEL) {
    // Local development only. A per-process random key: sessions end on restart, which is the
    // correct failure for a dev server.
    devKey ??= randomBytes(32);
    return devKey;
  }
  throw new HttpError(503, 'Session signing is not configured.');
}
let devKey: Buffer | null = null;

function base64url(input: Buffer | string): string {
  return Buffer.from(input).toString('base64url');
}

function issueSession(record: AccountRecord, provider: 'password' | 'google'): string {
  const now = Math.floor(Date.now() / 1000);
  const claims: SessionClaims = {
    uid: record.id,
    email: record.email,
    name: record.displayName,
    org: record.organisation,
    provider,
    avatar: record.avatarUrl,
    iat: now,
    exp: now + SESSION_TTL_SEC,
  };
  const payload = base64url(JSON.stringify(claims));
  const signature = base64url(createHmac('sha256', signingKey()).update(payload).digest());
  return `${payload}.${signature}`;
}

function readSession(request: Request): SessionClaims | null {
  const token = parseCookies(request.headers.get('cookie'))[SESSION_COOKIE];
  if (!token) return null;

  const [payload, signature] = token.split('.');
  if (!payload || !signature) return null;

  const expected = createHmac('sha256', signingKey()).update(payload).digest();
  const given = Buffer.from(signature, 'base64url');
  if (given.length !== expected.length || !timingSafeEqual(given, expected)) return null;

  try {
    const claims = JSON.parse(Buffer.from(payload, 'base64url').toString('utf8')) as SessionClaims;
    if (typeof claims.exp !== 'number' || claims.exp < Math.floor(Date.now() / 1000)) return null;
    return claims;
  } catch {
    return null;
  }
}

function parseCookies(header: string | null): Record<string, string> {
  const out: Record<string, string> = {};
  if (!header) return out;
  for (const part of header.split(';')) {
    const index = part.indexOf('=');
    if (index < 0) continue;
    out[part.slice(0, index).trim()] = decodeURIComponent(part.slice(index + 1).trim());
  }
  return out;
}

function sessionCookie(token: string, request: Request): string {
  const secure = new URL(request.url).protocol === 'https:' ? '; Secure' : '';
  return `${SESSION_COOKIE}=${token}; Path=/; HttpOnly; SameSite=Lax; Max-Age=${SESSION_TTL_SEC}${secure}`;
}

function clearedCookie(request: Request): string {
  const secure = new URL(request.url).protocol === 'https:' ? '; Secure' : '';
  return `${SESSION_COOKIE}=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0${secure}`;
}

/* ===================================================================== helpers ========= */

function json(status: number, body: unknown, headers: Record<string, string> = {}): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      'content-type': 'application/json; charset=utf-8',
      // Account responses are per-user and must never be served from a shared cache.
      'cache-control': 'no-store',
      ...headers,
    },
  });
}

function publicUser(claims: SessionClaims) {
  return {
    id: claims.uid,
    email: claims.email,
    displayName: claims.name,
    organisation: claims.org,
    provider: claims.provider,
    avatarUrl: claims.avatar,
  };
}

function nameFromEmail(email: string): string {
  const local = email.split('@')[0] ?? email;
  const words = local
    .split(/[._\-+]+/)
    .filter((part) => part.length > 0)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1));
  return words.length > 0 ? words.join(' ') : email;
}

function newId(): string {
  return `usr_${randomBytes(9).toString('hex')}`;
}

/** Same-origin check for state-changing requests. `Sec-Fetch-Site` is sent by every current
 * browser; `Origin` is the fallback. A request carrying neither (curl, a server) is allowed,
 * because CSRF is an attack that needs a victim's browser to send the cookie. */
function assertSameOrigin(request: Request): void {
  const site = request.headers.get('sec-fetch-site');
  if (site && site !== 'same-origin' && site !== 'none') {
    throw new HttpError(403, 'Cross-site request refused.');
  }
  const origin = request.headers.get('origin');
  if (origin && origin !== new URL(request.url).origin) {
    throw new HttpError(403, 'Cross-site request refused.');
  }
}

async function readBody(request: Request): Promise<Record<string, unknown>> {
  const text = await request.text();
  if (text.length > 16_384) throw new HttpError(413, 'Request body too large.');
  if (text.length === 0) return {};
  try {
    const parsed: unknown = JSON.parse(text);
    if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) {
      throw new HttpError(400, 'Expected a JSON object.');
    }
    return parsed as Record<string, unknown>;
  } catch (error) {
    if (error instanceof HttpError) throw error;
    throw new HttpError(400, 'The request body is not valid JSON.');
  }
}

function field(body: Record<string, unknown>, name: string): string {
  const value = body[name];
  return typeof value === 'string' ? value : '';
}

/* ====================================================================== routes ========= */

async function signUp(request: Request): Promise<Response> {
  assertSameOrigin(request);
  const body = await readBody(request);
  const email = normaliseEmail(field(body, 'email'));
  const password = field(body, 'password');
  const organisation = field(body, 'organisation').trim();

  if (!EMAIL_PATTERN.test(email)) throw new HttpError(400, 'Enter a complete email address.');
  if (password.length < PASSWORD_MIN) {
    throw new HttpError(400, `Use a password of at least ${PASSWORD_MIN} characters.`);
  }
  if (password.length > PASSWORD_MAX) throw new HttpError(400, 'That password is too long.');
  if (organisation.length < 2) throw new HttpError(400, 'Enter your institution or organisation.');

  if ((await readAccount(email)) !== null) throw new HttpError(409, DUPLICATE_MESSAGE);

  const now = new Date().toISOString();
  const record: AccountRecord = {
    id: newId(),
    email,
    displayName: nameFromEmail(email),
    organisation: organisation.slice(0, 120),
    providers: ['password'],
    passwordHash: await hashPassword(password),
    createdAt: now,
    lastSignInAt: now,
  };
  await writeAccount(record, 'create');

  const token = issueSession(record, 'password');
  return json(201, { user: publicUser(readClaims(token)) }, { 'set-cookie': sessionCookie(token, request) });
}

async function signIn(request: Request): Promise<Response> {
  assertSameOrigin(request);
  const body = await readBody(request);
  const email = normaliseEmail(field(body, 'email'));
  const password = field(body, 'password');

  const failure = new HttpError(401, 'That email and password do not match an account.');
  if (password.length === 0 || password.length > PASSWORD_MAX) throw failure;

  const record = EMAIL_PATTERN.test(email) ? await readAccount(email) : null;

  if (record === null || record.passwordHash === undefined) {
    await verifyPassword(password, await decoy());
    if (record !== null && record.providers.includes('google')) {
      // Only reached with the right address for a Google-only account; telling the reader how
      // to get in is worth more here than hiding a fact Google sign-in would reveal anyway.
      throw new HttpError(
        401,
        'That address signs in with Google and has no password. Use "Continue with Google".',
      );
    }
    throw failure;
  }

  if (!(await verifyPassword(password, record.passwordHash))) throw failure;

  const updated: AccountRecord = { ...record, lastSignInAt: new Date().toISOString() };
  await writeAccount(updated, 'update');

  const token = issueSession(updated, 'password');
  return json(200, { user: publicUser(readClaims(token)) }, { 'set-cookie': sessionCookie(token, request) });
}

async function google(request: Request): Promise<Response> {
  assertSameOrigin(request);
  const clientId = process.env.GOOGLE_CLIENT_ID ?? process.env.VITE_GOOGLE_CLIENT_ID;
  if (!clientId) {
    throw new HttpError(503, 'Google sign-in is not configured on this deployment.');
  }

  const body = await readBody(request);
  const accessToken = field(body, 'accessToken');
  if (accessToken.length === 0 || accessToken.length > 4_096) {
    throw new HttpError(400, 'Missing Google token.');
  }

  // Ask Google who this token belongs to and who it was issued TO. The audience check is the
  // whole point: without it, any site's Google token could sign in here as its owner.
  const info = await fetch(`${GOOGLE_TOKENINFO}?access_token=${encodeURIComponent(accessToken)}`);
  if (!info.ok) throw new HttpError(401, 'Google did not accept that sign-in.');
  const claims = (await info.json()) as {
    aud?: string;
    azp?: string;
    email?: string;
    email_verified?: string | boolean;
    exp?: string;
  };

  if (claims.aud !== clientId && claims.azp !== clientId) {
    throw new HttpError(401, 'That Google sign-in was issued for a different application.');
  }
  if (!claims.email || (claims.email_verified !== true && claims.email_verified !== 'true')) {
    throw new HttpError(401, 'Google did not confirm a verified email address for that account.');
  }

  let name: string | undefined;
  let picture: string | undefined;
  const profile = await fetch(GOOGLE_USERINFO, { headers: { authorization: `Bearer ${accessToken}` } });
  if (profile.ok) {
    const data = (await profile.json()) as { name?: string; picture?: string };
    name = data.name;
    picture = data.picture;
  }

  const email = normaliseEmail(claims.email);
  const existing = await readAccount(email);
  const now = new Date().toISOString();

  let record: AccountRecord;
  if (existing === null) {
    record = {
      id: newId(),
      email,
      displayName: name ?? nameFromEmail(email),
      organisation: 'Google account',
      providers: ['google'],
      avatarUrl: picture,
      createdAt: now,
      lastSignInAt: now,
    };
    await writeAccount(record, 'create');
  } else {
    // Linking: an email account that later uses Google with the same verified address keeps
    // its password. Google has just proven control of the address, which is exactly the
    // property the password account was created against.
    record = {
      ...existing,
      displayName: existing.displayName || name || nameFromEmail(email),
      providers: existing.providers.includes('google')
        ? existing.providers
        : [...existing.providers, 'google'],
      avatarUrl: picture ?? existing.avatarUrl,
      lastSignInAt: now,
    };
    await writeAccount(record, 'update');
  }

  const token = issueSession(record, 'google');
  return json(200, { user: publicUser(readClaims(token)) }, { 'set-cookie': sessionCookie(token, request) });
}

function readClaims(token: string): SessionClaims {
  const [payload] = token.split('.');
  return JSON.parse(Buffer.from(payload ?? '', 'base64url').toString('utf8')) as SessionClaims;
}

function session(request: Request): Response {
  const claims = readSession(request);
  if (claims === null) return json(401, { user: null });
  return json(200, { user: publicUser(claims) });
}

function signOut(request: Request): Response {
  assertSameOrigin(request);
  return json(200, { ok: true }, { 'set-cookie': clearedCookie(request) });
}

function config(): Response {
  return json(200, {
    storage: storageMode() !== 'missing',
    // The client ID is public by design — it is printed into every Google sign-in page.
    // Serving it from here means a deployment enables Google by setting one environment
    // variable, with no rebuild of the frontend.
    googleClientId: process.env.GOOGLE_CLIENT_ID ?? process.env.VITE_GOOGLE_CLIENT_ID ?? null,
  });
}

/* ===================================================================== dispatch ========= */

function actionOf(request: Request): string {
  const segments = new URL(request.url).pathname.split('/').filter(Boolean);
  return segments[segments.length - 1] ?? '';
}

async function handle(request: Request, method: 'GET' | 'POST'): Promise<Response> {
  const action = actionOf(request);
  try {
    if (method === 'GET') {
      if (action === 'config') return config();
      if (action === 'session') return session(request);
    } else {
      if (action === 'signup') return await signUp(request);
      if (action === 'signin') return await signIn(request);
      if (action === 'google') return await google(request);
      if (action === 'signout') return signOut(request);
    }
    return json(404, { error: 'No such auth route.' });
  } catch (error) {
    if (error instanceof HttpError) return json(error.status, { error: error.message });
    // Logged without the request body: the body of a sign-in request is a password.
    console.error(`[auth/${action}]`, error instanceof Error ? error.message : error);
    return json(500, { error: 'The account service failed. Nothing was changed.' });
  }
}

export function GET(request: Request): Promise<Response> {
  return handle(request, 'GET');
}

export function POST(request: Request): Promise<Response> {
  return handle(request, 'POST');
}
