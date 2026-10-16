/**
 * The opening frame: headline, the 3D scene, and two registers of numbers that must never be
 * confused with one another.
 *
 * ## Why there are two number registers
 *
 * The annotations over the scene are an ILLUSTRATIVE SCENARIO. They are read from
 * `paretoDemo`'s fixture -- the same fixture the optimization section plots -- so that a
 * reader who scrolls on meets consistent values rather than two invented scenarios, and they
 * are labelled as illustrative in visible text next to the composition.
 *
 * The strip below them is the opposite: every figure there is transcribed from this
 * repository's own benchmark documents and carries its citation inline. The two registers are
 * visually distinct on purpose. A landing page that blurs a fixture into a measurement has
 * lied, however good the render looks.
 *
 * `TELEMETRY LINK — OFFLINE` is the same commitment stated once more: this page holds no
 * engine session and no bridge connection, and the composition says so rather than implying a
 * live feed it does not have.
 *
 * ## Why the annotations are HTML
 *
 * They sit over the canvas, not inside it. Text drawn into WebGL is an image of text: it
 * cannot be selected, found, translated, zoomed by the reader's own settings or read by a
 * screen reader. Percentage coordinates keep them tracking the composition as it resizes, and
 * below the two-column breakpoint they stop being annotations and become an ordinary readout
 * grid rather than being hidden.
 */

import type { CSSProperties } from 'react';

import { usePrefersReducedMotion } from '../../hooks/usePrefersReducedMotion';
import { useRoute } from '../../routes/RouteContext';
import { ActionButton, StatusDot, TelemetryReadout } from '../components';
import { HERO } from '../data/landingContent';
import { DEMO_CANDIDATES, paretoFrontier, preferredCandidate } from '../data/paretoDemo';
import { OrbitalHeroScene } from '../scene/OrbitalHeroScene';
import { HERO_SECTION_ID, scrollToSection } from './MarketingNav';

/** The frontier and the highlighted plan are computed, not chosen: `paretoFrontier` runs the
 * exact dominance test over the fixture, and the annotated plan is whichever point the
 * balanced weighting selects from it. Nothing here is hand-picked to look tidy. */
const HERO_FRONTIER = paretoFrontier(DEMO_CANDIDATES);
const HERO_PLAN = preferredCandidate(HERO_FRONTIER, 'balanced');

/** Optional chaining rather than a narrowing branch: `preferredCandidate` returns null for an
 * empty input, and `findIndex` answers -1 for that case without a second guard. */
const HERO_PLAN_INDEX = HERO_FRONTIER.findIndex((candidate) => candidate.id === HERO_PLAN?.id);

const NO_VALUE = '—';

interface HeroAnnotation {
  readonly id: string;
  readonly label: string;
  readonly value: string;
  readonly unit?: string;
  readonly tone?: 'neutral' | 'active' | 'warm' | 'critical';
  /** Which edge of the hero frame the annotation is pinned to. Its leader line always points
   * inward, toward the render, so both columns of annotations read as one instrument. */
  readonly anchor: 'left' | 'right';
  readonly inset: string;
  readonly top: string;
}

/**
 * Tone is used three times and each use encodes something: cyan for the selected path and the
 * recovery term it maximises, the rescue signal for the risk term it trades against. The
 * remaining channels stay neutral, because a composition where everything glows encodes
 * nothing.
 */
const HERO_ANNOTATIONS: readonly HeroAnnotation[] = [
  {
    id: 'target',
    label: 'Target vehicle',
    value: 'DERELICT',
    unit: '800 km',
    anchor: 'left',
    inset: '58%',
    top: '13%',
  },
  {
    id: 'path',
    label: 'Optimal path',
    value: HERO_PLAN?.id ?? NO_VALUE,
    unit: HERO_PLAN ? `${HERO_PLAN_INDEX + 1} of ${HERO_FRONTIER.length}` : NO_VALUE,
    tone: 'active',
    anchor: 'right',
    inset: '2%',
    top: '25%',
  },
  {
    id: 'window',
    label: 'Rendezvous window',
    value: HERO_PLAN ? HERO_PLAN.transferHours.toFixed(1) : NO_VALUE,
    unit: 'h',
    anchor: 'left',
    inset: '62%',
    top: '39%',
  },
  {
    id: 'delta-v',
    label: 'Delta-v',
    value: HERO_PLAN ? HERO_PLAN.deltaVMs.toFixed(0) : NO_VALUE,
    unit: 'm/s',
    anchor: 'right',
    inset: '1%',
    top: '51%',
  },
  {
    id: 'risk',
    label: 'Collision risk',
    value: HERO_PLAN ? HERO_PLAN.riskIndex.toFixed(2) : NO_VALUE,
    unit: 'index',
    tone: 'warm',
    anchor: 'left',
    inset: '57%',
    top: '63%',
  },
  {
    id: 'recovery',
    label: 'Recovery probability',
    value: HERO_PLAN ? HERO_PLAN.recoveryIndex.toFixed(2) : NO_VALUE,
    unit: 'p',
    tone: 'active',
    anchor: 'right',
    inset: '4%',
    top: '75%',
  },
  {
    id: 'link',
    label: 'Telemetry link',
    value: 'OFFLINE',
    unit: 'no engine session',
    anchor: 'left',
    inset: '60%',
    top: '87%',
  },
];

