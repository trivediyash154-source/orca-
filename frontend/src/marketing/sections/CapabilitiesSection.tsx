/**
 * Section 05 — the four capability claims, as a ledger rather than a card grid.
 *
 * Four equal cards would say the four claims are interchangeable and equally weighted; they
 * are not, and a hairline-divided ledger reads top to bottom in the order they were argued.
 * The left gutter carries a continuous rule with one orbital glyph per entry, its node
 * advancing a quarter turn each row, so the four marks resolve into a single orbit descending
 * the page instead of four repeated ornaments.
 */

import { Reveal, SectionHeading } from '../components';
import { CAPABILITIES } from '../data/landingContent';

const TITLE_ID = 'capabilities-title';

/** A, B, C, D. Letters rather than digits so the ledger does not collide visually with the
 * numbered section index in the same left margin. */
const ENTRY_LETTERS = 'ABCDEFGH';

const GLYPH_CENTRE = 22;
const GLYPH_RX = 17;
const GLYPH_RY = 7.5;
/** Degrees advanced per entry. A quarter turn is the largest step that still reads as the same
 * body continuing round rather than a new one. */
const GLYPH_STEP_DEG = 90;
const GLYPH_START_DEG = 202;
/** The orbit ellipse is tilted so the glyph reads as a viewed orbit rather than a flat ring. */
const GLYPH_TILT_DEG = -24;

interface OrbitGlyphProps {
  readonly index: number;
}

/**
 * The node is positioned on the untilted ellipse and then carried by the same rotation applied
 * to the ellipse, so it stays exactly on the path for any phase — computing a point on an
 * already-rotated ellipse would need the rotation folded into the parametrisation and drifts
 * off the curve the moment either constant changes.
 */
function OrbitGlyph({ index }: OrbitGlyphProps) {
  const phase = ((GLYPH_START_DEG + index * GLYPH_STEP_DEG) * Math.PI) / 180;
  const nodeX = GLYPH_CENTRE + GLYPH_RX * Math.cos(phase);
  const nodeY = GLYPH_CENTRE + GLYPH_RY * Math.sin(phase);

  return (
    <svg
      className="mk-ledger__glyph"
      viewBox="0 0 44 44"
      width="44"
      height="44"
      aria-hidden="true"
      focusable="false"
    >
      <g transform={`rotate(${GLYPH_TILT_DEG} ${GLYPH_CENTRE} ${GLYPH_CENTRE})`}>
        <ellipse
          className="mk-ledger__glyph-orbit"
          cx={GLYPH_CENTRE}
          cy={GLYPH_CENTRE}
          rx={GLYPH_RX}
          ry={GLYPH_RY}
        />
        {/* The stop index is the entry's ordinal, so the four glyphs step along the same
            spectral ramp the frontier uses instead of repeating one accent four times. */}
        <circle
          className={`mk-ledger__glyph-node mk-ledger__glyph-node--${index % 4}`}
          cx={nodeX}
          cy={nodeY}
          r={2.6}
        />
      </g>
      <circle
        className="mk-ledger__glyph-body"
        cx={GLYPH_CENTRE}
        cy={GLYPH_CENTRE}
        r={3.4}
      />
    </svg>
  );
}

export function CapabilitiesSection() {
  return (
    <section id="capabilities" className="mk-section" aria-labelledby={TITLE_ID}>
      <div className="mk-shell">
        <Reveal>
          <SectionHeading
            index="05"
            label="Capabilities"
            title="Four claims, each with something behind it"
            lede={
              'Each entry below names a design decision, the algorithm that implements it, and ' +
              'the measurement or baseline that makes the claim checkable rather than asserted.'
            }
            id={TITLE_ID}
          />
        </Reveal>

        {/* An ordered list, because the four claims build on each other in this order. `Reveal`
            cannot render an `<li>`, so it wraps the article inside one rather than replacing
            it -- an `<ol>` whose children are not list items is not a list to a screen reader. */}
        <ol className="mk-ledger">
          {CAPABILITIES.map((capability, index) => (
            <li className="mk-ledger__item" key={capability.key}>
              <Reveal as="article" className="mk-ledger__row" delayStep={index}>
                <div className="mk-ledger__gutter">
                  <span className="mk-ledger__letter mk-mono" aria-hidden="true">
                    {ENTRY_LETTERS.charAt(index) || String(index + 1)}
                  </span>
                  <OrbitGlyph index={index} />
                </div>

                <div className="mk-ledger__body">
                  <h3 className="mk-ledger__title">{capability.title}</h3>
                  <p className="mk-ledger__text">{capability.body}</p>
                  <ul className="mk-ledger__tags">
                    {capability.tags.map((tag) => (
                      <li key={tag}>
                        <span className="mk-technical-tag">{tag}</span>
                      </li>
                    ))}
                  </ul>
                  {/* Only the two entries that quote a measurement carry one, in the same
                      `Source · path` form the research panels use, so a reader learns the
                      convention once and applies it across the page. */}
                  {capability.source === undefined ? null : (
                    <p className="mk-ledger__source mk-mono">Source · {capability.source}</p>
                  )}
                </div>
              </Reveal>
            </li>
          ))}
        </ol>
      </div>
    </section>
  );
}
