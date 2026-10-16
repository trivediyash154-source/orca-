import { useEngine } from '../state/EngineContext';

/** Brief §56: never leave a frozen dashboard pretending the simulation is running -- an
 * unreachable/unready engine gets an explicit, honest overlay instead. Polling (already
 * running in `EngineProvider`) IS the retry -- no separate manual action is required for it
 * to recover once the engine comes back. */
export function EngineOfflineOverlay() {
  const { connectionStatus, lastError } = useEngine();
  if (connectionStatus === 'Connected') return null;

  const heading = connectionStatus === 'EngineStarting' ? 'ORCA ENGINE STARTING' : 'ORCA ENGINE OFFLINE';
  const body =
    connectionStatus === 'EngineStarting'
      ? 'Waiting for the simulation engine to become ready.'
      : connectionStatus === 'Error'
        ? 'The engine cannot be used safely (crashed, or an incompatible version). Retrying automatically.'
        : 'The simulation engine is not responding. Retrying automatically.';

  return (
    <div className="engine-offline-overlay" role="alert">
      <h1>{heading}</h1>
      <p>{body}</p>
      {lastError && <p className="muted">{lastError}</p>}
    </div>
  );
}
