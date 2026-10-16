/**
 * The landing view's composition.
 *
 * This file owns three things and deliberately nothing else: the order of the bands, the page
 * landmarks (`main`, `footer`), and the entry surface's stylesheet import. Every band's
 * content, layout and behaviour belongs to that band's own module, so the reading order of the
 * page is legible from this file alone.
 *
 * There is no skip link here on purpose. `MarketingNav` renders one as the first element it
 * returns, targeting the hero, and it moves focus as well as scrolling. A second skip link in
 * this file would give the page two of them, which is worse than having none.
 *
 * The stylesheet is imported here rather than from `main.tsx` because the entry surface's CSS
 * is only needed when this view is on screen; the workspace shell must not have to know that
 * `styles/marketing/` exists.
 *
 * Heading hierarchy: exactly one `h1` on this view, in the hero. Every band opens with an
 * `h2` (`SectionHeading` hard-codes that level), and anything below a band heading is an `h3`.
 */

import { useEffect } from 'react';

import '../styles/marketing/index.css';

import { useLandingScrollTarget } from '../routes/RouteContext';
import { AccessSection } from './sections/AccessSection';
import { CapabilitiesSection } from './sections/CapabilitiesSection';
import { HeroSection } from './sections/HeroSection';
import { MarketingNav } from './sections/MarketingNav';
import { MissionPreviewSection } from './sections/MissionPreviewSection';
import { OptimizationSection } from './sections/OptimizationSection';
import { PipelineSection } from './sections/PipelineSection';
import { ProblemSection } from './sections/ProblemSection';
import { ResearchSection } from './sections/ResearchSection';
import { SiteFooter } from './sections/SiteFooter';

const DOCUMENT_TITLE = 'ORCA — Autonomous Orbital Rescue Mission Planning';

/**
 * The domain the rest of the page argues inside, stated once as plain terms. These are
 * descriptions of what the engine implements (see `ALGORITHM_LIST` in `landingContent.ts`),
 * not claims, so no figure appears here and none needs a citation.
 */
const MISSION_KEYWORDS: readonly string[] = [
  'Orbital mechanics',
  'Multi-objective optimisation',
  'Label-setting search',
  'Pareto dominance',
  'Assignment under scarcity',
  'Debris proximity',
  'C++20 engine',
];

/** A hairline-bounded band rather than a section: it carries no heading, so making it a
 * landmark would add an unnamed region to the accessibility tree for no benefit. The list is
 * labelled instead, because a bare list of seven terms announces itself as nothing. */
function MissionStrip() {
  return (
    <div className="mk-mission-strip">
      <div className="mk-shell">
        <ul className="mk-mission-strip__list" aria-label="Problem domain">
          {MISSION_KEYWORDS.map((keyword) => (
            <li key={keyword} className="mk-mission-strip__item">
              {keyword}
            </li>
          ))}
        </ul>
      </div>
    </div>
  );
}

export function OrcaLanding() {
  // Consumes a section request left by `navigate('landing', { section })` from another route.
  // Called here because by the time this component's effects run, every section below is
  // already committed to the DOM and the target id resolves in one lookup.
  useLandingScrollTarget();

  useEffect(() => {
    document.title = DOCUMENT_TITLE;
    // Nothing is restored on unmount: each view sets its own title, so a restore here would
    // only race the incoming view's effect.
  }, []);

  return (
    <div className="mk-page">
      <MarketingNav />

      <main id="mk-main">
        <HeroSection />
        <MissionStrip />
        <ProblemSection />
        <PipelineSection />
        <OptimizationSection />
        <MissionPreviewSection />
        <CapabilitiesSection />
        <ResearchSection />
        <AccessSection />
      </main>

      <SiteFooter />
    </div>
  );
}
