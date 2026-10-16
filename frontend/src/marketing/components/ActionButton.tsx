import type { ReactNode } from 'react';

/**
 * The entry surface's only control primitive. Always a real `<button>` — never a styled anchor
 * or a `div` with a click handler — so keyboard activation, the disabled state and the busy
 * state come from the platform rather than from re-implementations of it.
 *
 * `busy` is not merely a spinner: it also disables the control and sets `aria-busy`, because a
 * request that is already in flight must not be submittable a second time and a screen-reader
 * user needs the same "working" signal a sighted user gets from the spinner.
 */
export interface ActionButtonProps {
  readonly children: ReactNode;
  readonly onClick?: () => void;
  readonly variant?: 'primary' | 'ghost' | 'quiet';
  readonly type?: 'button' | 'submit';
  readonly disabled?: boolean;
  readonly busy?: boolean;
  readonly fullWidth?: boolean;
  readonly ariaLabel?: string;
}

/** Drawn rather than typed: a glyph font would inherit the reader's font substitution and an
 * arrow character renders at a different weight on every platform. */
function ArrowGlyph() {
  return (
    <svg
      className="mk-action__arrow"
      width="14"
      height="10"
      viewBox="0 0 14 10"
      fill="none"
      aria-hidden="true"
      focusable="false"
    >
      <path
        d="M1 5h11M8.5 1.5 12 5l-3.5 3.5"
        stroke="currentColor"
        strokeWidth="1.2"
        strokeLinecap="square"
      />
    </svg>
  );
}

export function ActionButton({
  children,
  onClick,
  variant = 'ghost',
  type = 'button',
  disabled = false,
  busy = false,
  fullWidth = false,
  ariaLabel,
}: ActionButtonProps) {
  const classes = ['mk-action', `mk-action--${variant}`, fullWidth ? 'mk-action--full' : null]
    .filter((entry): entry is string => Boolean(entry))
    .join(' ');

  return (
    <button
      type={type}
      className={classes}
      onClick={onClick}
      disabled={disabled || busy}
      aria-busy={busy ? true : undefined}
      aria-label={ariaLabel}
    >
      <span className="mk-action__label">{children}</span>
      {busy ? <span className="mk-action__spinner" aria-hidden="true" /> : <ArrowGlyph />}
    </button>
  );
}
