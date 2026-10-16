import { useEngine } from '../state/EngineContext';

/**
 * Says, on every workspace page, which process produced the numbers on screen.
 *
 * It renders only in demo mode. With the real engine attached there is nothing to disclose --
 * the top bar's "Engine: CONNECTED" already covers it, and a permanent banner would be noise
 * that trains the reader to ignore banners.
 *
 * One line, with the full explanation behind a native disclosure. The first version printed
 * the whole paragraph, which cost Mission Control a sixth of its height on a laptop and most of
 * the screen on a phone -- disclosure that crowds out the thing it is disclosing about.
 */
export function DataSourceBanner() {
  const { dataSource } = useEngine();
  if (dataSource !== 'demo') return null;

  return (
    <details className="dash-source-banner">
      <summary>
        <strong>Browser model</strong>
        <span className="dash-source-banner__short">
          No C++ engine reachable — running the in-browser kinematic model.
        </span>
        <span className="dash-source-banner__more" aria-hidden="true">
          What does that mean?
        </span>
      </summary>
      <p>
        The workspace normally renders state from the C++ engine through the Node bridge. With
        neither reachable — as on any static deployment — it runs an in-browser kinematic model
        instead: real closed-form circular-orbit motion, a recurring emergency arrival process,
        representative mission and Pareto data, and algorithm counters replayed rather than
        searched. Run the engine and the bridge locally and the frontend switches to the real
        thing automatically, with no setting to change.
      </p>
    </details>
  );
}
