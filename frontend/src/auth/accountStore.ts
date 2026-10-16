/**
 * Account and session storage for the entry surface.
 *
 * ORCA has no identity server: the C++ engine owns orbital state and nothing else, and the
 * Node bridge is transport only. So accounts live on the device that created them, in
 * `localStorage`, under this module's sole ownership. That is a real, working account system
 * with real credential checking -- it is simply scoped to one browser, which is stated in the
 * UI rather than hidden.
 *
 * ## What is and is not stored
 *
 * A password is never stored, in any form that can be read back. On sign-up the password is
 * stretched with PBKDF2-HMAC-SHA-256 (210 000 iterations, the OWASP 2023 figure for this
 * primitive) over a fresh 16-byte random salt, and only the salt and the derived key are
 * written. Sign-in repeats the derivation with the stored salt and compares the result in
 * constant time, so neither the hash nor the comparison leaks the secret.
 *
 * WebCrypto is required for that and is only exposed on secure origins (https, or localhost).
 * There is no weaker fallback: silently downgrading to a fast or reversible hash would be the
 * kind of defect that is invisible until it matters. On an origin without it, account creation
 * reports that plainly.
 */

export interface StoredAccount {
  readonly id: string;
  readonly email: string;
  readonly displayName: string;
  readonly organisation: string;
  /** `'password'` accounts carry `salt`/`hash`; `'google'` accounts never do -- the provider
   * did the authenticating, so there is no local secret to verify and none is invented. */
  readonly provider: 'password' | 'google';
  readonly salt?: string;
  readonly hash?: string;
  readonly createdAtIso: string;
  readonly lastSignInIso: string;
  readonly avatarUrl?: string;
}

export interface SessionUser {
  readonly id: string;
  readonly email: string;
  readonly displayName: string;
  readonly organisation: string;
  readonly provider: 'password' | 'google';
  readonly avatarUrl?: string;
  readonly signedInAtIso: string;
  /** `'server'` when the account service issued the session (its HttpOnly cookie is the real
   * credential and this record is a mirror for display); absent for a device-local session. */
  readonly source?: 'server';
}

const ACCOUNTS_KEY = 'orca.accounts.v1';
const SESSION_KEY = 'orca.session.v1';

const PBKDF2_ITERATIONS = 210_000;
const SALT_BYTES = 16;
const DERIVED_BITS = 256;

export class AuthStorageError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'AuthStorageError';
  }
}

function subtle(): SubtleCrypto {
  const c = typeof globalThis === 'undefined' ? undefined : globalThis.crypto;
  if (!c || !c.subtle) {
    throw new AuthStorageError(
      'This browser does not expose WebCrypto on this origin, so a password cannot be hashed ' +
        'safely. Open the site over https (or on localhost), or continue with Google.',
    );
  }
  return c.subtle;
}

function randomBytes(length: number): Uint8Array {
  const out = new Uint8Array(length);
  globalThis.crypto.getRandomValues(out);
  return out;
}

function toHex(bytes: ArrayBuffer | Uint8Array): string {
  const view = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  let out = '';
  for (const byte of view) out += byte.toString(16).padStart(2, '0');
  return out;
}

/** Length-independent, early-exit-free comparison. Both inputs here are fixed-length hex of a
 * 256-bit digest, but the loop is written so that a future change of length cannot turn this
 * into a timing oracle by accident. */
function constantTimeEquals(a: string, b: string): boolean {
  const length = Math.max(a.length, b.length);
  let diff = a.length ^ b.length;
  for (let i = 0; i < length; i += 1) {
    diff |= (a.charCodeAt(i) || 0) ^ (b.charCodeAt(i) || 0);
  }
  return diff === 0;
}

async function derive(password: string, saltHex: string): Promise<string> {
  const api = subtle();
  const salt = Uint8Array.from(
    saltHex.match(/.{1,2}/g)?.map((pair) => Number.parseInt(pair, 16)) ?? [],
  );
  const key = await api.importKey('raw', new TextEncoder().encode(password), 'PBKDF2', false, [
    'deriveBits',
  ]);
  const bits = await api.deriveBits(
    { name: 'PBKDF2', hash: 'SHA-256', salt, iterations: PBKDF2_ITERATIONS },
    key,
    DERIVED_BITS,
  );
  return toHex(bits);
}

