/**
 * The ORCA wordmark: an inclined orbit crossing a body, drawn as inline SVG in `currentColor`
 * so it inherits whatever the surrounding surface sets and needs no asset request.
 *
 * When `onClick` is supplied the mark becomes a real `<button>` rather than a clickable span,
 * because a home-navigation control that cannot be reached by Tab is a broken control.
 */
export interface OrcaWordmarkProps {
  readonly withDescriptor?: boolean;
  readonly onClick?: () => void;
  readonly size?: 'sm' | 'md';
}

function OrbitalGlyph({ size }: { readonly size: 'sm' | 'md' }) {
  const px = size === 'sm' ? 22 : 28;
  return (
    <svg
      className="mk-wordmark__glyph"
      width={px}
      height={px}
      viewBox="0 0 32 32"
      fill="none"
      aria-hidden="true"
      focusable="false"
    >
      <circle cx="16" cy="16" r="5.4" stroke="currentColor" strokeWidth="1.3" />
      <ellipse
        cx="16"
        cy="16"
        rx="14"
        ry="6.6"
        stroke="currentColor"
        strokeWidth="1.1"
        opacity="0.7"
        transform="rotate(-28 16 16)"
      />
      <circle cx="27.4" cy="10.6" r="1.9" fill="currentColor" />
    </svg>
  );
}

export function OrcaWordmark({ withDescriptor = false, onClick, size = 'md' }: OrcaWordmarkProps) {
  const classes = ['mk-wordmark', size === 'sm' ? 'mk-wordmark--sm' : null]
    .filter((entry): entry is string => Boolean(entry))
    .join(' ');

  const content = (
    <>
      <OrbitalGlyph size={size} />
      <span className="mk-wordmark__text">
        <span className="mk-wordmark__name">ORCA</span>
        {withDescriptor ? (
          <span className="mk-wordmark__descriptor">Orbital rescue mission planning</span>
        ) : null}
      </span>
    </>
  );

  if (onClick) {
    return (
      <button type="button" className={classes} onClick={onClick} aria-label="ORCA — home">
        {content}
      </button>
    );
  }

  return <span className={classes}>{content}</span>;
}
