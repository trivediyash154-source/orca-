import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

// `app.css` is imported ahead of the app module on purpose. It carries `tokens.css` and the
// document-level reset, and `App` pulls in `styles/marketing/index.css` transitively (via
// `OrcaLanding`), so importing the app first would emit the entry surface's rules *before* the
// base layer they specialise. Nothing collides today -- the two surfaces are namespaced apart --
// but the cascade should read in the order the layers are meant to apply, not in whatever order
// the module graph happened to resolve.
import './styles/app.css';
import { App } from './App';

const rootElement = document.getElementById('root');
if (!rootElement) {
  throw new Error('#root element not found');
}

createRoot(rootElement).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
