/**
 * The adapter handed to `configureAuth()` at bootstrap. It is the one place that knows both
 * the shape `AuthForm` expects and the stores that back it.
 *
 * Where the account service answers (`serverAuth.ts`, i.e. on Vercel), accounts are created,
 * checked and signed into on the server, so they work from any device. Where it does not -- a
 * static host, `vite preview`, or the service failing -- the same operations run against the
 * device-local store, and the confirmation says which happened.
 *
 * Two of the six operations are genuinely implemented here (email accounts and Google), and
 * four are not. The four that are not fail with a message naming what is missing, because an
 * adapter that returned a cheerful `{ message: 'Check your inbox' }` for a reset email nobody
 * sent would be a lie told by the product surface -- the exact failure `authService.ts`'s
 * header refuses to ship.
 */

import {
  AuthStorageError,
  adoptSession,
  createAccount,
  findAccount,
  signInWithGoogleProfile,
  signInWithPassword,
} from './accountStore';
import type { SessionUser } from './accountStore';
import {
  chooseLocalAccount,
  googleClientId,
  profileFromAccessToken,
  requestGoogleAccessToken,
} from './googleAuth';
import {
  ServerAuthError,
  isServiceUnavailable,
  serverConfig,
  serverGoogle,
  serverSignIn,
  serverSignUp,
} from './serverAuth';
import type { AuthAdapter, AuthResult } from '../services/authService';

/** Where a successful sign-in lands. The hash router owns this value; `AuthForm` only passes
 * it to `window.location.hash`. */
const WORKSPACE = '#/workspace';

function describe(error: unknown, fallback: string): never {
  if (error instanceof AuthStorageError || error instanceof ServerAuthError || error instanceof Error) {
    throw new Error(error.message.length > 0 ? error.message : fallback);
  }
  throw new Error(fallback);
}

function landed(message: string): AuthResult {
  return { message, redirectTo: WORKSPACE };
}

/** Whether this deployment has a working account service. */
async function serviceAvailable(): Promise<boolean> {
  return (await serverConfig())?.storage === true;
}

export const orcaAuthAdapter: AuthAdapter = {
  async signIn({ email, password }): Promise<AuthResult> {
    if (await serviceAvailable()) {
      try {
        const user = adoptSession(await serverSignIn(email, password));
        return landed(`Signed in as ${user.email}. Opening the mission workspace.`);
      } catch (error) {
        // An account made before this deployment had a service exists only in the browser that
        // made it. If the password matches it, carry it over so the next sign-in works anywhere.
        if (error instanceof ServerAuthError && error.status === 401) {
          const local = findAccount(email);
          if (local !== null && local.provider === 'password') {
            try {
              const user = await signInWithPassword(email, password);
              try {
                const moved = adoptSession(
                  await serverSignUp({ email, password, organisation: user.organisation || 'ORCA' }),
                );
                return landed(
                  `Signed in as ${moved.email}. This account was saved only in this browser; it is ` +
                    'now on the server and works from any device.',
                );
              } catch {
                return landed(`Signed in as ${user.email} on this device. Opening the mission workspace.`);
              }
            } catch {
              // Wrong password for the local copy too: report the server's answer below.
            }
          }
        }
        if (isServiceUnavailable(error)) {
          try {
            const user = await signInWithPassword(email, password);
            return landed(
              `The account service did not respond, so ${user.email} was signed in from this ` +
                "browser's own copy of the account.",
            );
          } catch {
            // Nothing local either: the service's own message is the useful one.
          }
        }
        return describe(error, 'Sign-in did not complete. No session was created.');
      }
    }

    try {
      const user = await signInWithPassword(email, password);
      return landed(`Signed in as ${user.email}. Opening the mission workspace.`);
    } catch (error) {
      return describe(error, 'Sign-in did not complete. No session was created.');
    }
  },

  async signUp({ email, password, organisation }): Promise<AuthResult> {
    if (await serviceAvailable()) {
      try {
        const user = adoptSession(await serverSignUp({ email, password, organisation }));
        return landed(
          `Account created for ${user.email} and signed in. It works from any device; the ` +
            'password is stored only as a salted scrypt hash.',
        );
      } catch (error) {
        if (!isServiceUnavailable(error)) {
          return describe(error, 'The account was not created. Nothing was stored.');
        }
        // The service is down: fall through and create the account on this device rather than
        // leave the reader with no way in.
      }
    }

    try {
      const user = await createAccount({ email, password, organisation });
      return landed(
        `Account created for ${user.email} and signed in. It is saved in this browser, with the ` +
          'password stored only as a PBKDF2 hash.',
      );
    } catch (error) {
      return describe(error, 'The account was not created. Nothing was stored.');
    }
  },

  async social({ provider }): Promise<AuthResult> {
    if (provider !== 'google') {
      throw new Error(
        'Facebook sign-in is not connected in this build: no Facebook app ID is configured, so ' +
          'no authorisation request was started. Use Google, or an email address.',
      );
    }
    try {
      const config = await serverConfig();
      const clientId = config?.googleClientId ?? googleClientId();

      if (clientId !== null) {
        // Google's own chooser. The service verifies the token when it holds the same client ID
        // (it checks the token was issued for it); otherwise the profile is read directly.
        const token = await requestGoogleAccessToken(clientId);
        let user: SessionUser;
        if (config?.storage === true && config.googleClientId !== null) {
          user = adoptSession(await serverGoogle(token));
        } else {
          user = signInWithGoogleProfile(await profileFromAccessToken(token));
        }
        return landed(`Signed in as ${user.email} with Google. Opening the mission workspace.`);
      }

      const user = signInWithGoogleProfile(await chooseLocalAccount());
      return landed(
        `Continuing as ${user.email} from the device-local chooser (no Google client ID is ` +
          'configured, so Google verified nothing). Opening the mission workspace.',
      );
    } catch (error) {
      return describe(error, 'Google sign-in did not complete. No session was created.');
    }
  },

  async resetPassword({ email }): Promise<AuthResult> {
    // No mail server exists in this deployment, and sending nothing while saying "check your
    // inbox" is the one outcome this file will not produce.
    throw new Error(
      `No reset email was sent to ${email.trim()}: this build has no mail service. Passwords are ` +
        'kept only as one-way hashes, so they cannot be recovered; create a new account, or ' +
        'continue with Google, which needs no password at all.',
    );
  },

  async requestPhoneCode({ phone }): Promise<AuthResult> {
    throw new Error(
      `No code was sent to ${phone}: this build has no SMS provider configured. Use Google or ` +
        'an email address.',
    );
  },

  async verifyPhoneCode(): Promise<AuthResult> {
    throw new Error(
      'There is no code on record to check this against, because no SMS provider is configured. ' +
        'No session was created.',
    );
  },
};
