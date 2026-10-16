import { useEffect } from 'react';
import type { MouseEvent } from 'react';

import { OrcaWordmark, TelemetryReadout } from '../marketing/components';
import { OrbitalHeroScene } from '../marketing/scene/OrbitalHeroScene';
import { useRoute } from '../routes/RouteContext';
import { AuthForm } from './AuthForm';

/**
 * The entry surface's split view: the scene on the left, the console on the right.
 *
 * The two halves are one `<main>` and one continuous ground, not a hero pasted next to a card.
 * Everything readable over the scene is real HTML -- headings, telemetry labels, the citation --
 * because text baked into a WebGL render is text that cannot be selected, translated, scaled by
 * the reader's own font settings or reached by a screen reader, and the scene is exactly the
 * place where that failure would be least visible to the author.
 *
 * There is one `h1` in this view and it belongs to the form (`AuthForm`), which also owns the
 * focus move on mount and on mode change: the heading is the thing that changes when the reader
 * switches tab, so the element that renders it is the element that must move focus to it.
 */
export interface AuthPageProps {
  readonly mode: 'signin' | 'signup';
}

const DOCUMENT_TITLE: Readonly<Record<AuthPageProps['mode'], string>> = {
  signin: 'Sign in — ORCA',
  signup: 'Create an account — ORCA',
};

function BackGlyph() {
  return (
    <svg width="14" height="10" viewBox="0 0 14 10" fill="none" aria-hidden="true" focusable="false">
      <path
        d="M13 5H2M5.5 1.5 2 5l3.5 3.5"
        stroke="currentColor"
        strokeWidth="1.2"
        strokeLinecap="square"
      />
    </svg>
  );
}

export function AuthPage({ mode }: AuthPageProps) {
  const { navigate } = useRoute();

  // The title is restored on unmount rather than left behind: this is a single document, and a
  // browser tab still reading "Sign in" after the reader has reached the workspace is wrong.
  useEffect(() => {
    if (typeof document === 'undefined') return undefined;
    const previous = document.title;
    document.title = DOCUMENT_TITLE[mode];
    return () => {
      document.title = previous;
    };
  }, [mode]);

  function handleBack(event: MouseEvent<HTMLAnchorElement>): void {
    if (event.metaKey || event.ctrlKey || event.shiftKey || event.altKey || event.button !== 0) {
      return;
    }
    event.preventDefault();
    navigate('landing');
  }

  return (
    <div className="mk-page mk-auth">
      <main className="mk-auth__grid">
        <aside className="mk-auth-universe" aria-label="About ORCA">
          {/* The scene owns its own WebGL, reduced-motion and visibility gating and swaps in a
              static fallback when any of those say no; this container only positions it. */}
          <OrbitalHeroScene variant="auth" className="mk-auth-universe__scene" />

          <div className="mk-auth-universe__content">
            <OrcaWordmark withDescriptor onClick={() => navigate('landing')} />

            <div className="mk-auth-universe__copy">
              <p className="mk-auth-universe__statement">Enter the orbital intelligence layer.</p>
              <p className="mk-auth-universe__support">
                Between a 500 km and an 800 km orbit the synodic period is 25.4 hours, so the
                fuel-optimal departure phase recurs about once a day while a survival window runs
                for tens of minutes. Time and fuel are not traded here for effect: the conflict is
                in the orbital mechanics.{' '}
                <span className="mk-auth-universe__cite mk-mono">README</span>
              </p>
            </div>

            <div className="mk-auth-universe__strip">
              <TelemetryReadout label="Mission domain" value="LEO 500-800 km" />
              <TelemetryReadout label="Planning mode" value="Multi-objective" mono={false} />
              <TelemetryReadout label="Environment" value="Research demonstrator" mono={false} />
            </div>

            <p className="mk-auth-universe__footer">
              Entry surface only. This view does not connect to the C++ engine; the workspace
              does.{' '}
              <span className="mk-auth-universe__cite mk-mono">
                README, docs/ARCHITECTURE.md §6.1
              </span>
            </p>
          </div>
        </aside>

        <div className="mk-auth-console">
          <div className="mk-auth-console__top">
            {/* A real href, so the destination is visible in the status bar and the link can be
                opened in a new tab; the handler only takes over the unmodified click. Note that
                no in-page fragment link is used anywhere on this surface -- the router reads
                `location.hash`, so `href="#some-id"` would navigate the application away. */}
            <a className="mk-auth-back" href="#/" onClick={handleBack}>
              <BackGlyph />
              <span>Back to ORCA</span>
            </a>
          </div>

          <AuthForm mode={mode} />
        </div>
      </main>
    </div>
  );
}
