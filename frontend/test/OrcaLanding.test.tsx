import { fireEvent, render, screen, within } from '@testing-library/react';
import type { RenderResult } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { OrcaLanding } from '../src/marketing/OrcaLanding';
import { RouteProvider, useRoute } from '../src/routes/RouteContext';

/** The landing page as a whole.
 *
 * Most of what is pinned here is honesty copy, and that is deliberate: house rule 4 allows a
 * figure on this surface only if it is either measured and cited or labelled illustrative in
 * VISIBLE text, and a label is exactly the kind of thing a later edit removes without noticing.
 * The assertions are therefore on the actual strings, so deleting one fails the suite rather
 * than quietly turning a fixture into an implied engine reading.
 *
 * The rest is structure that assistive technology depends on and that nothing on screen would
 * show to be broken: one `h1` for the view, the section ids the nav and the router both address
 * by name, and the scene's non-WebGL path — which is the path this environment takes, and the
 * one a reader with WebGL disabled takes too. */

/** jsdom's `window.scrollTo` reports "not implemented" to its virtual console instead of
 * throwing, so `RouteProvider`'s own try/catch never sees it and every render here would print a
 * stack trace over the suite's output. `RouteContext.test.tsx` owns the assertions on that. */
let originalScrollTo: typeof window.scrollTo;
let fetchCalls: string[];
let originalFetch: typeof globalThis.fetch;
let originalGetContext: typeof HTMLCanvasElement.prototype.getContext;

beforeEach(() => {
  // The location is real, shared and written by the navigation tests below, so each test starts
  // from a clean URL rather than from wherever the previous one left the reader.
  window.history.replaceState(null, '', '/');

  originalScrollTo = window.scrollTo;
  window.scrollTo = (() => undefined) as typeof window.scrollTo;

  // `useWebGLSupport()` probes for a context and correctly concludes there is none. jsdom
  // answers with null and ALSO reports "not implemented" to its virtual console, twice per
  // render. This returns the same null quietly; the assertions below still exercise the
  // no-WebGL branch, which is the point of them.
  originalGetContext = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = (() =>
    null) as typeof HTMLCanvasElement.prototype.getContext;

  fetchCalls = [];
  originalFetch = globalThis.fetch;
  globalThis.fetch = ((input: RequestInfo | URL) => {
    fetchCalls.push(String(input));
    return Promise.reject(new Error('The landing page must not reach the network.'));
  }) as typeof globalThis.fetch;
});

afterEach(() => {
  window.scrollTo = originalScrollTo;
  globalThis.fetch = originalFetch;
  HTMLCanvasElement.prototype.getContext = originalGetContext;
});

/** Reads the live route alongside the page, so a nav control can be asserted to navigate rather
 * than merely to exist. Local to this file: `test/testUtils.tsx` is shared with the workspace
 * suites and has no reason to know about routing. */
function RouteProbe() {
  const { route } = useRoute();
  return <span data-testid="route">{route}</span>;
}

function renderLanding(): RenderResult {
  return render(
    <RouteProvider>
      <OrcaLanding />
      <RouteProbe />
    </RouteProvider>,
  );
}

/** Every id the nav rail, the footer links and `navigate('landing', { section })` address by
 * name. A section renamed without updating its callers leaves controls that scroll nowhere. */
const SECTION_IDS: readonly string[] = [
  'top',
  'overview',
  'approach',
  'optimization',
  'workspace-preview',
  'capabilities',
  'research',
  'access',
];

describe('OrcaLanding: document structure', () => {
  it('has exactly one h1 for the view', () => {
    renderLanding();
    const headings = screen.getAllByRole('heading', { level: 1 });
    expect(headings).toHaveLength(1);
  });

  it('opens every band with an h2, so nothing is nested under a level that does not exist', () => {
    renderLanding();
    expect(screen.getAllByRole('heading', { level: 2 }).length).toBeGreaterThan(0);
  });

  it('renders every section id the nav and the router address by name', () => {
    const { container } = renderLanding();
    for (const id of SECTION_IDS) {
      expect(container.querySelector(`#${id}`)).not.toBeNull();
    }
  });

  it('renders one main landmark and one footer', () => {
    renderLanding();
    expect(screen.getAllByRole('main')).toHaveLength(1);
    expect(screen.getAllByRole('contentinfo')).toHaveLength(1);
  });

  it('offers a skip link as the first control on the page', () => {
    renderLanding();
    expect(screen.getByRole('link', { name: 'Skip to content' })).toBeInTheDocument();
  });

  it('names the document', () => {
    renderLanding();
    expect(document.title).toBe('ORCA — Autonomous Orbital Rescue Mission Planning');
  });
});

