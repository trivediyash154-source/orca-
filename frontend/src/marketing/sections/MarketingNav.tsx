/**
 * The landing page's persistent navigation.
 *
 * Four decisions worth stating, because each of them is a place a conventional implementation
 * would have been wrong here:
 *
 * 1. In-page links are real anchors, but their default activation is always prevented and the
 *    hash is never written. `RouteContext` treats the hash as the source of truth for which
 *    view is mounted, so letting `#overview` reach the URL would push an unrecognised route
 *    through the router on every section jump. The element stays an anchor for its semantics,
 *    its status-bar preview and modifier-clicks; the scroll itself happens here.
 * 2. The current section is observed, not derived from a scroll offset. A thin band across the
 *    middle of the viewport decides: whichever section covers it is current. The landing
 *    sections are contiguous and never overlap, so exactly one can qualify, and while the hero
 *    covers the band nothing is marked current -- which is the truthful answer at the top of
 *    the page.
 * 3. The compact menu is a disclosure, not a modal. Nothing is trapped, the page behind it
 *    stays reachable by Tab, and Escape returns focus to the control that opened it.
 * 4. Section ids are looked up lazily. Sections are rendered by sibling components; an id that
 *    is not on the page is skipped rather than treated as an error, so the nav degrades to the
 *    links that resolve instead of failing whole.
 */

import { useCallback, useEffect, useRef, useState } from 'react';
import type { MouseEvent as ReactMouseEvent } from 'react';

import { usePrefersReducedMotion } from '../../hooks/usePrefersReducedMotion';
import { useRoute } from '../../routes/RouteContext';
import { ActionButton, OrcaWordmark } from '../components';

/** The hero's own section id. Lives here rather than in `HeroSection` because the nav is what
 * navigates: the skip link targets it, and importing it the other way would make the two
 * modules circular. */
export const HERO_SECTION_ID = 'top';

const MENU_ID = 'mk-nav-menu';

interface NavLink {
  readonly id: string;
  readonly label: string;
}

/** Exactly the section ids in the shared contract, in document order. The order is load-bearing
 * twice over: it is the reading order of the page, and it resolves which link is current when
 * the observer reports more than one candidate. */
const NAV_LINKS: readonly NavLink[] = [
  { id: 'overview', label: 'Overview' },
  { id: 'approach', label: 'Approach' },
  { id: 'optimization', label: 'Optimization' },
  { id: 'workspace-preview', label: 'Workspace' },
  { id: 'capabilities', label: 'Capabilities' },
  { id: 'research', label: 'Research' },
  { id: 'access', label: 'Access' },
];

/** A band from 42% to 50% of the viewport height. Narrow enough that only one section can
 * occupy it, low enough that the sticky bar never covers the section being reported. */
const ACTIVE_BAND: IntersectionObserverInit = {
  rootMargin: '-42% 0px -50% 0px',
  threshold: 0,
};

/** The width at which the full link rail fits beside the wordmark and both actions. Below it,
 * every link moves into the disclosure panel -- links are never dropped to save space. */
const DESKTOP_QUERY = '(min-width: 1200px)';

/**
 * Scrolls to a landing section and moves focus into it.
 *
 * Focus movement is the part that is easy to omit and the part that matters: without it a
 * keyboard or screen-reader user activates "Optimization", the page scrolls, and their next
 * Tab continues from the nav link they just left rather than from the section they asked for.
 * `tabindex="-1"` is added only if the section does not already declare one, and it is left in
 * place -- removing it on blur would break a second visit to the same section.
 *
 * Exported because the hero's primary action performs the same jump, and two copies of this
 * would be two chances for the two to diverge.
 */
export function scrollToSection(id: string, reducedMotion: boolean): void {
  if (typeof document === 'undefined') return;

  const element = document.getElementById(id);
  if (!element) return;

  if (typeof element.scrollIntoView === 'function') {
    try {
      element.scrollIntoView({ behavior: reducedMotion ? 'auto' : 'smooth', block: 'start' });
    } catch {
      // jsdom reports scrollIntoView as not implemented; the focus move below still runs.
    }
  }

  if (!element.hasAttribute('tabindex')) {
    element.setAttribute('tabindex', '-1');
  }
  if (typeof element.focus === 'function') {
    element.focus({ preventScroll: true });
  }
}

