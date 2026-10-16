import type { CSSProperties } from 'react';

import type { RouteContextValue } from '../../routes/RouteContext';
import { useRoute } from '../../routes/RouteContext';
import { useEngine } from '../../state/EngineContext';
import { useUI } from '../../state/UIContext';
import { useTheme } from '../../state/ThemeContext';
import { formatSimTime } from '../../utils/format';

/**
 * `useRoute()` asserts that a `<RouteProvider>` is above it, which is right for the entry
 * surface (a landing section that silently fails to navigate is worse than a crash) and wrong
 * for this file: the workspace shell has always been renderable on its own, and every existing
 * component test mounts a piece of it with only the contexts that piece actually needs. So the
 * assertion is caught rather than relaxed -- `RouteContext.tsx` is not this task's file to
 * change, and weakening `useRoute()` for everyone to suit one consumer would be the wrong
 * trade anyway.
 *
 * Hook order is stable through the catch: `useRoute()` calls exactly one hook before it can
 * throw, and whether a provider is present is fixed for the life of a mounted subtree, so this
 * either always throws or never does.
 */
function useOptionalRoute(): RouteContextValue | null {
  try {
    return useRoute();
  } catch {
    return null;
  }
}

/**
 * The wordmark is the way out of the workspace. Without it the reader who enters from the
 * landing page is stranded: the sidebar only moves between workspace pages, and the browser's
 * back button is the sole exit.
 *
 * The first four declarations undo user-agent button chrome; `.brand` still supplies every
 * colour and type value. They are inline because `.brand` lives in `app.css`, which this task
 * does not own -- no colour or spacing value is expressed here, and the rule belongs next to
 * `.brand` the next time that file is opened.
 *
 * The last two are not cosmetic. `EngineOfflineOverlay` is `position: absolute; inset: 0;
 * z-index: 10` over the whole shell, so with the engine down it covers the top bar and eats
 * every click on it -- measured in a browser: the exit was reachable by keyboard and dead to
 * the mouse. That is precisely the moment a visitor who opened the workspace without a local
 * backend needs to leave. Nothing else in the top bar is lifted: the overlay's job is to stop
 * the operator acting on state the engine is not confirming, and reading the way out is not
 * acting on it. `.app-shell` is `position: static`, so both elements resolve in the root
 * stacking context and 11 sits directly above the overlay's 10 -- if that value moves in
 * `app.css`, this one moves with it.
 */
const BRAND_BUTTON_RESET: CSSProperties = {
  background: 'none',
  border: 'none',
  padding: 0,
  cursor: 'pointer',
  position: 'relative',
  zIndex: 11,
};

/** Brief §11: scenario, sim time, engine status, simulation status, speed -- every value
 * real, from the current snapshot. Never hard-codes the scenario name (it comes from
 * `snapshot.scenarioName`, added specifically so this component would not need to). */
export function TopBar() {
  const { connectionStatus, snapshot } = useEngine();
  const { setPresentationMode } = useUI();
  const { preference, theme, cycle } = useTheme();
  const route = useOptionalRoute();

  return (
    <header className="top-bar">
      {route ? (
        <button
          type="button"
          className="brand"
          style={BRAND_BUTTON_RESET}
          onClick={() => route.navigate('landing')}
          aria-label="ORCA — return to the landing page"
          title="Return to the landing page"
        >
          ORCA
        </button>
      ) : (
        <span className="brand">ORCA</span>
      )}
      <div className="top-bar-field">
        <span className="top-bar-label">Scenario</span>
        <span className="top-bar-value">{snapshot?.scenarioName ?? 'N/A'}</span>
      </div>
      <div className="top-bar-field">
        <span className="top-bar-label">Sim Time</span>
        <span className="top-bar-value top-bar-mono">
          {snapshot ? formatSimTime(snapshot.simulationTimeSec) : '--:--:--'}
        </span>
      </div>
      <div className="top-bar-field">
        <span className="top-bar-label">Engine</span>
        <span className={`top-bar-value status-${connectionStatus}`}>
          {connectionStatus === 'Connected' ? 'CONNECTED' : connectionStatus.toUpperCase()}
        </span>
      </div>
      <div className="top-bar-field">
        <span className="top-bar-label">Simulation</span>
        <span className="top-bar-value">{snapshot?.simulationStatus ?? '--'}</span>
      </div>
      <div className="top-bar-field">
        <span className="top-bar-label">Speed</span>
        <span className="top-bar-value">{snapshot ? `${snapshot.speedMultiplier}×` : '--'}</span>
      </div>
      <button
        className="theme-toggle"
        onClick={cycle}
        // The label states the CURRENT setting rather than the next one: a control announced as
        // "switch to light" is ambiguous about what is on now, which matters most to the
        // screen-reader user who cannot see the answer.
        aria-label={`Theme: ${preference}${preference === 'system' ? ` (following system, currently ${theme})` : ''}. Activate to change.`}
        title="Change theme"
      >
        <span aria-hidden="true">{preference === 'dark' ? '\u25D0' : preference === 'light' ? '\u25D1' : '\u25D3'}</span>
        <span className="theme-toggle-label">{preference}</span>
      </button>
      <button className="presentation-toggle" onClick={() => setPresentationMode(true)}>
        Present
      </button>
    </header>
  );
}
