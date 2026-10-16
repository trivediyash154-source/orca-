/**
 * Who is signed in, for the whole document.
 *
 * It sits above routing (alongside `ThemeProvider`) because three separate surfaces read it:
 * the landing page's nav swaps "Sign in" for "Open workspace", the workspace guard refuses to
 * mount without a session, and the workspace's own top bar renders the account menu. One
 * context means those three can never disagree about it.
 *
 * `localStorage` is the source of truth, and the `storage` event is subscribed to, so signing
 * out in one tab signs out the others instead of leaving a second tab holding a dead session.
 */

import { createContext, useCallback, useContext, useEffect, useMemo, useState } from 'react';
import type { ReactNode } from 'react';

import { clearSession, readSession } from '../auth/accountStore';
import type { SessionUser } from '../auth/accountStore';

export interface SessionContextValue {
  readonly user: SessionUser | null;
  readonly signOut: () => void;
  /** Re-reads storage. Called after an adapter writes a session, so the UI does not have to
   * wait for a reload or a cross-tab event to notice. */
  readonly refresh: () => void;
}

const SessionContext = createContext<SessionContextValue | null>(null);

export function SessionProvider({ children }: { readonly children: ReactNode }) {
  const [user, setUser] = useState<SessionUser | null>(() => {
    if (typeof window === 'undefined') return null;
    return readSession();
  });

  const refresh = useCallback(() => {
    setUser(readSession());
  }, []);

  const signOut = useCallback(() => {
    clearSession();
    setUser(null);
    if (typeof window !== 'undefined') {
      window.location.hash = '#/';
    }
  }, []);

  useEffect(() => {
    if (typeof window === 'undefined') return undefined;

    function onStorage(event: StorageEvent): void {
      // A null key means the whole store was cleared, which is also a sign-out.
      if (event.key !== null && !event.key.startsWith('orca.session')) return;
      refresh();
    }

    // The hash changing is the moment a successful sign-in redirect lands, and it is the
    // cheapest reliable signal that the session may have just been written.
    window.addEventListener('storage', onStorage);
    window.addEventListener('hashchange', refresh);
    return () => {
      window.removeEventListener('storage', onStorage);
      window.removeEventListener('hashchange', refresh);
    };
  }, [refresh]);

  const value = useMemo<SessionContextValue>(
    () => ({ user, signOut, refresh }),
    [user, signOut, refresh],
  );

  return <SessionContext.Provider value={value}>{children}</SessionContext.Provider>;
}

export function useSession(): SessionContextValue {
  const value = useContext(SessionContext);
  if (!value) throw new Error('useSession() requires a <SessionProvider> ancestor');
  return value;
}
