import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import type {
  AuthAdapter,
  AuthOperation,
  AuthPayloads,
  AuthResult,
} from '../src/services/authService';

/** House rule 5: this repository ships no authentication backend, and the service must fail
 * honestly rather than simulate a session. These tests are the guard on that — every operation
 * must reject, the rejection must be an `AuthNotConfiguredError`, and its message must state
 * that nothing happened without anywhere claiming that something did. The second half pins the
 * other direction: once a deployment calls `configureAuth()`, the exact payload must reach the
 * adapter and the adapter's own result must come back unaltered, so the honest failure is a
 * property of the missing provider and not of this module. */

/** The module keeps its adapter in a module-level variable and exposes no way to unregister
 * one, which is correct for production and unusable across tests in one file -- a
 * `configureAuth()` in test one would still be in force in test five. Resetting the registry
 * gives each test the state this repository actually ships in. */
async function loadAuthService(): Promise<typeof import('../src/services/authService')> {
  vi.resetModules();
  return import('../src/services/authService');
}

/** Every operation, with a payload that is valid for it. Driving the table rather than writing
 * six near-identical tests means a seventh operation added to `AuthOperation` without a row here
 * is a compile error, not a silently untested path. */
const OPERATIONS: {
  readonly [K in AuthOperation]: AuthPayloads[K];
} = {
  signIn: { email: 'jdoe@institution.edu', password: 'correct horse battery' },
  signUp: {
    email: 'jdoe@institution.edu',
    password: 'correct horse battery',
    organisation: 'Orbital Dynamics Lab',
  },
  social: { provider: 'google' },
  resetPassword: { email: 'jdoe@institution.edu' },
  requestPhoneCode: { phone: '+14155550123' },
  verifyPhoneCode: { phone: '+14155550123', code: '123456' },
};

const OPERATION_NAMES = Object.keys(OPERATIONS) as readonly AuthOperation[];

/** Words that would amount to a claim that an account, a session or a message exists. The
 * unconfigured messages talk about sign-in and about sessions constantly -- always to deny them
 * -- so these match the affirmative forms only. */
const SUCCESS_CLAIMS: readonly RegExp[] = [
  /\bsigned in\b/i,
  /\bsucce(ss|eded)/i,
  /\bwelcome\b/i,
  /\byour account\b/i,
  /\bcheck your (email|inbox|phone)\b/i,
];

type Settled<T> =
  | { readonly ok: true; readonly value: T }
  | { readonly ok: false; readonly error: unknown };

/**
 * Runs a service call to completion under fake timers. The unconfigured path waits out a
 * deliberate ~450ms presentation delay before it rejects (so the button's busy state paints),
 * which is six tests' worth of real waiting for no extra coverage. The rejection handler is
 * attached before the clock is advanced, so the promise is never momentarily unhandled.
 */
async function settle<T>(work: Promise<T>): Promise<Settled<T>> {
  const captured: Promise<Settled<T>> = work.then(
    (value): Settled<T> => ({ ok: true, value }),
    (error: unknown): Settled<T> => ({ ok: false, error }),
  );
  await vi.advanceTimersByTimeAsync(2000);
  return captured;
}

beforeEach(() => {
  vi.useFakeTimers();
});

afterEach(() => {
  vi.useRealTimers();
});

