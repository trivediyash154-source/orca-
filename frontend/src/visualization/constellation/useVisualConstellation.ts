import { useEffect, useState } from 'react';

import { demoVisualConstellation } from '../../services/demoConstellation';
import { fetchVisualConstellation } from '../../services/engineService';
import { useEngine } from '../../state/EngineContext';
import { useUI } from '../../state/UIContext';
import type { VisualConstellationSnapshot } from '../../types/engine';

export interface VisualConstellationState {
  readonly data: VisualConstellationSnapshot | null;
  readonly loading: boolean;
  readonly error: string | null;
  /** Wall-clock round trip including the engine's own process spawn -- reported so the UI
   * can show a real cost rather than an estimate. */
  readonly fetchMs: number | null;
}

const EMPTY: VisualConstellationState = { data: null, loading: false, error: null, fetchMs: null };

/** Loads the synthetic visual constellation whenever the size setting changes (Phase 12
 * brief §1/§19). Uses the `visualShell` family specifically: the planning-tuned families
 * (`dense`, etc.) deliberately cap inclination, which renders as a flat equatorial band
 * rather than a shell -- see `ScenarioGenerator.hpp`'s own note on `visualShell`. */
export function useVisualConstellation(): VisualConstellationState {
  const { baseUrl, dataSource } = useEngine();
  const { constellationSize } = useUI();
  const [state, setState] = useState<VisualConstellationState>(EMPTY);

  useEffect(() => {
    if (constellationSize === 0) {
      setState(EMPTY);
      return;
    }
    let cancelled = false;
    setState((s) => ({ ...s, loading: true, error: null }));
    const started = performance.now();

    // With no engine there is no process to ask, so the browser model generates the same
    // shape locally. Without this branch Mission Control captioned 5,000 objects over an
    // empty sky on every static deployment.
    const request =
      dataSource === 'demo'
        ? Promise.resolve(demoVisualConstellation(constellationSize, 1, 0))
        : fetchVisualConstellation(baseUrl, 'visualShell', constellationSize, 1, 0);

    request
      .then((data) => {
        if (cancelled) return;
        setState({ data, loading: false, error: null, fetchMs: performance.now() - started });
      })
      .catch((err: unknown) => {
        if (cancelled) return;
        setState({
          data: null,
          loading: false,
          error: err instanceof Error ? err.message : String(err),
          fetchMs: null,
        });
      });
    return () => {
      cancelled = true;
    };
  }, [baseUrl, constellationSize, dataSource]);

  return state;
}
