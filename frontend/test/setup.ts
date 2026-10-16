import { cleanup } from '@testing-library/react';
import { afterEach } from 'vitest';

import '@testing-library/jest-dom/vitest';

// jsdom does not implement `matchMedia` -- stubbed here so `UIContext`'s real
// `prefers-reduced-motion` read (`state/UIContext.tsx`) does not throw in tests. Reports
// "no preference" by default, matching this app's existing conservative default and every
// existing test's assumption that `reducedMotion` starts `false`.
if (typeof window.matchMedia !== 'function') {
  window.matchMedia = (query: string) => ({
    matches: false,
    media: query,
    onchange: null,
    addListener: () => {},
    removeListener: () => {},
    addEventListener: () => {},
    removeEventListener: () => {},
    dispatchEvent: () => false,
  }) as MediaQueryList;
}

// jsdom does not implement `ResizeObserver` either -- `@react-three/fiber`'s `<Canvas>`
// (react-use-measure) needs one just to MOUNT, before any real WebGL work happens. This
// stub never fires callbacks (no test asserts on resize behavior); it exists purely so a
// page that renders a `<Canvas>` (Phase 12's `ConstellationScene`, Mission Control's own
// `Scene`) does not crash on mount in jsdom -- a real browser has this API natively.
if (typeof window.ResizeObserver !== 'function') {
  window.ResizeObserver = class {
    observe() {}
    unobserve() {}
    disconnect() {}
  } as unknown as typeof ResizeObserver;
}

// `globals: false` in vite.config.ts means @testing-library/react's own automatic
// afterEach-cleanup registration (which detects a global `afterEach`) never fires, so it is
// registered explicitly here instead -- without this, each test's render accumulates in the
// jsdom document rather than being unmounted, and `getByText`-style single-match queries
// start seeing duplicates from every earlier test in the same file.
afterEach(() => {
  cleanup();
});
