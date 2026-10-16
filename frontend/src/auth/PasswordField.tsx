import { useState } from 'react';
import type { RefObject } from 'react';

/**
 * A password input with a visibility toggle, an error slot and an optional hint slot.
 *
 * The toggle is a real `<button type="button">`: inside a form, a button without an explicit
 * type is a submit button, so an unqualified toggle would post the form on every reveal. It
 * carries `aria-pressed` because it is a two-state control, and its `aria-label` changes with
 * that state so the name always describes what pressing it will do next.
 *
 * The hint is rendered before the reader can fail rather than as a consolation after: a length
 * requirement discovered only by violating it is a requirement the form withheld.
 */
export interface PasswordFieldProps {
  readonly id: string;
  readonly label: string;
  readonly value: string;
  readonly onChange: (next: string) => void;
  readonly autoComplete: 'current-password' | 'new-password';
  readonly error?: string;
  readonly hint?: string;
  readonly inputRef?: RefObject<HTMLInputElement>;
}

function EyeGlyph({ crossed }: { readonly crossed: boolean }) {
  return (
    <svg width="16" height="16" viewBox="0 0 16 16" fill="none" aria-hidden="true" focusable="false">
      <path
        d="M1.4 8S3.9 3.6 8 3.6 14.6 8 14.6 8 12.1 12.4 8 12.4 1.4 8 1.4 8Z"
        stroke="currentColor"
        strokeWidth="1.1"
        strokeLinejoin="round"
      />
      <circle cx="8" cy="8" r="2.1" stroke="currentColor" strokeWidth="1.1" />
      {crossed ? (
        <path
          d="M2.6 13.4 13.4 2.6"
          stroke="currentColor"
          strokeWidth="1.1"
          strokeLinecap="square"
        />
      ) : null}
    </svg>
  );
}

export function PasswordField({
  id,
  label,
  value,
  onChange,
  autoComplete,
  error,
  hint,
  inputRef,
}: PasswordFieldProps) {
  const [visible, setVisible] = useState(false);

  const errorId = `${id}-error`;
  const hintId = `${id}-hint`;
  const describedBy = [hint ? hintId : null, error ? errorId : null]
    .filter((entry): entry is string => entry !== null)
    .join(' ');

  return (
    <div className="mk-auth-field">
      <label className="mk-auth-field__label" htmlFor={id}>
        {label}
      </label>

      <div className="mk-auth-password">
        <input
          id={id}
          ref={inputRef}
          className="mk-auth-field__input mk-auth-password__input"
          type={visible ? 'text' : 'password'}
          value={value}
          onChange={(event) => onChange(event.currentTarget.value)}
          autoComplete={autoComplete}
          autoCapitalize="none"
          autoCorrect="off"
          spellCheck={false}
          aria-invalid={error ? true : undefined}
          aria-describedby={describedBy.length > 0 ? describedBy : undefined}
        />
        <button
          type="button"
          className="mk-auth-password__toggle"
          onClick={() => setVisible((current) => !current)}
          aria-pressed={visible}
          aria-controls={id}
          aria-label={visible ? 'Hide password' : 'Show password'}
        >
          <EyeGlyph crossed={visible} />
        </button>
      </div>

      {hint ? (
        <p className="mk-auth-field__hint" id={hintId}>
          {hint}
        </p>
      ) : null}

      {error ? (
        <p className="mk-auth-field__error" id={errorId}>
          {error}
        </p>
      ) : null}
    </div>
  );
}