function readAll(): StoredAccount[] {
  try {
    const raw = window.localStorage.getItem(ACCOUNTS_KEY);
    if (raw === null) return [];
    const parsed: unknown = JSON.parse(raw);
    if (!Array.isArray(parsed)) return [];
    // Anything that does not carry the two fields every lookup depends on is dropped rather
    // than trusted: this value can be edited by hand in devtools.
    return parsed.filter(
      (entry): entry is StoredAccount =>
        typeof entry === 'object' &&
        entry !== null &&
        typeof (entry as StoredAccount).email === 'string' &&
        typeof (entry as StoredAccount).id === 'string',
    );
  } catch {
    return [];
  }
}

function writeAll(accounts: readonly StoredAccount[]): void {
  try {
    window.localStorage.setItem(ACCOUNTS_KEY, JSON.stringify(accounts));
  } catch {
    throw new AuthStorageError(
      'This browser is blocking site storage, so the account could not be saved. Allow site ' +
        'data for this origin, or leave private browsing, and try again.',
    );
  }
}

export function normaliseEmail(raw: string): string {
  return raw.trim().toLowerCase();
}

export function listAccounts(): readonly StoredAccount[] {
  return readAll();
}

export function findAccount(email: string): StoredAccount | null {
  const wanted = normaliseEmail(email);
  return readAll().find((account) => normaliseEmail(account.email) === wanted) ?? null;
}

/** Derived from the address, not a random value: "jdoe@lab.edu" becomes "Jdoe", which is a
 * better greeting than the raw address and is never presented as a verified real name. */
function nameFromEmail(email: string): string {
  const local = email.split('@')[0] ?? email;
  const words = local
    .split(/[._\-+]+/)
    .filter((part) => part.length > 0)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1));
  return words.length > 0 ? words.join(' ') : email;
}

function newId(): string {
  return `usr_${toHex(randomBytes(8))}`;
}

export interface CreateAccountInput {
  readonly email: string;
  readonly password: string;
  readonly organisation: string;
}

export async function createAccount(input: CreateAccountInput): Promise<SessionUser> {
  const email = normaliseEmail(input.email);
  if (findAccount(email) !== null) {
    throw new AuthStorageError(
      'An account already exists on this device for that address. Sign in instead, or use the ' +
        'password reset link below the form.',
    );
  }

  const salt = toHex(randomBytes(SALT_BYTES));
  const hash = await derive(input.password, salt);
  const nowIso = new Date().toISOString();

  const account: StoredAccount = {
    id: newId(),
    email,
    displayName: nameFromEmail(email),
    organisation: input.organisation.trim(),
    provider: 'password',
    salt,
    hash,
    createdAtIso: nowIso,
    lastSignInIso: nowIso,
  };

  writeAll([...readAll(), account]);
  return startSession(account);
}

export async function signInWithPassword(email: string, password: string): Promise<SessionUser> {
  const account = findAccount(email);

  // One message for "no such account" and "wrong password", because two messages tell an
  // attacker which addresses are registered. The derivation still runs in the miss case so the
  // two paths take comparable time.
  const failure = new AuthStorageError(
    'That email and password do not match an account on this device. Accounts are stored per ' +
      'browser, so an account made elsewhere will not be found here.',
  );

  if (account === null) {
    await derive(password, toHex(randomBytes(SALT_BYTES)));
    throw failure;
  }
  if (account.provider === 'google') {
    throw new AuthStorageError(
      'That address is registered through Google on this device. Use "Continue with Google" ' +
        'above — it has no password to enter.',
    );
  }
  if (account.salt === undefined || account.hash === undefined) {
    throw failure;
  }

  const candidate = await derive(password, account.salt);
  if (!constantTimeEquals(candidate, account.hash)) throw failure;

  return touchAndStart(account);
}

