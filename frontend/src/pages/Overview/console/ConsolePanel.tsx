import type { ReactNode } from 'react';

/**
 * The console's one frame. Every instrument on the operations page sits in one, so the page
 * reads as a single console rather than a wall of unrelated cards.
 *
 * The frame carries a short channel code ("02") and an uppercase name, the way instrument
 * panels are labelled, and chamfered corners rather than the rounded card every component
 * library ships. The chamfer is a `clip-path`, so it costs no extra markup and no images.
 */
export interface ConsolePanelProps {
  readonly code: string;
  readonly title: string;
  readonly subtitle?: string;
  /** Right-aligned header content: a live marker, a count, a link. */
  readonly meta?: ReactNode;
  readonly footnote?: ReactNode;
  readonly className?: string;
  readonly children: ReactNode;
}

export function ConsolePanel({ code, title, subtitle, meta, footnote, className, children }: ConsolePanelProps) {
  return (
    <section className={`cx-panel${className ? ` ${className}` : ''}`} aria-label={title}>
      <header className="cx-panel__head">
        <span className="cx-panel__code" aria-hidden="true">
          {code}
        </span>
        <div className="cx-panel__titles">
          <h3 className="cx-panel__title">{title}</h3>
          {subtitle ? <p className="cx-panel__subtitle">{subtitle}</p> : null}
        </div>
        {meta ? <div className="cx-panel__meta">{meta}</div> : null}
      </header>
      <div className="cx-panel__body">{children}</div>
      {footnote ? <p className="cx-panel__foot">{footnote}</p> : null}
    </section>
  );
}

/** A pulsing "live" marker for a panel header. The pulse stops for reduced-motion readers. */
export function LiveMark({ label = 'Live' }: { readonly label?: string }) {
  return (
    <span className="cx-live">
      <span className="cx-live__dot" aria-hidden="true" />
      {label}
    </span>
  );
}
