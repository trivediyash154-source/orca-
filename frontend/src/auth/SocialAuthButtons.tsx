import type { ReactNode } from 'react';

import type { AuthProvider } from '../services/authService';

/**
 * Federated and phone entry points, above the email form.
 *
 * The provider marks are drawn here as geometry in `currentColor`. No brand asset is fetched or
 * vendored: a remote logo request would leak the fact that this page was opened to a third party
 * before the reader has chosen anything, and the official multi-colour marks would additionally
 * mean literal hex values in a component, which this repository does not allow. A monochrome
 * letterform is unambiguous next to the words "Continue with Google" and costs nothing.
 *
 * `busyProvider` is a string rather than a union because the phone route is a third busy target
 * that is not an `AuthProvider`.
 */
export interface SocialAuthButtonsProps {
  readonly busyProvider: string | null;
  readonly onProvider: (p: AuthProvider) => void;
  readonly onPhone: () => void;
  readonly disabled: boolean;
}

const BUSY_LABEL: Readonly<Record<string, string>> = {
  google: 'Contacting Google.',
  facebook: 'Contacting Facebook.',
  phone: 'Opening phone verification.',
};

function GoogleMark() {
  return (
    <svg width="18" height="18" viewBox="0 0 20 20" fill="none" aria-hidden="true" focusable="false">
      <path
        d="M16.4 10a6.4 6.4 0 1 1-1.6-4.2"
        stroke="currentColor"
        strokeWidth="1.4"
        strokeLinecap="round"
      />
      <path d="M16.4 10h-4.7" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" />
    </svg>
  );
}

function FacebookMark() {
  return (
    <svg width="18" height="18" viewBox="0 0 20 20" fill="none" aria-hidden="true" focusable="false">
      <rect
        x="2.6"
        y="2.6"
        width="14.8"
        height="14.8"
        rx="3.4"
        stroke="currentColor"
        strokeWidth="1.3"
      />
      <path
        d="M12.5 6.4h-1.2c-.9 0-1.4.5-1.4 1.4V17"
        stroke="currentColor"
        strokeWidth="1.3"
        strokeLinecap="round"
      />
      <path d="M8.3 10.2h4.1" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
    </svg>
  );
}

function PhoneMark() {
  return (
    <svg width="18" height="18" viewBox="0 0 20 20" fill="none" aria-hidden="true" focusable="false">
      <rect x="6" y="2.4" width="8" height="15.2" rx="2.2" stroke="currentColor" strokeWidth="1.3" />
      <path d="M8.8 4.9h2.4" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" />
      <circle cx="10" cy="14.9" r="0.9" fill="currentColor" />
    </svg>
  );
}

interface SocialButtonProps {
  readonly mark: ReactNode;
  readonly label: string;
  readonly busy: boolean;
  readonly disabled: boolean;
  readonly onActivate: () => void;
}

function SocialButton({ mark, label, busy, disabled, onActivate }: SocialButtonProps) {
  return (
    <button
      type="button"
      className="mk-auth-social__button"
      /* The busy control is deliberately NOT disabled: disabling the element that was just
         activated drops focus to the document body, so a keyboard reader loses their place at
         the exact moment the response arrives. It is `aria-busy` and inert by guard instead. */
      disabled={disabled && !busy}
      aria-busy={busy ? true : undefined}
      onClick={() => {
        if (disabled || busy) return;
        onActivate();
      }}
    >
      <span className="mk-auth-social__mark" aria-hidden="true">
        {mark}
      </span>
      <span className="mk-auth-social__label">{label}</span>
      <span className="mk-auth-social__slot" aria-hidden="true">
        {busy ? <span className="mk-auth-spin" /> : null}
      </span>
    </button>
  );
}

export function SocialAuthButtons({
  busyProvider,
  onProvider,
  onPhone,
  disabled,
}: SocialAuthButtonsProps) {
  const anyBusy = busyProvider !== null;

  return (
    <div className="mk-auth-social">
      <SocialButton
        mark={<GoogleMark />}
        label="Continue with Google"
        busy={busyProvider === 'google'}
        disabled={disabled || anyBusy}
        onActivate={() => onProvider('google')}
      />
      <SocialButton
        mark={<FacebookMark />}
        label="Continue with Facebook"
        busy={busyProvider === 'facebook'}
        disabled={disabled || anyBusy}
        onActivate={() => onProvider('facebook')}
      />
      <SocialButton
        mark={<PhoneMark />}
        label="Continue with phone number"
        busy={busyProvider === 'phone'}
        disabled={disabled || anyBusy}
        onActivate={onPhone}
      />

      {/* In-flight announcement. It sits beside the buttons rather than inside the busy one:
          content nested in a button joins its accessible name, so a spinner label there would
          rename the control mid-press. */}
      <p className="mk-auth-vh" role="status">
        {busyProvider === null ? '' : (BUSY_LABEL[busyProvider] ?? 'Working.')}
      </p>

      {/* Hidden from assistive technology on purpose: the rule is a visual separator, and the
          form below announces its own purpose through its legend. A stray "or" read between the
          buttons and the first field would be noise, not structure. */}
      <div className="mk-auth-divider" aria-hidden="true">
        <span>or use an email address</span>
      </div>
    </div>
  );
}
