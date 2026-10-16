/**
 * The single integration point between the sign-in / sign-up UI and an identity provider.
 *
 * This repository ships no authentication backend. There is no session store, no token
 * endpoint and no user table anywhere in `cpp/`, `api/` or `src/` -- the engine owns orbital
 * state and nothing else. So this module does the one honest thing available to it: it
 * validates nothing it cannot validate, transmits nothing, and fails with an explicit
 * `AuthNotConfiguredError` that says so in words the reader can act on.
 *
 * It deliberately does NOT fake a session. A demo that "logs you in" would be a false claim
 * baked into the product surface, and every later reviewer would have to discover for
 * themselves that the green checkmark meant nothing.
 *
 * ## Wiring a real provider
 *
 * A real deployment calls `configureAuth()` once at bootstrap, before the first render, with an
 * adapter backed by its actual identity provider. Everything downstream -- forms, validation,
 * loading states, error surfaces -- is already written against `runAuth()` and needs no change:
 *
 * ```ts
 * // src/main.tsx (a deployment that has an identity provider)
 * import { configureAuth } from './services/authService';
 *
 * configureAuth({
 *   async signIn({ email, password }) {
 *     const res = await fetch('/auth/session', {
 *       method: 'POST',
 *       headers: { 'Content-Type': 'application/json' },
 *       body: JSON.stringify({ email, password }),
 *     });
 *     if (!res.ok) throw new Error('Those credentials were not accepted.');
 *     return { message: 'Signed in.', redirectTo: '#/workspace' };
 *   },
 *   async signUp({ email, password, organisation }) { ... },
 *   async social({ provider }) { ... },
 *   async resetPassword({ email }) { ... },
 *   async requestPhoneCode({ phone }) { ... },
 *   async verifyPhoneCode({ phone, code }) { ... },
 * });
 * ```
 *
 * ## Storage
 *
 * No credential, token, code or session identifier is stored in this module -- not in a module
 * variable, not in `localStorage`, not in a cookie written from here. The adapter owns whatever
 * persistence its provider requires, which keeps the blast radius of a mistake inside the
 * deployment's own code rather than inside the shared UI layer.
 */

export type AuthProvider = 'google' | 'facebook';

export type AuthOperation =
  | 'signIn'
  | 'signUp'
  | 'social'
  | 'resetPassword'
  | 'requestPhoneCode'
  | 'verifyPhoneCode';

export interface AuthPayloads {
  signIn: { readonly email: string; readonly password: string };
  signUp: { readonly email: string; readonly password: string; readonly organisation: string };
  social: { readonly provider: AuthProvider };
  resetPassword: { readonly email: string };
  requestPhoneCode: { readonly phone: string };
  verifyPhoneCode: { readonly phone: string; readonly code: string };
}

export interface AuthResult {
  readonly message: string;
  readonly redirectTo?: string;
}

export interface AuthAdapter {
  signIn(payload: AuthPayloads['signIn']): Promise<AuthResult>;
  signUp(payload: AuthPayloads['signUp']): Promise<AuthResult>;
  social(payload: AuthPayloads['social']): Promise<AuthResult>;
  resetPassword(payload: AuthPayloads['resetPassword']): Promise<AuthResult>;
  requestPhoneCode(payload: AuthPayloads['requestPhoneCode']): Promise<AuthResult>;
  verifyPhoneCode(payload: AuthPayloads['verifyPhoneCode']): Promise<AuthResult>;
}

/** Structurally identical to `AuthAdapter`, but written as a mapped type so `handlers[op]`
 * narrows to the one signature matching the operation's payload. Indexing the interface with a
 * type parameter would instead produce a union of six signatures, which is not callable. */
type AuthHandlers = {
  [K in AuthOperation]: (payload: AuthPayloads[K]) => Promise<AuthResult>;
};

export class AuthNotConfiguredError extends Error {
  readonly operation: AuthOperation;

  constructor(operation: AuthOperation, message: string) {
    super(message);
    this.name = 'AuthNotConfiguredError';
    this.operation = operation;
  }
}

/**
 * Per-operation copy. Each message states three things in order: what the UI did establish,
 * what is missing, and what did NOT happen. The third clause is the important one -- without
 * it a reader is entitled to assume their password reached a server somewhere.
 */
const NOT_CONFIGURED_MESSAGE: Readonly<Record<AuthOperation, string>> = {
  signIn:
    'Validation passed, but sign-in is not connected to an authentication backend. ' +
    'No credentials were transmitted and no session was created.',
  signUp:
    'Validation passed, but account creation is not connected to an authentication backend. ' +
    'No account was created and nothing you entered was transmitted or stored.',
  social:
    'Federated sign-in is not connected to an identity provider in this build. ' +
    'No authorisation request was started and no profile data was requested.',
  resetPassword:
    'Password reset is not connected to an authentication backend. ' +
    'No reset email was sent and no account was looked up.',
  requestPhoneCode:
    'Phone verification is not connected to a messaging provider. ' +
    'No code was generated and no message was sent to that number.',
  verifyPhoneCode:
    'Phone verification is not connected to a messaging provider. ' +
    'There is no code on record to check this against, and no session was created.',
};

/**
 * ~450ms before the honest failure. Not padding: without it the request resolves inside a
 * single frame, the button's busy state never paints, and the reader sees an error appear with
 * no evidence that anything was attempted. The delay makes the attempt legible.
 */
const PRESENTATION_DELAY_MS = 450;

/** The registry starts empty. That is the accurate description of this repository. */
let registeredAdapter: AuthAdapter | null = null;

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => {
    setTimeout(resolve, ms);
  });
}

export function configureAuth(adapter: AuthAdapter): void {
  registeredAdapter = adapter;
}

export function isAuthConfigured(): boolean {
  return registeredAdapter !== null;
}

export async function runAuth<K extends AuthOperation>(
  operation: K,
  payload: AuthPayloads[K],
): Promise<AuthResult> {
  const adapter = registeredAdapter;
  if (adapter === null) {
    await delay(PRESENTATION_DELAY_MS);
    throw new AuthNotConfiguredError(operation, NOT_CONFIGURED_MESSAGE[operation]);
  }

  const handlers: AuthHandlers = adapter;
  return handlers[operation](payload);
}
