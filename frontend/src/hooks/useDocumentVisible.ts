/**
 * Whether the document is currently visible.
 *
 * The hero scene uses this to stop its frame loop when the tab is backgrounded. A WebGL loop
 * that keeps running in a hidden tab burns battery for nothing, and on a laptop it is audible.
 */

import { useEffect, useState } from 'react';

function readVisibility(): boolean {
  if (typeof document === 'undefined') return true;
  // `visibilityState` is absent in some minimal test environments; only an explicit "hidden"
  // is treated as hidden, so an unknown value never silently freezes the scene.
  return document.visibilityState !== 'hidden';
}

export function useDocumentVisible(): boolean {
  const [visible, setVisible] = useState<boolean>(() => readVisibility());

  useEffect(() => {
    if (typeof document === 'undefined' || typeof document.addEventListener !== 'function') {
      return undefined;
    }

    function handleVisibilityChange(): void {
      setVisible(readVisibility());
    }

    document.addEventListener('visibilitychange', handleVisibilityChange);
    return () => document.removeEventListener('visibilitychange', handleVisibilityChange);
  }, []);

  return visible;
}