export function MarketingNav() {
  const { navigate } = useRoute();
  const reducedMotion = usePrefersReducedMotion();
  const [menuOpen, setMenuOpen] = useState(false);
  const [activeId, setActiveId] = useState<string | null>(null);
  const toggleRef = useRef<HTMLButtonElement>(null);

  useEffect(() => {
    if (typeof document === 'undefined' || typeof IntersectionObserver === 'undefined') {
      return undefined;
    }

    // The observer reports transitions, not state, so the set of sections currently crossing
    // the band is accumulated here and the winner recomputed from it on every callback.
    const crossing = new Set<string>();

    const observer = new IntersectionObserver((entries) => {
      for (const entry of entries) {
        if (entry.isIntersecting) {
          crossing.add(entry.target.id);
        } else {
          crossing.delete(entry.target.id);
        }
      }
      setActiveId(NAV_LINKS.find((link) => crossing.has(link.id))?.id ?? null);
    }, ACTIVE_BAND);

    for (const link of NAV_LINKS) {
      const element = document.getElementById(link.id);
      if (element) observer.observe(element);
    }

    return () => observer.disconnect();
  }, []);

  useEffect(() => {
    if (!menuOpen || typeof document === 'undefined') return undefined;

    function handleKeyDown(event: KeyboardEvent): void {
      if (event.key !== 'Escape') return;
      setMenuOpen(false);
      toggleRef.current?.focus();
    }

    document.addEventListener('keydown', handleKeyDown);
    return () => document.removeEventListener('keydown', handleKeyDown);
  }, [menuOpen]);

  // A viewport widened past the desktop breakpoint hides the panel in CSS, which would leave
  // `aria-expanded="true"` describing something no longer on screen.
  useEffect(() => {
    if (typeof window === 'undefined' || typeof window.matchMedia !== 'function') return undefined;

    let query: MediaQueryList;
    try {
      query = window.matchMedia(DESKTOP_QUERY);
    } catch {
      return undefined;
    }
    if (typeof query.addEventListener !== 'function') return undefined;

    function handleChange(event: MediaQueryListEvent): void {
      if (event.matches) setMenuOpen(false);
    }

    query.addEventListener('change', handleChange);
    return () => query.removeEventListener('change', handleChange);
  }, []);

  const handleSectionClick = useCallback(
    (event: ReactMouseEvent<HTMLAnchorElement>, id: string) => {
      // Modifier clicks are left to the browser: a reader deliberately opening a link in a new
      // tab should get one, even though the destination is the same document.
      if (event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
      event.preventDefault();
      setMenuOpen(false);
      scrollToSection(id, reducedMotion);
    },
    [reducedMotion],
  );

  const handleRouteAction = useCallback(
    (route: 'signin' | 'signup') => {
      setMenuOpen(false);
      navigate(route);
    },
    [navigate],
  );

  function renderLink(link: NavLink, className: string) {
    const current = link.id === activeId;
    return (
      <li key={link.id}>
        <a
          className={current ? `${className} is-current` : className}
          href={`#${link.id}`}
          // "location" rather than "page": these are positions within one document, which is
          // the distinction the token exists to draw.
          aria-current={current ? 'location' : undefined}
          onClick={(event) => handleSectionClick(event, link.id)}
        >
          {link.label}
        </a>
      </li>
    );
  }

  return (
    <>
      <a
        className="mk-skip-link"
        href={`#${HERO_SECTION_ID}`}
        onClick={(event) => handleSectionClick(event, HERO_SECTION_ID)}
      >
        Skip to content
      </a>

      <header className="mk-nav">
        <div className="mk-nav__inner mk-shell">
          <OrcaWordmark size="sm" onClick={() => navigate('landing')} />

          <nav className="mk-nav__rail" aria-label="Landing sections">
            <ul className="mk-nav__list">
              {NAV_LINKS.map((link) => renderLink(link, 'mk-nav__link'))}
            </ul>
          </nav>

          <div className="mk-nav__actions">
            <span className="mk-nav__action mk-nav__action--signin">
              <ActionButton variant="quiet" onClick={() => handleRouteAction('signin')}>
                Sign in
              </ActionButton>
            </span>
            <span className="mk-nav__action mk-nav__action--access">
              <ActionButton variant="ghost" onClick={() => handleRouteAction('signup')}>
                Request access
              </ActionButton>
            </span>

            <button
              ref={toggleRef}
              type="button"
              className="mk-nav__toggle"
              aria-expanded={menuOpen}
              aria-controls={MENU_ID}
              onClick={() => setMenuOpen((open) => !open)}
            >
              <span className="mk-nav__toggle-text mk-mono">{menuOpen ? 'Close' : 'Menu'}</span>
              <span
                className={menuOpen ? 'mk-nav__toggle-bars is-open' : 'mk-nav__toggle-bars'}
                aria-hidden="true"
              >
                <span />
                <span />
                <span />
              </span>
            </button>
          </div>
        </div>

        {/* Both lists carry the same label. Only one of them is ever displayed, so only one is
            ever in the accessibility tree -- the alternative, inventing a second label, would
            describe a distinction the reader cannot perceive. */}
        <div id={MENU_ID} className="mk-nav__panel" hidden={!menuOpen}>
          <div className="mk-nav__panel-inner mk-shell">
            <nav aria-label="Landing sections">
              <ul className="mk-nav__panel-list">
                {NAV_LINKS.map((link) => renderLink(link, 'mk-nav__panel-link'))}
              </ul>
            </nav>
            <div className="mk-nav__panel-actions">
              <ActionButton variant="ghost" fullWidth onClick={() => handleRouteAction('signin')}>
                Sign in
              </ActionButton>
              <ActionButton variant="primary" fullWidth onClick={() => handleRouteAction('signup')}>
                Request access
              </ActionButton>
            </div>
          </div>
        </div>
      </header>
    </>
  );
}
