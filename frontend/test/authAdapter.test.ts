import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { createAccount, findAccount, readSession } from '../src/auth/accountStore';
import { orcaAuthAdapter } from '../src/auth/authAdapter';
import { resetServerConfig } from '../src/auth/serverAuth';

type Route = (init: RequestInit | undefined) => Response | Promise<Response>;

const PASSWORD = 'correct horse battery staple';

function json(status: number, body: unknown): Response {
  return new Response(JSON.stringify(body), { status, headers: { 'content-type': 'application/json' } });
}

/** A fake `fetch` answering only the auth routes given, and recording every call. */
function serve(routes: Record<string, Route>) {
  const calls: string[] = [];
  const fetchMock = vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
    const url = typeof input === 'string' ? input : input instanceof URL ? input.href : input.url;
    const path = url.replace(/^https?:\/\/[^/]+/, '');
    calls.push(`${init?.method ?? 'GET'} ${path}`);
    const route = routes[path];
    if (route) return route(init);
    // What a static host answers for an unknown path: the SPA's index.html.
    return new Response('<!doctype html><title>ORCA</title>', { status: 200, headers: { 'content-type': 'text/html' } });
  });
  vi.stubGlobal('fetch', fetchMock);
  return calls;
}

const SERVER_USER = {
  id: 'usr_1',
  email: 'yash@example.com',
  displayName: 'Yash',
  organisation: 'VIT Pune',
  provider: 'password',
};

describe('orcaAuthAdapter with and without the account service', () => {
  beforeEach(() => {
    window.localStorage.clear();
    resetServerConfig();
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('creates the account on the server when the service answers', async () => {
    const calls = serve({
      '/api/auth/config': () => json(200, { storage: true, googleClientId: null }),
      '/api/auth/signup': () => json(201, { user: SERVER_USER }),
    });

    const result = await orcaAuthAdapter.signUp({ email: 'yash@example.com', password: PASSWORD, organisation: 'VIT Pune' });

    expect(calls).toContain('POST /api/auth/signup');
    expect(result.redirectTo).toBe('#/workspace');
    expect(result.message).toMatch(/any device/);
    expect(readSession()).toMatchObject({ email: 'yash@example.com', source: 'server' });
    // Nothing was written to the device-local store: the server holds the account.
    expect(findAccount('yash@example.com')).toBeNull();
  });

  it('falls back to a device-local account on a static host', async () => {
    const calls = serve({});

    const result = await orcaAuthAdapter.signUp({ email: 'local@example.com', password: PASSWORD, organisation: 'VIT Pune' });

    expect(calls).not.toContain('POST /api/auth/signup');
    expect(result.message).toMatch(/saved in this browser/);
    expect(findAccount('local@example.com')).not.toBeNull();
    expect(readSession()?.source).toBeUndefined();
  });

  it('falls back to this device when the service fails, but not when it refuses', async () => {
    serve({
      '/api/auth/config': () => json(200, { storage: true, googleClientId: null }),
      '/api/auth/signup': () => json(500, { error: 'The account service failed. Nothing was changed.' }),
    });
    const result = await orcaAuthAdapter.signUp({ email: 'down@example.com', password: PASSWORD, organisation: 'VIT Pune' });
    expect(result.message).toMatch(/saved in this browser/);

    resetServerConfig();
    serve({
      '/api/auth/config': () => json(200, { storage: true, googleClientId: null }),
      '/api/auth/signup': () => json(409, { error: 'An account already exists for that address.' }),
    });
    await expect(
      orcaAuthAdapter.signUp({ email: 'taken@example.com', password: PASSWORD, organisation: 'VIT Pune' }),
    ).rejects.toThrow(/already exists/);
    expect(findAccount('taken@example.com')).toBeNull();
  });

  it('moves an account that existed only in this browser onto the server at sign-in', async () => {
    await createAccount({ email: 'early@example.com', password: PASSWORD, organisation: 'VIT Pune' });
    window.localStorage.removeItem('orca.session.v1');

    const calls = serve({
      '/api/auth/config': () => json(200, { storage: true, googleClientId: null }),
      '/api/auth/signin': () => json(401, { error: 'That email and password do not match an account.' }),
      '/api/auth/signup': () => json(201, { user: { ...SERVER_USER, email: 'early@example.com' } }),
    });

    const result = await orcaAuthAdapter.signIn({ email: 'early@example.com', password: PASSWORD });

    expect(calls).toEqual(['GET /api/auth/config', 'POST /api/auth/signin', 'POST /api/auth/signup']);
    expect(result.message).toMatch(/now on the server/);
    expect(readSession()).toMatchObject({ email: 'early@example.com', source: 'server' });
  });

  it("reports the service's refusal when no local account matches", async () => {
    serve({
      '/api/auth/config': () => json(200, { storage: true, googleClientId: null }),
      '/api/auth/signin': () => json(401, { error: 'That email and password do not match an account.' }),
    });

    await expect(orcaAuthAdapter.signIn({ email: 'nobody@example.com', password: PASSWORD })).rejects.toThrow(
      /do not match an account/,
    );
    expect(readSession()).toBeNull();
  });
});
