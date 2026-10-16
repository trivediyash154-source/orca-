/**
 * "Continue with Google", in both of the two situations a deployment can actually be in.
 *
 * ## 1. A Google OAuth client ID is configured (`VITE_GOOGLE_CLIENT_ID`)
 *
 * The real thing. Google Identity Services is loaded from `accounts.google.com`, and
 * `initTokenClient(...).requestAccessToken({ prompt: 'select_account' })` opens Google's own
 * account chooser in a popup. The reader picks an account; Google returns an access token
 * without asking for a password, because the browser's existing Google session is what is
 * being presented. The token is then exchanged for the profile at Google's `userinfo`
 * endpoint and discarded -- it is never stored, logged or sent anywhere else.
 *
 * ## 2. No client ID is configured
 *
 * An OAuth client ID is issued per origin by whoever owns the Google Cloud project, so a
 * repository cannot ship one and a preview deployment on a new URL would not be authorised by
 * it anyway. Rather than make the button dead, this module falls back to its own account
 * chooser: the same interaction (pick a remembered account, or add one), rendered locally and
 * labelled, in the dialog itself, as a device-local chooser rather than Google. Nothing claims
 * Google verified anything.
 *
 * `README.md` documents the one-line step that moves a deployment from (2) to (1).
 */

import type { GoogleProfile } from './accountStore';
import { listAccounts } from './accountStore';

const GSI_SRC = 'https://accounts.google.com/gsi/client';
const USERINFO_URL = 'https://www.googleapis.com/oauth2/v3/userinfo';
const SCOPES = 'openid email profile';

/** Minimal shape of the parts of GIS this module calls. The library ships no types and
 * `@types/google.accounts` would be a dependency for three call signatures. */
interface TokenResponse {
  readonly access_token?: string;
  readonly error?: string;
  readonly error_description?: string;
}

interface TokenClient {
  requestAccessToken(overrides?: { readonly prompt?: string }): void;
}

interface GoogleAccountsApi {
  readonly oauth2: {
    initTokenClient(config: {
      readonly client_id: string;
      readonly scope: string;
      readonly callback: (response: TokenResponse) => void;
      readonly error_callback?: (error: { readonly type?: string }) => void;
    }): TokenClient;
  };
}

declare global {
  // eslint-disable-next-line no-var
  var google: { readonly accounts: GoogleAccountsApi } | undefined;
}

export class GoogleAuthError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'GoogleAuthError';
  }
}

export function googleClientId(): string | null {
  const raw = import.meta.env.VITE_GOOGLE_CLIENT_ID;
  if (typeof raw !== 'string') return null;
  const trimmed = raw.trim();
  return trimmed.length > 0 ? trimmed : null;
}

export function isRealGoogleConfigured(): boolean {
  return googleClientId() !== null;
}

let scriptPromise: Promise<void> | null = null;

/** One load per document, shared by every later press. A second `<script>` for the same URL
 * would re-register the library's globals while a flow is in progress. */
function loadGsi(): Promise<void> {
  if (scriptPromise !== null) return scriptPromise;

  scriptPromise = new Promise<void>((resolve, reject) => {
    if (typeof document === 'undefined') {
      reject(new GoogleAuthError('Google sign-in needs a browser document.'));
      return;
    }
    if (globalThis.google?.accounts?.oauth2) {
      resolve();
      return;
    }

    const existing = document.querySelector<HTMLScriptElement>(`script[src="${GSI_SRC}"]`);
    const script = existing ?? document.createElement('script');

    const timer = window.setTimeout(() => {
      // A blocked request to accounts.google.com otherwise leaves the button spinning for as
      // long as the reader is willing to watch it.
      scriptPromise = null;
      reject(
        new GoogleAuthError(
          'Google sign-in did not load. A network block, an extension or an offline connection ' +
            'can all cause this. Create an account with an email address instead.',
        ),
      );
    }, 10_000);

    script.addEventListener('load', () => {
      window.clearTimeout(timer);
      if (globalThis.google?.accounts?.oauth2) resolve();
      else {
        scriptPromise = null;
        reject(new GoogleAuthError('Google sign-in loaded but did not initialise.'));
      }
    });
    script.addEventListener('error', () => {
      window.clearTimeout(timer);
      scriptPromise = null;
      reject(
        new GoogleAuthError(
          'Google sign-in could not be reached. Create an account with an email address instead.',
        ),
      );
    });

    if (existing === null) {
      script.src = GSI_SRC;
      script.async = true;
      script.defer = true;
      document.head.appendChild(script);
    }
  });

  return scriptPromise;
}

