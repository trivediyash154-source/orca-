import type { ReactNode } from 'react';

/**
 * The heading block every landing section opens with. Centralised so that the numbered gutter,
 * the eyebrow and the `h2` keep one consistent structure — a page where each section invents
 * its own heading layout stops reading as a single instrument.
 *
 * Always an `h2`: the landing page has exactly one `h1` (the hero), and a section that needed a
 * different level would be nested rather than reconfigured here.
 */
export interface SectionHeadingProps {
  readonly index: string;
  readonly label: string;
  readonly title: ReactNode;
  readonly lede?: ReactNode;
  readonly id?: string;
}

export function SectionHeading({ index, label, title, lede, id }: SectionHeadingProps) {
  return (
    <header className="mk-section-heading">
      {/* The index is a visual figure number that duplicates the label's position in the
          document; announcing "zero two" before every heading adds nothing for a screen
          reader, so it is hidden from the accessibility tree rather than read twice. */}
      <span className="mk-section-index mk-mono" aria-hidden="true">
        {index}
      </span>
      <p className="mk-eyebrow">{label}</p>
      <h2 className="mk-section-title" id={id}>
        {title}
      </h2>
      {lede ? <p className="mk-section-lede">{lede}</p> : null}
    </header>
  );
}