export interface GoogleProfile {
  readonly email: string;
  readonly name?: string;
  readonly picture?: string;
}

/**
 * Federated entry. A Google account is created on first use and reused afterwards, which is
 * what makes the second visit a one-tap sign-in with no password -- exactly as the provider's
 * own flow behaves.
 */
export function signInWithGoogleProfile(profile: GoogleProfile): SessionUser {
  const email = normaliseEmail(profile.email);
  const existing = findAccount(email);
  const nowIso = new Date().toISOString();

  if (existing !== null) {
    const upgraded: StoredAccount = {
      ...existing,
      // An email account that later signs in with the same Google address keeps its password:
      // removing it would silently lock the reader out of the way they first signed up.
      displayName: profile.name ?? existing.displayName,
      avatarUrl: profile.picture ?? existing.avatarUrl,
      lastSignInIso: nowIso,
    };
    writeAll(readAll().map((entry) => (entry.id === existing.id ? upgraded : entry)));
    return startSession(upgraded);
  }

  const account: StoredAccount = {
    id: newId(),
    email,
    displayName: profile.name ?? nameFromEmail(email),
    organisation: 'Google account',
    provider: 'google',
    createdAtIso: nowIso,
    lastSignInIso: nowIso,
    avatarUrl: profile.picture,
  };
  writeAll([...readAll(), account]);
  return startSession(account);
}

function touchAndStart(account: StoredAccount): SessionUser {
  const updated: StoredAccount = { ...account, lastSignInIso: new Date().toISOString() };
  writeAll(readAll().map((entry) => (entry.id === account.id ? updated : entry)));
  return startSession(updated);
}

function startSession(account: StoredAccount): SessionUser {
  const user: SessionUser = {
    id: account.id,
    email: account.email,
    displayName: account.displayName,
    organisation: account.organisation,
    provider: account.provider,
    avatarUrl: account.avatarUrl,
    signedInAtIso: new Date().toISOString(),
  };
  try {
    window.localStorage.setItem(SESSION_KEY, JSON.stringify(user));
  } catch {
    // A session that cannot be persisted is still a valid session for this tab; the reader is
    // simply signed out on reload. Failing the sign-in over it would be worse.
  }
  return user;
}

/**
 * Records a session the account service issued, so every surface that reads the session from
 * storage -- the landing nav, the workspace guard, the top bar -- sees it without a second code
 * path. The service's cookie stays the credential; this copy carries no secret.
 */
export function adoptSession(user: SessionUser): SessionUser {
  try {
    window.localStorage.setItem(SESSION_KEY, JSON.stringify(user));
  } catch {
    // As in `startSession`: valid for this tab even if it cannot be persisted.
  }
  return user;
}

export function readSession(): SessionUser | null {
  try {
    const raw = window.localStorage.getItem(SESSION_KEY);
    if (raw === null) return null;
    const parsed: unknown = JSON.parse(raw);
    if (
      typeof parsed === 'object' &&
      parsed !== null &&
      typeof (parsed as SessionUser).email === 'string' &&
      typeof (parsed as SessionUser).id === 'string'
    ) {
      return parsed as SessionUser;
    }
  } catch {
    // Unreadable session: treated as signed out, which is the safe direction.
  }
  return null;
}

export function clearSession(): void {
  try {
    window.localStorage.removeItem(SESSION_KEY);
  } catch {
    // Nothing to do: if storage cannot be written it cannot be holding a session either.
  }
}

/** Password reset without a mail server. The reader proves control of the device rather than of
 * the mailbox, which is the only proof available here and is labelled as such in the UI. */
export async function resetPasswordOnDevice(email: string, nextPassword: string): Promise<void> {
  const account = findAccount(email);
  if (account === null || account.provider !== 'password') {
    throw new AuthStorageError(
      'No password account on this device uses that address, so there is nothing to reset here.',
    );
  }
  const salt = toHex(randomBytes(SALT_BYTES));
  const hash = await derive(nextPassword, salt);
  writeAll(
    readAll().map((entry) => (entry.id === account.id ? { ...entry, salt, hash } : entry)),
  );
}
