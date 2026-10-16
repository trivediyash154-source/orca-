/**
 * Site footer for the landing view.
 *
 * Three decisions worth stating:
 *
 * 1. In-page section links are anchors; route changes are buttons. A section is a location, so
 *    it keeps a real `href` for its semantics, its status-bar preview and modifier-clicks.
 *    Sign in, request access and the workspace are actions, so they are buttons.
 * 2. Those anchors jump through `scrollToSection()` from `MarketingNav` rather than through the
 *    browser's default. The nav owns that helper for a reason: it moves focus into the target
 *    as well as scrolling it into view, and it leaves the hash alone, which `RouteContext`
 *    treats as the source of truth for which view is mounted. A second implementation here
 *    would be a second chance for the footer and the nav to behave differently.
 * 3. `new Date()` appears exactly once on this surface, here. A rendered copyright year is the
 *    one legitimate use of wall-clock time on a page that otherwise displays nothing it did
 *    not read from a document.
 */

import type { MouseEvent as ReactMouseEvent } from 'react';

import { usePrefersReducedMotion } from '../../hooks/usePrefersReducedMotion';
import { useRoute } from '../../routes/RouteContext';
import { OrcaWordmark } from '../components';
import { ARCHITECTURE_LEVELS } from '../data/landingContent';
import { scrollToSection } from './MarketingNav';

const PRODUCT_NAME =
  'Autonomous Orbital Rescue Mission Planning Using Multi-Objective Optimization';

interface SectionLink {
  readonly id: string;
  readonly label: string;
}

/** Ids match the section ids the nav and the scroll targets use; renaming one here without
 * renaming the section silently produces a link that scrolls nowhere. */
const SECTION_LINKS: readonly SectionLink[] = [
  { id: 'overview', label: 'The problem' },
  { id: 'approach', label: 'Method' },
  { id: 'optimization', label: 'Optimisation' },
  { id: 'workspace-preview', label: 'Workspace' },
  { id: 'capabilities', label: 'Capabilities' },
  { id: 'research', label: 'Evaluation' },
  { id: 'access', label: 'Access' },
];

/** Drawn rather than typed, for the same reason as the action arrow: a glyph character
 * renders at a different weight and baseline on every platform. */
function UpArrowGlyph() {
  return (
    <svg
      className="mk-footer__top-arrow"
      width="10"
      height="14"
      viewBox="0 0 10 14"
      fill="none"
      aria-hidden="true"
      focusable="false"
    >
      <path
        d="M5 13V1M1.5 4.5 5 1l3.5 3.5"
        stroke="currentColor"
        strokeWidth="1.2"
        strokeLinecap="square"
      />
    </svg>
  );
}

export function SiteFooter() {
  const { navigate } = useRoute();
  const reducedMotion = usePrefersReducedMotion();

  // Computed at render: the footer is the one place on this surface where the current date is
  // the correct source for a displayed value.
  const year = new Date().getFullYear();

  function handleSectionClick(event: ReactMouseEvent<HTMLAnchorElement>, id: string): void {
    // Modifier clicks are left to the browser, matching the nav: a reader deliberately opening
    // a link in a new tab should get one, even though the destination is the same document.
    if (event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
    event.preventDefault();
    scrollToSection(id, reducedMotion);
  }

  function handleBackToTop(): void {
    if (typeof window === 'undefined' || typeof window.scrollTo !== 'function') return;
    try {
      window.scrollTo({ top: 0, left: 0, behavior: reducedMotion ? 'auto' : 'smooth' });
    } catch {
      // jsdom reports `scrollTo` as not implemented; a failed scroll is cosmetic, never fatal.
    }
  }

  return (
    <footer className="mk-footer">
      <div className="mk-shell">
        <div className="mk-footer__top">
          <div className="mk-footer__brand">
            <OrcaWordmark withDescriptor onClick={() => navigate('landing')} />
            <p className="mk-footer__product">{PRODUCT_NAME}</p>
            <p className="mk-footer__brand-note">
              A research demonstrator: a C++20 optimisation engine, a Node bridge that transports
              its state, and this interface, which displays what the engine decided.
            </p>
          </div>

          {/* Labelled by its own visible heading rather than by an `aria-label`, so the
              landmark and the heading do not announce two different names for one column. */}
          <nav className="mk-footer-column" aria-labelledby="mk-footer-sections">
            <h2 className="mk-footer-column__title mk-mono" id="mk-footer-sections">
              On this page
            </h2>
            <ul className="mk-footer-column__list">
              {SECTION_LINKS.map((link) => (
                <li key={link.id}>
                  <a
                    className="mk-footer-link"
                    href={`#${link.id}`}
                    onClick={(event) => handleSectionClick(event, link.id)}
                  >
                    {link.label}
                  </a>
                </li>
              ))}
            </ul>
          </nav>

          <nav className="mk-footer-column" aria-labelledby="mk-footer-access">
            <h2 className="mk-footer-column__title mk-mono" id="mk-footer-access">
              Access
            </h2>
            <ul className="mk-footer-column__list">
              <li>
                <button
                  type="button"
                  className="mk-footer-link"
                  onClick={() => navigate('signin')}
                >
                  Sign in
                </button>
              </li>
              <li>
                <button
                  type="button"
                  className="mk-footer-link"
                  onClick={() => navigate('signup')}
                >
                  Request research access
                </button>
              </li>
              <li>
                <button
                  type="button"
                  className="mk-footer-link"
                  onClick={() => navigate('workspace')}
                >
                  Open the workspace
                </button>
              </li>
            </ul>
          </nav>
        </div>

        <section className="mk-footer-architecture" aria-labelledby="mk-footer-architecture">
          <h2 className="mk-footer-column__title mk-mono" id="mk-footer-architecture">
            Architecture
          </h2>
          <ul className="mk-footer-architecture__list">
            {ARCHITECTURE_LEVELS.map((level) => (
              <li key={level.level} className="mk-footer-architecture__item">
                <span className="mk-footer-architecture__level mk-mono">{level.level}</span>
                <span className="mk-footer-architecture__name">{level.name}</span>
                <span className="mk-footer-architecture__stack mk-mono">{level.stack}</span>
                <span className="mk-footer-architecture__role">{level.role}</span>
              </li>
            ))}
          </ul>
        </section>

        <div className="mk-footer__bottom">
          <p className="mk-footer__legal mk-mono">
            © {year} ORCA · Research demonstrator · Not a mission-planning authority
          </p>
          <button type="button" className="mk-footer__top-link mk-mono" onClick={handleBackToTop}>
            <UpArrowGlyph />
            Back to top
          </button>
        </div>
      </div>
    </footer>
  );
}
