import { useCallback, useEffect, useRef, useState } from 'react';

/**
 * Transient async feedback for the entry surface — principally the authentication flow, where
 * the honest "not connected" outcome must reach a screen-reader user as reliably as it reaches
 * a sighted one.
 *
 * ## Why this stack is not a live region
 *
 * It was, and that was a defect. `AuthForm` writes each outcome into two places in one commit:
 * its own `.mk-auth-feedback` panel and a toast here, with the identical title and body. When
 * both were `role="status" aria-live="polite"`, both were mounted, and both gained a subtree in
 * the same render — so every outcome was announced twice, and the "Integration required"
 * explanation is several sentences long. Two readings of the same paragraph make the message
 * harder to follow, not more reliable.
 *
 * The panel keeps the announcement, because it is the durable copy: it stays on screen after
 * this stack has auto-dismissed, and it carries the workspace escape hatch. The toast is the
 * visual echo for a reader who is looking somewhere else on the form, and it is reached by
 * pointer and keyboard like any other content.
 */
export interface ToastMessage {
  readonly id: number;
  readonly tone: 'info' | 'error' | 'success';
  readonly title: string;
  readonly body: string;
}

export interface ToastStackProps {
  readonly toasts: readonly ToastMessage[];
  readonly onDismiss: (id: number) => void;
}

function DismissGlyph() {
  return (
    <svg width="10" height="10" viewBox="0 0 10 10" fill="none" aria-hidden="true" focusable="false">
      <path d="M1 1l8 8M9 1l-8 8" stroke="currentColor" strokeWidth="1.2" strokeLinecap="square" />
    </svg>
  );
}

export function ToastStack({ toasts, onDismiss }: ToastStackProps) {
  return (
    <div className="mk-toast-stack">
      {toasts.map((toast) => (
        <div key={toast.id} className={`mk-toast mk-toast--${toast.tone}`}>
          <div>
            <p className="mk-toast__title">{toast.title}</p>
            <p className="mk-toast__body">{toast.body}</p>
          </div>
          <button
            type="button"
            className="mk-toast__dismiss"
            onClick={() => onDismiss(toast.id)}
            aria-label={`Dismiss: ${toast.title}`}
          >
            <DismissGlyph />
          </button>
        </div>
      ))}
    </div>
  );
}

/** Long enough to read a two-line message without hurrying, short enough that a stack of them
 * clears itself while the reader is still on the same view. */
const AUTO_DISMISS_MS = 6000;

export function useToasts(): {
  readonly toasts: readonly ToastMessage[];
  readonly push: (t: Omit<ToastMessage, 'id'>) => void;
  readonly dismiss: (id: number) => void;
} {
  const [toasts, setToasts] = useState<readonly ToastMessage[]>([]);

  /** A monotonic counter, not `Date.now()`: two toasts pushed in the same millisecond would
   * collide on a timestamp, and React would then reuse one DOM node for two messages. */
  const nextIdRef = useRef(1);
  const timersRef = useRef(new Map<number, ReturnType<typeof setTimeout>>());

  const dismiss = useCallback((id: number) => {
    const timer = timersRef.current.get(id);
    if (timer !== undefined) {
      clearTimeout(timer);
      timersRef.current.delete(id);
    }
    setToasts((current) => current.filter((toast) => toast.id !== id));
  }, []);

  const push = useCallback((toast: Omit<ToastMessage, 'id'>) => {
    const id = nextIdRef.current;
    nextIdRef.current += 1;
    setToasts((current) => [...current, { ...toast, id }]);

    const timer = setTimeout(() => {
      timersRef.current.delete(id);
      setToasts((current) => current.filter((entry) => entry.id !== id));
    }, AUTO_DISMISS_MS);
    timersRef.current.set(id, timer);
  }, []);

  // Every pending auto-dismiss is cancelled on unmount, so a navigation away from the auth
  // page cannot leave a timer that calls setState on a torn-down component.
  useEffect(() => {
    const timers = timersRef.current;
    return () => {
      timers.forEach((timer) => {
        clearTimeout(timer);
      });
      timers.clear();
    };
  }, []);

  return { toasts, push, dismiss };
}