async function realGoogleFlow(clientId: string): Promise<GoogleProfile> {
  await loadGsi();
  const api = globalThis.google?.accounts;
  if (!api) throw new GoogleAuthError('Google sign-in is unavailable in this browser.');

  const token = await new Promise<string>((resolve, reject) => {
    const client = api.oauth2.initTokenClient({
      client_id: clientId,
      scope: SCOPES,
      callback: (response) => {
        if (typeof response.access_token === 'string' && response.access_token.length > 0) {
          resolve(response.access_token);
          return;
        }
        reject(
          new GoogleAuthError(
            response.error === 'access_denied'
              ? 'Google sign-in was cancelled, so no account was selected.'
              : (response.error_description ??
                'Google did not return an account. Nothing was signed in.'),
          ),
        );
      },
      error_callback: (error) => {
        reject(
          new GoogleAuthError(
            error.type === 'popup_closed'
              ? 'The Google window was closed before an account was chosen.'
              : error.type === 'popup_failed_to_open'
                ? 'The browser blocked the Google popup. Allow popups for this site and try again.'
                : 'Google sign-in did not complete. Nothing was signed in.',
          ),
        );
      },
    });

    // `select_account` is what produces the chooser every time, rather than silently reusing
    // the single signed-in account -- which is the behaviour being asked for.
    client.requestAccessToken({ prompt: 'select_account' });
  });

  const response = await fetch(USERINFO_URL, {
    headers: { Authorization: `Bearer ${token}` },
  });
  if (!response.ok) {
    throw new GoogleAuthError(
      `Google accepted the sign-in but the profile request failed (HTTP ${response.status}).`,
    );
  }

  const profile = (await response.json()) as {
    readonly email?: string;
    readonly name?: string;
    readonly picture?: string;
    readonly email_verified?: boolean;
  };
  if (typeof profile.email !== 'string' || profile.email.length === 0) {
    throw new GoogleAuthError('Google returned an account with no email address.');
  }

  return { email: profile.email, name: profile.name, picture: profile.picture };
}

/* --------------------------------------------------------------------------------------- *
 * Device-local chooser, for a deployment with no client ID configured.
 * --------------------------------------------------------------------------------------- */

const CHOOSER_STYLE_ID = 'orca-chooser-style';