export function HeroSection() {
  const { navigate } = useRoute();
  const reducedMotion = usePrefersReducedMotion();

  return (
    <section id={HERO_SECTION_ID} className="mk-hero" aria-labelledby="mk-hero-title">
      <div className="mk-hero__scene">
        <OrbitalHeroScene variant="hero" />
        <div className="mk-hero__scrim" aria-hidden="true" />
      </div>

      <div className="mk-hero__frame mk-shell">
        <div className="mk-hero__copy">
          <p className="mk-eyebrow">{HERO.eyebrow}</p>
          <h1 id="mk-hero-title" className="mk-hero__title">
            {HERO.headlineLead}{' '}
            <span className="mk-hero__title-emphasis">{HERO.headlineEmphasis}</span>
          </h1>
          <p className="mk-hero__lede">{HERO.lede}</p>
          <div className="mk-hero__actions">
            {/* Both labels name what the control does on this page. The primary one jumps to
                the problem statement; it does not open the workspace, so it does not say it
                does. */}
            <ActionButton
              variant="primary"
              onClick={() => scrollToSection('overview', reducedMotion)}
            >
              Explore the mission
            </ActionButton>
            <ActionButton variant="ghost" onClick={() => navigate('signup')}>
              Request research access
            </ActionButton>
          </div>
        </div>

        <ul className="mk-hero__annotations" aria-label="Illustrative scenario readouts">
          {HERO_ANNOTATIONS.map((annotation) => (
            <li
              key={annotation.id}
              className={`mk-hero-tag mk-hero-tag--${annotation.anchor}`}
              // Position is handed to CSS as custom properties so the same markup can be an
              // absolute annotation on a wide frame and a plain grid cell on a narrow one,
              // with no second code path here.
              style={
                {
                  '--mk-tag-inset': annotation.inset,
                  '--mk-tag-top': annotation.top,
                } as CSSProperties
              }
            >
              <span className="mk-hero-tag__leader" aria-hidden="true" />
              <TelemetryReadout
                label={annotation.label}
                value={annotation.value}
                unit={annotation.unit}
                tone={annotation.tone}
              />
            </li>
          ))}
        </ul>
      </div>

      <div className="mk-hero__figures mk-shell">
        <p className="mk-eyebrow mk-hero__figures-label">Measured, sources cited</p>
        <ul className="mk-hero__figures-list">
          {HERO.chips.map((chip) => (
            <li key={chip.label} className="mk-hero__figure">
              <TelemetryReadout label={chip.label} value={chip.value} unit={chip.unit} />
              <p className="mk-hero__figure-note">{chip.note}</p>
            </li>
          ))}
        </ul>
      </div>

      <div className="mk-hero__bar mk-shell">
        <a
          className="mk-hero__scroll"
          href="#overview"
          onClick={(event) => {
            if (event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
            event.preventDefault();
            scrollToSection('overview', reducedMotion);
          }}
        >
          <span className="mk-hero__scroll-label mk-mono">Scroll</span>
          <span className="mk-hero__scroll-line" aria-hidden="true" />
        </a>

        <p className="mk-hero__disclosure">
          <StatusDot tone="neutral" />
          <span className="mk-technical-tag">Illustrative scenario</span>
          <span className="mk-hero__disclosure-text">
            Values annotating the render are fixture data, not engine output. This page holds no
            engine session.
          </span>
        </p>

        <p className="mk-hero__index mk-mono" aria-hidden="true">
          01 / 08
        </p>
      </div>
    </section>
  );
}
