/**
 * A small state indicator. Hidden from the accessibility tree on purpose: the dot never
 * carries information that is not already in the adjacent text, and a screen reader announcing
 * an unlabelled bullet before every status line is noise. Any caller tempted to use the dot as
 * the only carrier of a state should add that state to its own text instead.
 */
export interface StatusDotProps {
  readonly tone?: 'active' | 'warm' | 'critical' | 'neutral';
  readonly pulse?: boolean;
}

export function StatusDot({ tone = 'neutral', pulse = false }: StatusDotProps) {
  const classes = ['mk-status-dot', `mk-status-dot--${tone}`, pulse ? 'mk-status-dot--pulse' : null]
    .filter((entry): entry is string => Boolean(entry))
    .join(' ');

  return <span className={classes} aria-hidden="true" />;
}
