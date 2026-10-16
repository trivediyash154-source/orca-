/**
 * Theme state — light and dark as two designed surfaces, not one inverted.
 *
 * ## Why a context rather than a CSS media query alone
 *
 * A pure `prefers-color-scheme` stylesheet cannot be overridden by the user, and it cannot reach
 * WebGL. Both matter here: the operator needs to choose, and the globe's exposure genuinely has
 * to change with the theme (`earthMaterial.ts`'s `EARTH_GRADING` explains why a planet graded for
 * black surroundings looks murky against a pale page). So the resolved theme lives in React,
 * is written to `data-theme` on the document element for CSS, and is read by the scene for its
 * own grading.
 *
 * ## Three states, not two
 *
 * The stored preference is `'system' | 'light' | 'dark'`. Collapsing it to a boolean loses the
 * ability to follow the OS, which is the correct default and the one most people want — and it
 * means a user who has never touched the toggle is not locked to whatever the app guessed on
 * their first visit.
 */

import { createContext, useCallback, useContext, useEffect, useMemo, useState } from 'react';
import type { ReactNode } from 'react';

export type ThemePreference = 'system' | 'light' | 'dark';
export type ResolvedTheme = 'light' | 'dark';

const STORAGE_KEY = 'orca.theme';

export interface ThemeContextValue {
  readonly preference: ThemePreference;
  /** What is actually on screen: `preference`, with `'system'` resolved. */
  readonly theme: ResolvedTheme;
  readonly setPreference: (preference: ThemePreference) => void;
  /** Cycles dark -> light -> system, which is what a single toggle button needs. */
  readonly cycle: () => void;
}

const ThemeContext = createContext<ThemeContextValue | null>(null);

function readStoredPreference(): ThemePreference {
  // Storage can throw outright in a private window or with site data blocked, so this is
  // wrapped rather than merely null-checked.
  try {
    const stored = window.localStorage.getItem(STORAGE_KEY);
    if (stored === 'light' || stored === 'dark' || stored === 'system') return stored;
  } catch {
    // Fall through to the default.
  }
  return 'system';
}

function systemTheme(): ResolvedTheme {
  if (typeof window === 'undefined' || typeof window.matchMedia !== 'function') return 'dark';
  return window.matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
}

export function ThemeProvider({ children }: { readonly children: ReactNode }) {
  const [preference, setPreferenceState] = useState<ThemePreference>(readStoredPreference);
  const [systemResolved, setSystemResolved] = useState<ResolvedTheme>(systemTheme);

  // Following the OS means following it as it CHANGES, not only reading it once at mount.
  useEffect(() => {
    if (typeof window === 'undefined' || typeof window.matchMedia !== 'function') return;
    const query = window.matchMedia('(prefers-color-scheme: light)');
    const onChange = () => setSystemResolved(query.matches ? 'light' : 'dark');
    if (typeof query.addEventListener === 'function') {
      query.addEventListener('change', onChange);
      return () => query.removeEventListener('change', onChange);
    }
    // Safari below 14 only has the deprecated form.
    query.addListener(onChange);
    return () => query.removeListener(onChange);
  }, []);

  const theme: ResolvedTheme = preference === 'system' ? systemResolved : preference;

  // `data-theme` is what every stylesheet keys off, and `color-scheme` is what makes native
  // controls, scrollbars and form widgets match — without it a light page keeps dark scrollbars.
  useEffect(() => {
    const root = document.documentElement;
    root.setAttribute('data-theme', theme);
    root.style.colorScheme = theme;
  }, [theme]);

  const setPreference = useCallback((next: ThemePreference) => {
    setPreferenceState(next);
    try {
      window.localStorage.setItem(STORAGE_KEY, next);
    } catch {
      // A preference that cannot be persisted still applies for this session.
    }
  }, []);

  const cycle = useCallback(() => {
    setPreferenceState((current) => {
      const next: ThemePreference =
        current === 'dark' ? 'light' : current === 'light' ? 'system' : 'dark';
      try {
        window.localStorage.setItem(STORAGE_KEY, next);
      } catch {
        // As above.
      }
      return next;
    });
  }, []);

  const value = useMemo<ThemeContextValue>(
    () => ({ preference, theme, setPreference, cycle }),
    [preference, theme, setPreference, cycle],
  );

  return <ThemeContext.Provider value={value}>{children}</ThemeContext.Provider>;
}

/**
 * Reads the theme. Falls back to dark outside a provider rather than throwing: a visual
 * preference is not worth crashing a component over, and several surfaces (the auth page, the
 * marketing sections) are rendered in tests without the full provider stack.
 */
export function useTheme(): ThemeContextValue {
  const ctx = useContext(ThemeContext);
  if (ctx !== null) return ctx;
  return {
    preference: 'system',
    theme: 'dark',
    setPreference: () => {},
    cycle: () => {},
  };
}