const CHOOSER_CSS = `
.orca-chooser-backdrop {
  position: fixed; inset: 0; z-index: 9999;
  display: grid; place-items: center; padding: 24px;
  background: color-mix(in srgb, #05070c 72%, transparent);
  backdrop-filter: blur(6px);
}
.orca-chooser {
  width: min(420px, 100%); max-height: min(560px, 90vh); overflow: auto;
  border-radius: 16px; padding: 28px 26px 20px;
  background: var(--orca-chooser-bg, #ffffff); color: var(--orca-chooser-fg, #1f2328);
  box-shadow: 0 24px 64px rgb(0 0 0 / 0.38);
  font: 400 14px/1.5 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
}
.orca-chooser h2 { margin: 14px 0 4px; font-size: 22px; font-weight: 400; }
.orca-chooser p.orca-chooser__sub { margin: 0 0 18px; font-size: 14px; opacity: 0.72; }
.orca-chooser__list { list-style: none; margin: 0 0 8px; padding: 0; }
.orca-chooser__row {
  display: flex; align-items: center; gap: 14px; width: 100%;
  padding: 12px 10px; border: 0; border-radius: 10px;
  background: transparent; color: inherit; text-align: left; cursor: pointer;
  font: inherit;
}
.orca-chooser__row:hover, .orca-chooser__row:focus-visible {
  background: color-mix(in srgb, currentColor 7%, transparent); outline: none;
}
.orca-chooser__avatar {
  flex: 0 0 auto; width: 34px; height: 34px; border-radius: 50%;
  display: grid; place-items: center; font-size: 15px; font-weight: 500; color: #fff;
  background: linear-gradient(135deg, #4285f4, #9b72cb);
}
.orca-chooser__name { font-size: 14px; }
.orca-chooser__email { font-size: 13px; opacity: 0.66; }
.orca-chooser__divider { height: 1px; margin: 10px 0; background: color-mix(in srgb, currentColor 12%, transparent); }
.orca-chooser__field { display: block; margin: 6px 0 14px; }
.orca-chooser__field input {
  width: 100%; box-sizing: border-box; margin-top: 6px; padding: 11px 12px;
  border: 1px solid color-mix(in srgb, currentColor 26%, transparent);
  border-radius: 8px; background: transparent; color: inherit; font: inherit;
}
.orca-chooser__actions { display: flex; justify-content: flex-end; gap: 8px; margin-top: 6px; }
.orca-chooser__btn {
  padding: 9px 18px; border-radius: 999px; border: 0; cursor: pointer;
  font: 500 14px/1 inherit; font-family: inherit;
}
.orca-chooser__btn--ghost { background: transparent; color: #1a73e8; }
.orca-chooser__btn--primary { background: #1a73e8; color: #fff; }
.orca-chooser__note {
  margin: 16px 0 0; padding-top: 14px; font-size: 12px; line-height: 1.45; opacity: 0.6;
  border-top: 1px solid color-mix(in srgb, currentColor 12%, transparent);
}
@media (prefers-color-scheme: dark) {
  .orca-chooser { --orca-chooser-bg: #1f2125; --orca-chooser-fg: #e8eaed; }
}
`;

function ensureChooserStyle(): void {
  if (document.getElementById(CHOOSER_STYLE_ID) !== null) return;
  const style = document.createElement('style');
  style.id = CHOOSER_STYLE_ID;
  style.textContent = CHOOSER_CSS;
  document.head.appendChild(style);
}

const EMAIL_SHAPE = /^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/;

/**
 * The local chooser. Resolves with the chosen profile, rejects if the reader backs out --
 * deliberately the same contract as `realGoogleFlow`, so the adapter above has one code path.
 */
