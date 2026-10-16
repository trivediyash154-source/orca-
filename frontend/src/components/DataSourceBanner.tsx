import { useEngine } from '../state/EngineContext';

/**
 * Says, on every workspace page, which process produced the numbers on screen.
 *
 * It renders only in demo mode. With the real engine attached there is nothing to disclose --
 * the top bar's "Engine: CONNECTED" already covers it, and a permanent banner would be noise
 * that trains the reader to ignore banners.
 *
 * The wording names what the browser model does and does not do, because the difference is
 * material to anyone reading the charts: the orbital motion and the trade-off surfaces are
 * representative, and the searches themselves are not running.
 */
export function DataSourceBanner() {
  const { dataSource } = useEngine();
  if (dataSource !== 'demo') return null;

  return (
    <div className="dash-source-banner" role="status">
      <strong>Browser model</strong>
      <span>
        No C++ engine is reachable from this page, so the workspace is running its in-browser
        kinematic model: real circular-orbit motion, representative mission and Pareto data, and
        algorithm counters replayed rather than searched. Run the engine and the Node bridge
        locally for the real thing — the frontend switches to it automatically.
      </span>
    </div>
  );
}
