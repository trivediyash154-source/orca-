/**
 * The adapter handed to `configureAuth()` at bootstrap. It is the one place that knows both
 * the shape `AuthForm` expects and the device-local account store that backs it.
 *
 * Two of the six operations are genuinely implemented here (email accounts and Google), and
 * four are not. The four that are not fail with a message naming what is missing, because an
 * adapter that returned a cheerful `{ message: 'Check your inbox' }` for a reset email nobody
 * sent would be a lie told by the product surface -- the exact failure `authService.ts`'s
 * header refuses to ship.
 */

import {
  AuthStorageError,
  createAccount,
  signInWithGoogleProfile,
  signInWithPassword,
} from './accountStore';
import { chooseGoogleAccount, isRealGoogleConfigured } from './googleAuth';
import type { AuthAdapter, AuthResult } from '../services/authService';

/** Where a successful sign-in lands. The hash router owns this value; `AuthForm` only passes
 * it to `window.location.hash`. */
const WORKSPACE = '#/workspace';

function describe(error: unknown, fallback: string): never {
  if (error instanceof AuthStorageError || error instanceof Error) {
    throw new Error(error.message.length > 0 ? error.message : fallback);
  }
  throw new Error(fallback);
}

export const orcaAuthAdapter: AuthAdapter = {
  async signIn({ email, password }): Promise<AuthResult> {
    try {
      const user = await signInWithPassword(email, password);
      return {
        message: `Signed in as ${user.email}. Opening the mission workspace.`,
        redirectTo: WORKSPACE,
      };
    } catch (error) {
      return describe(error, 'Sign-in did not complete. No session was created.');
    }
  },

  async signUp({ email, password, organisation }): Promise<AuthResult> {
    try {
      const user = await createAccount({ email, password, organisation });
      return {
        message:
          `Account created for ${user.email} and signed in. The password is stored only as a ` +
          'PBKDF2 hash in this browser.',
        redirectTo: WORKSPACE,
      };
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
      const profile = await chooseGoogleAccount();
      const user = signInWithGoogleProfile(profile);
      return {
        message: isRealGoogleConfigured()
          ? `Signed in as ${user.email} with Google. Opening the mission workspace.`
          : `Continuing as ${user.email} from the device-local chooser (no Google client ID is ` +
            'configured, so Google verified nothing). Opening the mission workspace.',
        redirectTo: WORKSPACE,
      };
    } catch (error) {
      return describe(error, 'Google sign-in did not complete. No session was created.');
    }
  },

  async resetPassword({ email }): Promise<AuthResult> {
    // No mail server exists in this deployment, and sending nothing while saying "check your
    // inbox" is the one outcome this file will not produce.
    throw new Error(
      `No reset email was sent to ${email.trim()}: this build has no mail service. Passwords are ` +
        'held per browser as PBKDF2 hashes with no recovery path, so create a new account, or ' +
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