describe('authService with no adapter configured (what this repository ships)', () => {
  it('reports itself as not configured', async () => {
    const auth = await loadAuthService();
    expect(auth.isAuthConfigured()).toBe(false);
  });

  it.each(OPERATION_NAMES)('%s rejects with AuthNotConfiguredError', async (operation) => {
    const auth = await loadAuthService();
    const result = await settle(auth.runAuth(operation, OPERATIONS[operation]));

    expect(result.ok).toBe(false);
    if (result.ok) return;
    expect(result.error).toBeInstanceOf(auth.AuthNotConfiguredError);
    // The operation travels on the error, so a caller can tell which of six requests failed
    // without parsing the prose.
    expect((result.error as InstanceType<typeof auth.AuthNotConfiguredError>).operation).toBe(
      operation,
    );
  });

  it.each(OPERATION_NAMES)('%s says nothing happened, and never implies it did', async (
    operation,
  ) => {
    const auth = await loadAuthService();
    const result = await settle(auth.runAuth(operation, OPERATIONS[operation]));

    expect(result.ok).toBe(false);
    if (result.ok) return;
    const message = result.error instanceof Error ? result.error.message : '';

    // The gap is named: not connected, to something specific.
    expect(message).toMatch(/not connected to (an|a) [a-z ]+/);
    // And the consequence is stated, rather than left for the reader to assume.
    expect(message).toMatch(/\bno\b/i);
    for (const claim of SUCCESS_CLAIMS) {
      expect(message).not.toMatch(claim);
    }
  });

  it('gives each operation its own explanation rather than one generic string', async () => {
    const auth = await loadAuthService();
    const messages = await Promise.all(
      OPERATION_NAMES.map(async (operation) => {
        const result = await settle(auth.runAuth(operation, OPERATIONS[operation]));
        return result.ok || !(result.error instanceof Error) ? '' : result.error.message;
      }),
    );
    // resetPassword and signIn share a backend noun but differ in what they say did not happen.
    expect(new Set(messages).size).toBe(OPERATION_NAMES.length);
  });

  it('does not resolve early -- the busy state has time to paint before the failure', async () => {
    const auth = await loadAuthService();
    let settledEarly = false;
    const work = auth
      .runAuth('signIn', OPERATIONS.signIn)
      .catch(() => undefined)
      .finally(() => {
        settledEarly = true;
      });

    await vi.advanceTimersByTimeAsync(100);
    expect(settledEarly).toBe(false);

    await vi.advanceTimersByTimeAsync(2000);
    await work;
    expect(settledEarly).toBe(true);
  });
});

/** Records what reached the adapter, so the routing assertion is about the payload that
 * arrived and not merely about the fact that something was called. */
function makeRecordingAdapter(): {
  readonly adapter: AuthAdapter;
  readonly calls: { readonly operation: AuthOperation; readonly payload: unknown }[];
} {
  const calls: { operation: AuthOperation; payload: unknown }[] = [];

  function handler<K extends AuthOperation>(
    operation: K,
  ): (payload: AuthPayloads[K]) => Promise<AuthResult> {
    return async (payload) => {
      calls.push({ operation, payload });
      return { message: `${operation} handled by the adapter`, redirectTo: '#/workspace' };
    };
  }

  return {
    adapter: {
      signIn: handler('signIn'),
      signUp: handler('signUp'),
      social: handler('social'),
      resetPassword: handler('resetPassword'),
      requestPhoneCode: handler('requestPhoneCode'),
      verifyPhoneCode: handler('verifyPhoneCode'),
    },
    calls,
  };
}

describe('authService with an adapter configured (what a real deployment gets)', () => {
  it('reports itself as configured once an adapter is registered', async () => {
    const auth = await loadAuthService();
    expect(auth.isAuthConfigured()).toBe(false);
    auth.configureAuth(makeRecordingAdapter().adapter);
    expect(auth.isAuthConfigured()).toBe(true);
  });

  it.each(OPERATION_NAMES)('routes %s to the adapter and returns its result', async (operation) => {
    const auth = await loadAuthService();
    const { adapter, calls } = makeRecordingAdapter();
    auth.configureAuth(adapter);

    const result = await settle(auth.runAuth(operation, OPERATIONS[operation]));

    expect(result.ok).toBe(true);
    if (!result.ok) return;
    expect(result.value.message).toBe(`${operation} handled by the adapter`);
    expect(result.value.redirectTo).toBe('#/workspace');
    // Exactly one handler ran, it was the matching one, and the payload arrived unaltered.
    expect(calls).toEqual([{ operation, payload: OPERATIONS[operation] }]);
  });

  it('never produces AuthNotConfiguredError once an adapter is attached', async () => {
    const auth = await loadAuthService();
    auth.configureAuth(makeRecordingAdapter().adapter);
    const result = await settle(auth.runAuth('signIn', OPERATIONS.signIn));
    expect(result.ok).toBe(true);
  });

  it("surfaces the adapter's own rejection rather than rewriting it as a configuration gap", async () => {
    const auth = await loadAuthService();
    const { adapter } = makeRecordingAdapter();
    auth.configureAuth({
      ...adapter,
      signIn: async () => {
        throw new Error('Those credentials were not accepted.');
      },
    });

    const result = await settle(auth.runAuth('signIn', OPERATIONS.signIn));
    expect(result.ok).toBe(false);
    if (result.ok) return;
    expect(result.error).not.toBeInstanceOf(auth.AuthNotConfiguredError);
    expect((result.error as Error).message).toBe('Those credentials were not accepted.');
  });
});