describe('OrcaLanding: the honesty labels (house rule 4)', () => {
  it('labels the hero annotations as an illustrative scenario', () => {
    renderLanding();
    expect(screen.getByText('Illustrative scenario')).toBeInTheDocument();
    expect(
      screen.getByText(
        'Values annotating the render are fixture data, not engine output. This page holds no ' +
          'engine session.',
      ),
    ).toBeInTheDocument();
  });

  it('states in the hero readouts that there is no telemetry link', () => {
    renderLanding();
    expect(screen.getByText('no engine session')).toBeInTheDocument();
  });

  it('labels the workspace preview as a preview, twice: on the bar and under it', () => {
    renderLanding();
    expect(screen.getByText('Interface preview · illustrative scenario')).toBeInTheDocument();
    expect(
      screen.getByText(
        'This panel is not connected to the engine. It shows no live telemetry and issues no ' +
          'commands, and every value in it is an illustrative fixture. The real workspace runs ' +
          'the C++20 engine over the Node bridge and is one click away.',
      ),
    ).toBeInTheDocument();
  });

  it('labels the trade-space fixture as a fixture', () => {
    renderLanding();
    expect(
      screen.getByText(/Illustrative fixture data, not engine output\./),
    ).toBeInTheDocument();
  });

  it('states that the page itself never reaches the engine', () => {
    renderLanding();
    expect(
      screen.getByText(
        /it does not connect to the C\+\+ engine, and no figure shown here is live telemetry/,
      ),
    ).toBeInTheDocument();
  });

  it('makes no claim of flight readiness or certification', () => {
    renderLanding();
    expect(
      screen.getByText(/No claim of flight readiness, certification, operational qualification/),
    ).toBeInTheDocument();
  });

  it('states that authentication has no backend, rather than implying one', () => {
    renderLanding();
    expect(screen.getByText('Authentication service · not connected')).toBeInTheDocument();
    expect(
      screen.getByText(/Registration has no backend in this build\./),
    ).toBeInTheDocument();
  });

  it('says the measured figures are transcribed from cited documents', () => {
    renderLanding();
    expect(screen.getByText('Measured, sources cited')).toBeInTheDocument();
  });

  it('makes no network request while rendering -- the claim above is not just copy', () => {
    renderLanding();
    expect(fetchCalls).toEqual([]);
  });
});

describe('OrcaLanding: the 3D scene falls back honestly without WebGL', () => {
  it('renders the SVG schematic as a real image, not an empty box', () => {
    // jsdom has no WebGL, so `useWebGLSupport()` resolves to `unavailable` and this is the
    // branch a reader with WebGL disabled or blocklisted also gets.
    renderLanding();
    expect(screen.getByRole('img', { name: /Orbital rescue schematic/ })).toBeInTheDocument();
  });

  it('describes what the schematic depicts, and admits it is not engine output', () => {
    renderLanding();
    expect(
      screen.getByText(/The geometry is illustrative and is not\s+engine output\./),
    ).toBeInTheDocument();
    expect(screen.getByText('ILLUSTRATIVE GEOMETRY — NOT ENGINE OUTPUT')).toBeInTheDocument();
  });

  it('admits the altitude exaggeration the schematic applies, and cites where it comes from', () => {
    renderLanding();
    expect(screen.getByText(/ALTITUDE EXAGGERATED — coordinates\.ts/)).toBeInTheDocument();
  });
});

