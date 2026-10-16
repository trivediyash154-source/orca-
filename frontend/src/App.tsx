import { AuthPage } from './auth/AuthPage';
import { AppShell } from './components/layout/AppShell';
import { OrcaLanding } from './marketing/OrcaLanding';
import { AlgorithmLabPage } from './pages/AlgorithmLab/AlgorithmLab';
import { AnalyticsPage } from './pages/Analytics/Analytics';
import { MissionControlPage } from './pages/MissionControl/MissionControl';
import { MissionHistoryPage } from './pages/MissionHistory/MissionHistory';
import { MissionPlannerPage } from './pages/MissionPlanner/MissionPlanner';
import { OptimizationLabPage } from './pages/OptimizationLab/OptimizationLab';
import { OrbitalCatalogPage } from './pages/OrbitalCatalog/OrbitalCatalog';
import { OrbitalNetworkPage } from './pages/OrbitalNetwork/OrbitalNetwork';
import { PresentationModeView } from './pages/PresentationMode/PresentationMode';
import { ScenarioPage } from './pages/Scenario/Scenario';
import { SettingsPage } from './pages/Settings/Settings';
import { RouteProvider, useRoute } from './routes/RouteContext';
import { ThemeProvider } from './state/ThemeContext';
import { EngineProvider } from './state/EngineContext';
import { UIProvider, useUI } from './state/UIContext';

const BACKEND_BASE_URL = import.meta.env.VITE_BACKEND_URL ?? 'http://localhost:4000';

function CurrentPage() {
  const { page } = useUI();
  switch (page) {
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
  // Theme sits ABOVE routing: it applies to the landing page, the authentication views and the
  // workspace alike, and a route change must never reset it.
  return (
    <ThemeProvider>
      <RouteProvider>
        <CurrentRoute />
      </RouteProvider>
    </ThemeProvider>
  );
}