function localChooserFlow(): Promise<GoogleProfile> {
  return new Promise<GoogleProfile>((resolve, reject) => {
    ensureChooserStyle();

    const known = listAccounts();
    const previouslyFocused = document.activeElement as HTMLElement | null;

    const backdrop = document.createElement('div');
    backdrop.className = 'orca-chooser-backdrop';
    backdrop.setAttribute('role', 'dialog');
    backdrop.setAttribute('aria-modal', 'true');
    backdrop.setAttribute('aria-label', 'Choose an account');

    const panel = document.createElement('div');
    panel.className = 'orca-chooser';
    backdrop.appendChild(panel);

    function close(): void {
      document.removeEventListener('keydown', onKeyDown, true);
      backdrop.remove();
      previouslyFocused?.focus?.();
    }

    function onKeyDown(event: KeyboardEvent): void {
      if (event.key !== 'Escape') return;
      event.stopPropagation();
      close();
      reject(new GoogleAuthError('Account selection was cancelled. Nothing was signed in.'));
    }

    function accept(profile: GoogleProfile): void {
      close();
      resolve(profile);
    }

    function renderList(): void {
      panel.replaceChildren();

      const heading = document.createElement('h2');
      heading.textContent = 'Choose an account';
      const sub = document.createElement('p');
      sub.className = 'orca-chooser__sub';
      sub.textContent = 'to continue to ORCA';
      panel.append(heading, sub);

      const list = document.createElement('ul');
      list.className = 'orca-chooser__list';

      for (const account of known) {
        const item = document.createElement('li');
        const row = document.createElement('button');
        row.type = 'button';
        row.className = 'orca-chooser__row';

        const avatar = document.createElement('span');
        avatar.className = 'orca-chooser__avatar';
        avatar.setAttribute('aria-hidden', 'true');
        avatar.textContent = (account.displayName || account.email).charAt(0).toUpperCase();

        const text = document.createElement('span');
        const name = document.createElement('span');
        name.className = 'orca-chooser__name';
        name.textContent = account.displayName || account.email;
        const mail = document.createElement('span');
        mail.className = 'orca-chooser__email';
        mail.textContent = account.email;
        text.append(name, document.createElement('br'), mail);

        row.append(avatar, text);
        row.addEventListener('click', () => {
          accept({ email: account.email, name: account.displayName });
        });
        item.appendChild(row);
        list.appendChild(item);
      }

      const otherItem = document.createElement('li');
      const other = document.createElement('button');
      other.type = 'button';
      other.className = 'orca-chooser__row';
      const plus = document.createElement('span');
      plus.className = 'orca-chooser__avatar';
      plus.setAttribute('aria-hidden', 'true');
      plus.textContent = '+';
      const otherLabel = document.createElement('span');
      otherLabel.className = 'orca-chooser__name';
      otherLabel.textContent = 'Use another account';
      other.append(plus, otherLabel);
      other.addEventListener('click', renderEntry);
      otherItem.appendChild(other);

      if (known.length > 0) {
        const divider = document.createElement('div');
        divider.className = 'orca-chooser__divider';
        list.appendChild(divider);
      }
      list.appendChild(otherItem);
      panel.appendChild(list);

      panel.appendChild(note());
      list.querySelector<HTMLButtonElement>('button')?.focus();
    }

    function renderEntry(): void {
      panel.replaceChildren();

      const heading = document.createElement('h2');
      heading.textContent = 'Sign in';
      const sub = document.createElement('p');
      sub.className = 'orca-chooser__sub';
      sub.textContent = 'Use your account to continue to ORCA';

      const form = document.createElement('form');
      const label = document.createElement('label');
      label.className = 'orca-chooser__field';
      label.textContent = 'Email address';
      const input = document.createElement('input');
      input.type = 'email';
      input.autocomplete = 'email';
      input.required = true;
      input.placeholder = 'you@example.com';
      label.appendChild(input);

      const error = document.createElement('p');
      error.className = 'orca-chooser__sub';
      error.setAttribute('role', 'alert');
      error.style.color = '#d93025';
      error.style.margin = '-8px 0 12px';
      error.hidden = true;

      const actions = document.createElement('div');
      actions.className = 'orca-chooser__actions';
      const back = document.createElement('button');
      back.type = 'button';
      back.className = 'orca-chooser__btn orca-chooser__btn--ghost';
      back.textContent = known.length > 0 ? 'Back' : 'Cancel';
      const next = document.createElement('button');
      next.type = 'submit';
      next.className = 'orca-chooser__btn orca-chooser__btn--primary';
      next.textContent = 'Next';
      actions.append(back, next);

      back.addEventListener('click', () => {
        if (known.length > 0) {
          renderList();
          return;
        }
        close();
        reject(new GoogleAuthError('Account selection was cancelled. Nothing was signed in.'));
      });

      form.addEventListener('submit', (event) => {
        event.preventDefault();
        const value = input.value.trim();
        if (!EMAIL_SHAPE.test(value)) {
          error.textContent = 'Enter a complete email address, including the domain.';
          error.hidden = false;
          input.focus();
          return;
        }
        accept({ email: value });
      });

      form.append(label, error, actions);
      panel.append(heading, sub, form, note());
      input.focus();
    }

    function note(): HTMLParagraphElement {
      const paragraph = document.createElement('p');
      paragraph.className = 'orca-chooser__note';
      paragraph.textContent =
        'Device-local account chooser. This deployment has no Google OAuth client ID ' +
        'configured, so no request was made to Google and nothing was verified by Google. ' +
        'Set VITE_GOOGLE_CLIENT_ID to use the real Google flow.';
      return paragraph;
    }

    backdrop.addEventListener('mousedown', (event) => {
      if (event.target !== backdrop) return;
      close();
      reject(new GoogleAuthError('Account selection was cancelled. Nothing was signed in.'));
    });
    document.addEventListener('keydown', onKeyDown, true);

    document.body.appendChild(backdrop);
    if (known.length > 0) renderList();
    else renderEntry();
  });
}

/** The single entry point the auth adapter calls. */
export function chooseGoogleAccount(): Promise<GoogleProfile> {
  const clientId = googleClientId();
  if (clientId !== null) return realGoogleFlow(clientId);
  return localChooserFlow();
}