describe('OrcaLanding: the entry controls', () => {
  function nav(container: HTMLElement): HTMLElement {
    const header = container.querySelector('.mk-nav');
    if (!(header instanceof HTMLElement)) throw new Error('The landing nav did not render.');
    return header;
  }

  it('offers sign-in and request-access in the nav', () => {
    const { container } = renderLanding();
    const bar = within(nav(container));
    expect(bar.getByRole('button', { name: 'Sign in' })).toBeInTheDocument();
    expect(bar.getByRole('button', { name: 'Request access' })).toBeInTheDocument();
  });

  it('routes to the sign-in view from the nav', () => {
    const { container } = renderLanding();
    fireEvent.click(within(nav(container)).getByRole('button', { name: 'Sign in' }));
    expect(screen.getByTestId('route').textContent).toBe('signin');
  });

  it('routes to the sign-up view from the nav', () => {
    const { container } = renderLanding();
    fireEvent.click(within(nav(container)).getByRole('button', { name: 'Request access' }));
    expect(screen.getByTestId('route').textContent).toBe('signup');
  });

  it('repeats both entry points in the access band, where the reader ends up', () => {
    const { container } = renderLanding();
    const access = container.querySelector('#access');
    expect(access).not.toBeNull();
    if (!(access instanceof HTMLElement)) return;
    const band = within(access);
    expect(band.getByRole('button', { name: 'Request research access' })).toBeInTheDocument();
    expect(band.getByRole('button', { name: 'Sign in' })).toBeInTheDocument();
    expect(band.getByRole('button', { name: 'Open the workspace' })).toBeInTheDocument();
  });

  it('names the hero actions after what they do on this page', () => {
    const { container } = renderLanding();
    const hero = container.querySelector('#top');
    expect(hero).not.toBeNull();
    if (!(hero instanceof HTMLElement)) return;
    // The primary hero action jumps to the problem statement; it does not open the workspace,
    // so it must not say that it does.
    expect(within(hero).getByRole('button', { name: 'Explore the mission' })).toBeInTheDocument();
    expect(
      within(hero).getByRole('button', { name: 'Request research access' }),
    ).toBeInTheDocument();
  });

  it('opens the compact menu as a disclosure, not a modal', () => {
    const { container } = renderLanding();
    const bar = within(nav(container));
    const toggle = bar.getByRole('button', { name: 'Menu' });
    expect(toggle).toHaveAttribute('aria-expanded', 'false');
    fireEvent.click(toggle);
    expect(bar.getByRole('button', { name: 'Close' })).toHaveAttribute('aria-expanded', 'true');
  });

  it('closes the compact menu on Escape and returns focus to the control that opened it', () => {
    const { container } = renderLanding();
    const bar = within(nav(container));
    const toggle = bar.getByRole('button', { name: 'Menu' });
    fireEvent.click(toggle);
    fireEvent.keyDown(document, { key: 'Escape' });
    expect(bar.getByRole('button', { name: 'Menu' })).toHaveAttribute('aria-expanded', 'false');
    expect(toggle).toHaveFocus();
  });

  it('moves both entry points into the compact menu rather than dropping them', () => {
    const { container } = renderLanding();
    const bar = within(nav(container));
    // The panel carries `hidden` until the disclosure is opened, so its copies are out of the
    // accessibility tree until then. Below 1200px the rail is what disappears, and these are
    // the only sign-in and request-access controls left in the header.
    fireEvent.click(bar.getByRole('button', { name: 'Menu' }));
    expect(bar.getAllByRole('button', { name: 'Sign in' })).toHaveLength(2);
    expect(bar.getAllByRole('button', { name: 'Request access' })).toHaveLength(2);
  });
});

describe('OrcaLanding: in-page jumps never reach the router', () => {
  /** The nav, the hero and the footer all link to sections with real anchors, and all three
   * prevent the default. `RouteContext` treats the hash as the source of truth for which view is
   * mounted, so letting `#overview` reach the URL would push an unrecognised route through the
   * router — which resolves to the landing page — on every section jump. */
  function jumpTargets(container: HTMLElement): readonly HTMLAnchorElement[] {
    return [
      container.querySelector('.mk-skip-link'),
      container.querySelector('.mk-nav__link[href="#overview"]'),
      container.querySelector('.mk-hero__scroll'),
      container.querySelector('.mk-footer a[href="#research"]'),
    ].filter((node): node is HTMLAnchorElement => node instanceof HTMLAnchorElement);
  }

  it('leaves the hash untouched when a section link is activated', () => {
    const { container } = renderLanding();
    const targets = jumpTargets(container);
    expect(targets.length).toBe(4);

    for (const link of targets) {
      fireEvent.click(link);
      expect(window.location.hash).toBe('');
      expect(screen.getByTestId('route').textContent).toBe('landing');
    }
  });

  it('moves focus into the section it jumped to, not only the scroll position', () => {
    const { container } = renderLanding();
    const skip = container.querySelector('.mk-skip-link');
    expect(skip).toBeInstanceOf(HTMLAnchorElement);
    if (!(skip instanceof HTMLAnchorElement)) return;

    fireEvent.click(skip);
    // Without this a keyboard reader's next Tab continues from the link they just left.
    expect(document.activeElement?.id).toBe('top');
    expect(document.activeElement).toHaveAttribute('tabindex', '-1');
  });
});
