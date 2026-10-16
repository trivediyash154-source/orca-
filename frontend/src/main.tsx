import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

// `app.css` is imported ahead of the app module on purpose. It carries `tokens.css` and the
// document-level reset, and `App` pulls in `styles/marketing/index.css` transitively (via
// `OrcaLanding`), so importing the app first would emit the entry surface's rules *before* the
// base layer they specialise. Nothing collides today -- the two surfaces are namespaced apart --
// but the cascade should read in the order the layers are meant to apply, not in whatever order
// the module graph happened to resolve.
import '@fontsource-variable/space-grotesk';
import '@fontsource-variable/jetbrains-mono';
import './styles/app.css';
import { App } from './App';
import { orcaAuthAdapter } from './auth/authAdapter';
import { configureAuth } from './services/authService';

// Before the first render, as `authService.ts` requires: `AuthForm` reads `isAuthConfigured()`
// during its initial render to decide whether to show the integration notice, so registering
// the adapter after mount would flash a "not connected" state over a form that is connected.
configureAuth(orcaAuthAdapter);

const rootElement = document.getElementById('root');
if (!rootElement) {
  throw new Error('#root element not found');
}

createRoot(rootElement).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
