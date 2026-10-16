import type { CSSProperties, ReactNode } from 'react';

import { useScrollReveal } from '../../hooks/useScrollReveal';

/**
 * Wraps content in the one-shot scroll reveal. `as` exists because the wrapper must not
 * flatten document structure: a landmark that needs to be a `<section>` cannot become a
 * `<div>` just to gain an animation.
 *
 * `delayStep` is an ordinal, not a duration. Callers pass 0, 1, 2 for successive siblings and
 * the step size stays here, so a cascade cannot drift out of sync across sections.
 */
export interface RevealProps {
  readonly children: ReactNode;
  readonly className?: string;
  readonly as?: 'div' | 'section' | 'article';
  readonly delayStep?: number;
}

/** 70ms reads as a cascade; beyond roughly 100ms the last item feels late rather than ordered. */
const REVEAL_STEP_MS = 70;

export function Reveal({ children, className, as = 'div', delayStep = 0 }: RevealProps) {
  const { ref, revealed } = useScrollReveal<HTMLDivElement>();

  const classes = ['mk-reveal', revealed ? 'is-revealed' : null, className]
    .filter((entry): entry is string => Boolean(entry))
    .join(' ');

  // Delay is handed to CSS as a custom property so the transition stays entirely declarative;
  // the reduced-motion block in base.css can then zero it without this component knowing.
  const style =
    delayStep > 0
      ? ({ '--mk-reveal-delay': `${delayStep * REVEAL_STEP_MS}ms` } as CSSProperties)
      : undefined;

  if (as === 'section') {
    return (
      <section ref={ref} className={classes} style={style}>
        {children}
      </section>
    );
  }

  if (as === 'article') {
    return (
      <article ref={ref} className={classes} style={style}>
        {children}
      </article>
    );
  }

  return (
    <div ref={ref} className={classes} style={style}>
      {children}
    </div>
  );
}
