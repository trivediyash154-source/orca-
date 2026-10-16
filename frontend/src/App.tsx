import { useEffect } from 'react';

import { AuthPage } from './auth/AuthPage';
import { AppShell } from './components/layout/AppShell';
import { OrcaLanding } from './marketing/OrcaLanding';
import { AlgorithmLabPage } from './pages/AlgorithmLab/AlgorithmLab';
import { AnalyticsPage } from './pages/Analytics/Analytics';
import { MissionControlPage } from './pages/MissionControl/MissionControl';
import { MissionHistoryPage } from './pages/MissionHistory/MissionHistory';
import { MissionPlannerPage } from './pages/MissionPlanner/MissionPlanner';
import { OptimizationLabPage } from './pages/OptimizationLab/OptimizationLab';
import { OverviewPage } from './pages/Overview/Overview';
import { OrbitalCatalogPage } from './pages/OrbitalCatalog/OrbitalCatalog';
import { OrbitalNetworkPage } from './pages/OrbitalNetwork/OrbitalNetwork';
import { PresentationModeView } from './pages/PresentationMode/PresentationMode';
import { ScenarioPage } from './pages/Scenario/Scenario';
import { SettingsPage } from './pages/Settings/Settings';
import { RouteProvider, useRoute } from './routes/RouteContext';
import { readSession } from './auth/accountStore';
import { SessionProvider, useSession } from './state/SessionContext';
import { ThemeProvider } from './state/ThemeContext';
import { EngineProvider } from './state/EngineContext';
import { UIProvider, useUI } from './state/UIContext';

const BACKEND_BASE_URL = import.meta.env.VITE_BACKEND_URL ?? 'http://localhost:4000';

function CurrentPage() {
  const { page } = useUI();
  switch (page) {
    case 'overview':
      return <OverviewPage />;
    case 'mission-control':
      return <MissionControlPage />;
    case 'mission-planner':
      return <MissionPlannerPage />;
    case 'orbital-catalog':
      return <OrbitalCatalogPage />;
    case 'orbital-network':
      return <OrbitalNetworkPage />;
    case 'optimization-lab':
      return <OptimizationLabPage />;
    case 'algorithm-lab':
      return <AlgorithmLabPage />;
    case 'analytics':
      return <AnalyticsPage />;
    case 'mission-history':
      return <MissionHistoryPage />;
    case 'scenario':
      return <ScenarioPage />;
    case 'settings':
      return <SettingsPage />;
  }
}

function Shell() {
  const { presentationMode } = useUI();
  if (presentationMode) {
    return <PresentationModeView />;
  }
  return (
    <AppShell>
      <CurrentPage />
    </AppShell>
  );
}

/**
 * The workspace, and the only place `EngineProvider` is mounted.
 *
 * That scoping is the point of this component rather than an accident of nesting.
 * `EngineProvider` opens a 500 ms poll against the Node bridge for as long as it is mounted,
 * and `AppShell` renders `EngineOfflineOverlay` over anything below it whenever that poll
 * cannot reach the C++ engine. Mounting it above the route switch would mean the landing page
 * and the sign-in view both demanded a running engine to be legible -- a marketing surface
 * that goes dark because a local backend is not started is broken, and it would also poll an
 * endpoint the entry surface never reads. Those two views compute nothing from the engine, so
 * they take no engine context at all.
 *
 * The consequence is deliberate: leaving the workspace unmounts both providers, so the poll
 * stops and UI-only state (current page, presentation mode) starts fresh on return. Keeping
 * that state alive would mean keeping the snapshot behind it alive too, and a snapshot that
 * kept ageing while the reader was on the landing page is exactly the stale engine truth the
 * architecture forbids displaying.
 */
function Workspace() {
  const { user, refresh } = useSession();
  const { navigate } = useRoute();

  /**
   * Storage is consulted directly, not just the context's state.
   *
   * A successful sign-in writes the session and then sets `location.hash` to `#/workspace`.
   * Both the router and `SessionProvider` learn about that from the same `hashchange`, so
   * whether this component's first render already sees the new user depends on listener
   * order -- and when it does not, the guard below bounces a reader who has just signed in
   * straight back to the form. Reading `localStorage` here is synchronous and authoritative,
   * which removes the race rather than papering over it with a delay.
   */
  const session = user ?? readSession();

  // The guard runs in an effect rather than during render because `navigate()` writes
  // `location.hash`, and a side effect in a render body is a side effect React is free to run
  // twice (it does, under StrictMode in development).
  useEffect(() => {
    if (session === null) navigate('signin');
  }, [session, navigate]);

  // Brings the context up to date when storage was ahead of it, so the top bar's account chip
  // and the landing page's nav agree with what this component just decided.
  useEffect(() => {
    if (user === null && session !== null) refresh();
  }, [user, session, refresh]);

  if (session === null) {
    // Not an error state and not a spinner: the redirect above is already in flight, and this
    // is the one frame between it being scheduled and the hash changing.
    return (
      <div className="mk-page mk-auth">
        <main className="mk-auth__grid">
          <div className="mk-auth-console">
            <p className="mk-auth-feedback" role="status">
              This workspace needs an account. Opening the sign-in page.
            </p>
          </div>
        </main>
      </div>
    );
  }

  return (
    <EngineProvider baseUrl={BACKEND_BASE_URL}>
      <UIProvider>
        <Shell />
      </UIProvider>
    </EngineProvider>
  );
}

/** Exhaustive over `RouteName`, so a new route is a compile error here rather than a blank
 * screen at runtime. */
function CurrentRoute() {
  const { route } = useRoute();
  switch (route) {
    case 'landing':
      return <OrcaLanding />;
    case 'signin':
      return <AuthPage mode="signin" />;
    case 'signup':
      return <AuthPage mode="signup" />;
    case 'workspace':
      return <Workspace />;
  }
}

export function App() {
  // Theme and session both sit ABOVE routing: they apply to the landing page, the
  // authentication views and the workspace alike, and a route change must never reset either.
  return (
    <ThemeProvider>
      <SessionProvider>
        <RouteProvider>
          <CurrentRoute />
        </RouteProvider>
      </SessionProvider>
    </ThemeProvider>
  );
}
