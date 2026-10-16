import { act, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import {
  RouteProvider,
  useLandingScrollTarget,
  useRoute,
  type RouteName,
} from '../src/routes/RouteContext';

/** The entry surface's hand-written hash router. The hash is the source of truth, so what has
 * to hold is a round trip: a bookmarked or reloaded `#/signin` must mount the sign-in view, and
 * `navigate()` must write back the canonical hash for the view it mounted. The unknown-hash case
 * is here because a stale link has to land somewhere useful rather than on an error view, and
 * the `pendingSection` cases are here because navigating to the route you are already on writes
 * the same hash, fires no `hashchange`, and would otherwise drop the section request entirely. */

/** Reads the live context and exposes the controls a test needs to drive it. Kept local rather
 * than added to `test/testUtils.tsx`, which the workspace suites share. */
function RouteProbe() {
  const { route, navigate, pendingSection, clearPendingSection } = useRoute();
  return (
    <div>
      <span data-testid="route">{route}</span>
      <span data-testid="pending">{pendingSection ?? 'none'}</span>
      <button type="button" onClick={() => navigate('signin')}>
        go signin
      </button>
      <button type="button" onClick={() => navigate('workspace')}>
        go workspace
      </button>
      <button type="button" onClick={() => navigate('landing', { section: 'research' })}>
        go landing research
      </button>
      <button type="button" onClick={() => navigate('signup', { section: 'research' })}>
        go signup with section
      </button>
      <button type="button" onClick={clearPendingSection}>
        clear pending
      </button>
    </div>
  );
}

/** Sets the location without dispatching `hashchange`, which is what a cold load looks like:
 * the provider's initialiser reads `window.location.hash` and nothing has fired yet. */
function setInitialHash(hash: string): void {
  window.history.replaceState(null, '', hash === '' ? '/' : hash);
}

function renderRouter(hash: string) {
  setInitialHash(hash);
  return render(
    <RouteProvider>
      <RouteProbe />
    </RouteProvider>,
  );
}

function currentRoute(): string {
  return screen.getByTestId('route').textContent ?? '';
}

/** jsdom ships `window.scrollTo` as a stub that reports "not implemented" to its virtual
 * console instead of throwing, so the provider's try/catch never sees it and every navigation
 * would print a stack trace into the suite's output. Replacing it with a recorder both quiets
 * that and turns it into an assertion: the scroll reset is behaviour worth pinning. */
let scrollCalls: number;
let originalScrollTo: typeof window.scrollTo;

beforeEach(() => {
  setInitialHash('/');
  scrollCalls = 0;
  originalScrollTo = window.scrollTo;
  window.scrollTo = (() => {
    scrollCalls += 1;
  }) as typeof window.scrollTo;
});

afterEach(() => {
  window.scrollTo = originalScrollTo;
});

describe('routeFromHash (hash -> route, on a cold load)', () => {
  const CANONICAL: readonly (readonly [string, RouteName])[] = [
    ['#/', 'landing'],
    ['#/signin', 'signin'],
    ['#/signup', 'signup'],
    ['#/workspace', 'workspace'],
  ];

  it.each(CANONICAL)('mounts %s as the %s view', (hash, expected) => {
    renderRouter(hash);
    expect(currentRoute()).toBe(expected);
  });

  it('is tolerant on the way in: missing slash, trailing slash and case all resolve', () => {
    for (const hash of ['#signin', '#/signin/', '#//signin', '#/SignIn', '#/SIGNIN/']) {
      const view = renderRouter(hash);
      expect(currentRoute()).toBe('signin');
      view.unmount();
    }
  });

  it('falls back to the landing view for an unrecognised hash, never an error view', () => {
    for (const hash of ['', '#', '#/mission-control', '#/signin/extra', '#/../etc']) {
      const view = renderRouter(hash);
      expect(currentRoute()).toBe('landing');
      view.unmount();
    }
  });
});

describe('navigate (route -> hash)', () => {
  it('writes the canonical hash for the view it mounted', () => {
    renderRouter('#/');
    fireEvent.click(screen.getByText('go signin'));
    expect(currentRoute()).toBe('signin');
    expect(window.location.hash).toBe('#/signin');
  });

  it('canonicalises on the way out: a tolerated hash is rewritten, not preserved', () => {
    renderRouter('#signin');
    expect(currentRoute()).toBe('signin');
    fireEvent.click(screen.getByText('go workspace'));
    expect(window.location.hash).toBe('#/workspace');
  });

  it('updates the view immediately, without waiting for a hashchange to come back', () => {
    renderRouter('#/');
    // No event is dispatched here at all: `navigate()` sets local state as well as the hash,
    // which is what keeps a same-route navigation from being dropped.
    fireEvent.click(screen.getByText('go workspace'));
    expect(currentRoute()).toBe('workspace');
  });
});

describe('scroll reset on a route change', () => {
  it('starts a new view at the top', () => {
    renderRouter('#/');
    const before = scrollCalls;
    fireEvent.click(screen.getByText('go signin'));
    expect(scrollCalls).toBe(before + 1);
  });

  it('does not reset first when the navigation itself asked for a section', () => {
    renderRouter('#/signin');
    const before = scrollCalls;
    // Scrolling to the top and then to the section would be a visible double jump.
    fireEvent.click(screen.getByText('go landing research'));
    expect(currentRoute()).toBe('landing');
    expect(scrollCalls).toBe(before);
  });
});

describe('browser navigation (back, forward, a pasted hash)', () => {
  it('follows a hashchange it did not initiate', () => {
    renderRouter('#/');
    expect(currentRoute()).toBe('landing');

    act(() => {
      window.history.replaceState(null, '', '#/signup');
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(currentRoute()).toBe('signup');

    act(() => {
      window.history.replaceState(null, '', '#/');
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(currentRoute()).toBe('landing');
  });

  it('re-reads the location on mount, catching a hash written before the listener attached', () => {
    setInitialHash('/');
    const view = render(
      <RouteProvider>
        <RouteProbe />
      </RouteProvider>,
    );
    // A redirect of this shape (hash rewritten between first render and the mount effect) is
    // the case the provider's on-mount re-read exists for.
    act(() => {
      window.history.replaceState(null, '', '#/workspace');
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(currentRoute()).toBe('workspace');
    view.unmount();
  });
});

describe('pendingSection', () => {
  it('carries a section request from another route to the landing view', () => {
    renderRouter('#/signin');
    fireEvent.click(screen.getByText('go landing research'));
    expect(currentRoute()).toBe('landing');
    expect(screen.getByTestId('pending').textContent).toBe('research');
  });

  it('survives a navigation to the route already mounted, where no hashchange fires', () => {
    renderRouter('#/');
    // `#/` is already the location, so `navigate()` writes an identical hash and the browser
    // fires no `hashchange` -- exactly the case that would drop the request without local state.
    expect(window.location.hash).toBe('#/');
    fireEvent.click(screen.getByText('go landing research'));
    expect(screen.getByTestId('pending').textContent).toBe('research');
  });

  it('is cleared explicitly, and does not linger for a later unrelated render', () => {
    renderRouter('#/signin');
    fireEvent.click(screen.getByText('go landing research'));
    expect(screen.getByTestId('pending').textContent).toBe('research');
    fireEvent.click(screen.getByText('clear pending'));
    expect(screen.getByTestId('pending').textContent).toBe('none');
  });

  it('is dropped for a route that has no sections, rather than queued against it', () => {
    renderRouter('#/');
    fireEvent.click(screen.getByText('go signup with section'));
    expect(currentRoute()).toBe('signup');
    expect(screen.getByTestId('pending').textContent).toBe('none');
  });

  it('is reset by the next navigation that asks for no section', () => {
    renderRouter('#/signin');
    fireEvent.click(screen.getByText('go landing research'));
    expect(screen.getByTestId('pending').textContent).toBe('research');
    fireEvent.click(screen.getByText('go signin'));
    expect(screen.getByTestId('pending').textContent).toBe('none');
  });
});

/** Mirrors what the landing page does: one consumer, mounted alongside the sections, that
 * consumes the request once. */
function LandingConsumer() {
  useLandingScrollTarget();
  return <section id="research">Research</section>;
}

describe('useLandingScrollTarget (the consuming half of the round trip)', () => {
  it('consumes the request, so it fires once and not again on a later render', () => {
    setInitialHash('#/signin');
    render(
      <RouteProvider>
        <RouteProbe />
        <LandingConsumer />
      </RouteProvider>,
    );

    fireEvent.click(screen.getByText('go landing research'));
    expect(screen.getByTestId('pending').textContent).toBe('none');
  });

  it('clears an id that does not resolve, rather than leaving it queued', () => {
    setInitialHash('#/signin');
    render(
      <RouteProvider>
        <RouteProbe />
        {/* No element with a matching id is rendered here. */}
        <LandingConsumerWithoutTarget />
      </RouteProvider>,
    );

    fireEvent.click(screen.getByText('go landing research'));
    expect(screen.getByTestId('pending').textContent).toBe('none');
  });
});

function LandingConsumerWithoutTarget() {
  useLandingScrollTarget();
  return null;
}

describe('useRoute outside a provider', () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('throws rather than silently handing back a route nobody is driving', () => {
    // React re-reports a render-phase throw through `console.error` on its way out. The throw
    // is what this asserts on; the report is noise that would otherwise bury a real failure in
    // the suite's output.
    vi.spyOn(console, 'error').mockImplementation(() => {});
    expect(() => render(<RouteProbe />)).toThrow(/requires a <RouteProvider> ancestor/);
  });
});
