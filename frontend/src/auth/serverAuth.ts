/**
 * Client for the account service in `api/auth/[action].ts`.
 *
 * When the app is served by Vercel, accounts live on the server: an account made on one device
 * signs in on any other, and the session is an HttpOnly cookie no script can read. When the app
 * is served by anything else -- `vite preview`, a plain static host, a laptop with no network --
 * `/api/auth/config` does not answer with JSON, `serverConfig()` resolves `null`, and the
 * adapter keeps using the device-local store in `accountStore.ts`. Nothing here guesses which
 * situation it is in; it asks.
 */

import type { SessionUser } from './accountStore';

export interface ServerConfig {
  /** Whether the service has somewhere to keep accounts. */
  readonly storage: boolean;
  /** The deployment's Google OAuth client ID, served at runtime so that setting one environment
   * variable enables Google sign-in without rebuilding the frontend. */
  readonly googleClientId: string | null;
}

export class ServerAuthError extends Error {
  /** HTTP status, or 0 when the service could not be reached at all. */
  readonly status: number;

  constructor(message: string, status: number) {
    super(message);
    this.name = 'ServerAuthError';
    this.status = status;
  }
}

/** True for failures that say nothing about the credentials: the service is down or absent. */
export function isServiceUnavailable(error: unknown): boolean {
  return error instanceof ServerAuthError && (error.status === 0 || error.status >= 500);
}

const BASE = '/api/auth';

async function fetchWithTimeout(url: string, init: RequestInit, ms: number): Promise<Response> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), ms);
  try {
    return await fetch(url, { ...init, signal: controller.signal, credentials: 'same-origin' });
  } finally {
    clearTimeout(timer);
  }
}

let configRequest: Promise<ServerConfig | null> | null = null;

/** Asked once per page load. A static host answers `/api/auth/config` with `index.html` (the
 * SPA fallback) or a 404, neither of which is JSON, and both mean "no service here". */
export function serverConfig(): Promise<ServerConfig | null> {
  configRequest ??= (async () => {
    try {
      const response = await fetchWithTimeout(`${BASE}/config`, { headers: { accept: 'application/json' } }, 5_000);
      if (!response.ok) return null;
      if (!(response.headers.get('content-type') ?? '').includes('application/json')) return null;
      const body = (await response.json()) as { storage?: unknown; googleClientId?: unknown };
      if (typeof body.storage !== 'boolean') return null;
      const clientId = typeof body.googleClientId === 'string' ? body.googleClientId.trim() : '';
      return { storage: body.storage, googleClientId: clientId.length > 0 ? clientId : null };
    } catch {
      return null;
    }
  })();
  return configRequest;
}

/** Test seam: forget the cached answer so the next call asks again. */
export function resetServerConfig(): void {
  configRequest = null;
}

interface WireUser {
  readonly id?: unknown;
  readonly email?: unknown;
  readonly displayName?: unknown;
  readonly organisation?: unknown;
  readonly provider?: unknown;
  readonly avatarUrl?: unknown;
}

function toSessionUser(user: WireUser | null | undefined): SessionUser {
  if (!user || typeof user.id !== 'string' || typeof user.email !== 'string') {
    throw new ServerAuthError('The account service answered without an account.', 502);
  }
  return {
    id: user.id,
    email: user.email,
    displayName: typeof user.displayName === 'string' ? user.displayName : user.email,
    organisation: typeof user.organisation === 'string' ? user.organisation : '',
    provider: user.provider === 'google' ? 'google' : 'password',
    avatarUrl: typeof user.avatarUrl === 'string' ? user.avatarUrl : undefined,
    signedInAtIso: new Date().toISOString(),
    source: 'server',
  };
}

async function post(action: string, body: Record<string, string>): Promise<SessionUser> {
  let response: Response;
  try {
    response = await fetchWithTimeout(
      `${BASE}/${action}`,
      {
        method: 'POST',
        headers: { 'content-type': 'application/json', accept: 'application/json' },
        body: JSON.stringify(body),
      },
      15_000,
    );
  } catch {
    throw new ServerAuthError('The account service could not be reached.', 0);
  }

  let payload: { user?: WireUser; error?: unknown } | null = null;
  try {
    payload = (await response.json()) as { user?: WireUser; error?: unknown };
  } catch {
    payload = null;
  }

  if (!response.ok) {
    const message =
      payload && typeof payload.error === 'string'
        ? payload.error
        : `The account service answered HTTP ${response.status}.`;
    throw new ServerAuthError(message, response.status);
  }
  return toSessionUser(payload?.user);
}

export function serverSignUp(input: {
  readonly email: string;
  readonly password: string;
  readonly organisation: string;
}): Promise<SessionUser> {
  return post('signup', { email: input.email, password: input.password, organisation: input.organisation });
}

export function serverSignIn(email: string, password: string): Promise<SessionUser> {
  return post('signin', { email, password });
}

/** Hands Google's access token to the service, which asks Google who it belongs to and who it
 * was issued for before trusting it. The token is not kept by the browser afterwards. */
export function serverGoogle(accessToken: string): Promise<SessionUser> {
  return post('google', { accessToken });
}

export async function serverSignOut(): Promise<void> {
  try {
    await fetchWithTimeout(`${BASE}/signout`, { method: 'POST' }, 5_000);
  } catch {
    // The local mirror is cleared regardless; an unreachable service cannot be holding a
    // session that matters to this tab.
  }
}

/** `true` if the service still recognises this browser's session cookie, `false` if it does not,
 * `null` if it could not be asked. */
export async function serverSessionValid(): Promise<boolean | null> {
  try {
    const response = await fetchWithTimeout(`${BASE}/session`, { headers: { accept: 'application/json' } }, 5_000);
    if (response.status === 200) return true;
    if (response.status === 401) return false;
    return null;
  } catch {
    return null;
  }
}
