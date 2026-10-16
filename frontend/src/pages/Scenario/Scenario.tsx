import { useState } from 'react';

import { ConstellationScene } from '../../visualization/constellation/ConstellationScene';
import { ProximityLegend } from '../../visualization/constellation/ProximityLegend';
import { useEngine } from '../../state/EngineContext';
import { demoVisualConstellation } from '../../services/demoConstellation';
import { fetchVisualConstellation, type ScenarioFamily } from '../../services/engineService';
import type { VisualConstellationSnapshot, VisualObjectView } from '../../types/engine';

const FAMILIES: readonly ScenarioFamily[] = [
  'sparse',
  'normal',
  'dense',
  'multiEmergency',
  'fuelConstrained',
  'highDebris',
  'tradeoffRich',
  'stress',
];

/** Phase 12 brief §19's "VISUAL CONSTELLATION SIZE" tiers -- the default/high-density modes
 * §5 asks for, real generated sizes (`cpp/simulation/ScenarioGenerator.hpp`'s own families),
 * not a fabricated round number. */
const SIZE_PRESETS = [
  { label: '2,000 (default)', value: 2000 },
  { label: '5,000 (high density)', value: 5000 },
  { label: '10,000 (stress)', value: 10000 },
] as const;

/** Brief §48/§19: a real scenario control surface. The small, fixed LIVE mission-narrative
 * scenario (`snapshot.scenarioName`) is a separate, honest concern from the large VISUAL
 * constellation below it (brief §5's own explicit "make that distinction explicit and
 * honest") -- generating a large constellation here never touches the live mission session
 * `Mission Control` is driving. */
export function ScenarioPage() {
  const { snapshot, baseUrl, dataSource } = useEngine();

  const [family, setFamily] = useState<ScenarioFamily>('dense');
  const [size, setSize] = useState(2000);
  const [seed, setSeed] = useState(1);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [constellation, setConstellation] = useState<VisualConstellationSnapshot | null>(null);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [proximityMode, setProximityMode] = useState(false);
  const [lastFetchMs, setLastFetchMs] = useState<number | null>(null);

  const counts: Array<{ label: string; value: string }> = snapshot
    ? [
        { label: 'Satellites', value: String(snapshot.satellites.length) },
        { label: 'Rescue Vehicles', value: String(snapshot.rescueVehicles.length) },
        { label: 'Debris Objects', value: String(snapshot.debris.length) },
        { label: 'Graph Nodes', value: String(snapshot.graph.nodes.length) },
        { label: 'Graph Edges', value: String(snapshot.graph.edges.length) },
      ]
    : [];

  async function generate() {
    setLoading(true);
    setError(null);
    setSelectedId(null);
    const clientStart = performance.now();
    try {
      const data =
        dataSource === 'demo'
          ? demoVisualConstellation(size, seed, 0, family)
          : await fetchVisualConstellation(baseUrl, family, size, seed, 0);
      setConstellation(data);
      setLastFetchMs(performance.now() - clientStart);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setLoading(false);
    }
  }

  function resetConstellation() {
    setConstellation(null);
    setSelectedId(null);
    setError(null);
  }

  const selectedObject: VisualObjectView | undefined =
    constellation && selectedId
      ? [...constellation.satellites, ...constellation.vehicles].find((o) => o.id === selectedId)
      : undefined;

  return (
    <div className="scenario-page">
      <div className="scenario-card scenario-card-active">
        <span className="scenario-card-name">{snapshot?.scenarioName ?? 'N/A'}</span>
        <span className="scenario-card-badge">LIVE MISSION</span>
        <p className="muted">
          A propulsion failure triggers an emergency; ORCA dispatches a rescue vehicle; a
          debris hazard forces a dynamic replan mid-mission; the rescue completes. This is the
          small, real, computationally-planned scenario Mission Control drives.
        </p>
      </div>

      {counts.length > 0 && (
        <div className="metric-cards scenario-counts">
          {counts.map((c) => (
            <div key={c.label} className="metric-card">
              <span className="metric-card-value">{c.value}</span>
              <span className="metric-card-label">{c.label}</span>
            </div>
          ))}
        </div>
      )}

      <p className="muted">
        Scenario-file loading is not yet implemented by the engine — this build always runs
        the live mission scenario above. Object counts are read directly from the live
        snapshot, not precomputed from the scenario definition.
      </p>

      <section className="constellation-lab">
        <h2>Large Visual Constellation</h2>
        <p className="muted">
          A genuinely different layer from the live mission above (brief §5): real satellite
          positions from a freshly generated scenario (`orca_engine --visualScenario`), but
          never run through the mission-planning graph — at this scale that graph would be
          O(n²) and prohibitively expensive to build for a screen that only ever draws dots.
        </p>

        <div className="constellation-controls">
          <label>
            Family
            <select value={family} onChange={(e) => setFamily(e.target.value as ScenarioFamily)}>
              {FAMILIES.map((f) => (
                <option key={f} value={f}>
                  {f}
                </option>
              ))}
            </select>
          </label>
          <label>
            Size
            <select value={size} onChange={(e) => setSize(Number(e.target.value))}>
              {SIZE_PRESETS.map((p) => (
                <option key={p.value} value={p.value}>
                  {p.label}
                </option>
              ))}
            </select>
          </label>
          <label>
            Seed
            <input type="number" min={1} value={seed} onChange={(e) => setSeed(Number(e.target.value))} />
          </label>
          <button onClick={() => void generate()} disabled={loading}>
            {loading ? 'Generating…' : 'Generate'}
          </button>
          <button onClick={resetConstellation} disabled={!constellation && !error}>
            Reset
          </button>
          {constellation && (
            <label className="constellation-proximity-toggle">
              <input
                type="checkbox"
                checked={proximityMode}
                onChange={(e) => setProximityMode(e.target.checked)}
              />
              Proximity mode
            </label>
          )}
        </div>

        {error && <p className="lab-error">Generation failed: {error}</p>}

        {constellation && (
          <>
            <div className="metric-cards constellation-stats">
              <div className="metric-card">
                <span className="metric-card-value">{constellation.satelliteCount.toLocaleString()}</span>
                <span className="metric-card-label">Satellites</span>
              </div>
              <div className="metric-card">
                <span className="metric-card-value">{constellation.vehicleCount.toLocaleString()}</span>
                <span className="metric-card-label">Vehicles</span>
              </div>
              <div className="metric-card">
                <span className="metric-card-value">{constellation.debrisCount.toLocaleString()}</span>
                <span className="metric-card-label">Debris</span>
              </div>
              <div className="metric-card">
                <span className="metric-card-value">{constellation.generationMs.toFixed(1)} ms</span>
                <span className="metric-card-label">Engine generation time</span>
              </div>
              <div className="metric-card">
                <span className="metric-card-value">{lastFetchMs !== null ? `${lastFetchMs.toFixed(0)} ms` : 'N/A'}</span>
                <span className="metric-card-label">Round-trip (incl. process spawn)</span>
              </div>
              <div className="metric-card">
                <span className="metric-card-value">{seed}</span>
                <span className="metric-card-label">Seed</span>
              </div>
            </div>

            <div className="constellation-scene-wrapper">
              <ConstellationScene
                satellites={constellation.satellites}
                vehicles={constellation.vehicles}
                selectedId={selectedId}
                onSelect={setSelectedId}
                proximityMode={proximityMode}
              />
              {proximityMode && selectedId && <ProximityLegend />}
              {!selectedId && proximityMode && (
                <p className="constellation-hint">Click an object to see proximity coloring relative to it.</p>
              )}
            </div>

            {selectedObject && (
              <div className="constellation-inspector">
                <h3>{selectedObject.name ?? selectedObject.id}</h3>
                <dl>
                  <dt>ID</dt>
                  <dd>{selectedObject.id}</dd>
                  <dt>Status</dt>
                  <dd>{selectedObject.status ?? 'Not available from current simulation model'}</dd>
                  <dt>Altitude</dt>
                  <dd>
                    {selectedObject.altitudeKm !== undefined
                      ? `${selectedObject.altitudeKm.toFixed(0)} km`
                      : 'Not available from current simulation model'}
                  </dd>
                  <dt>Inclination</dt>
                  <dd>
                    {selectedObject.inclinationDeg !== undefined
                      ? `${selectedObject.inclinationDeg.toFixed(1)}°`
                      : 'Not available from current simulation model'}
                  </dd>
                  <dt>Band</dt>
                  <dd>{selectedObject.band ?? 'Not available from current simulation model'}</dd>
                </dl>
              </div>
            )}
          </>
        )}

        {!constellation && !loading && !error && (
          <p className="muted">Click Generate to load a real, freshly generated constellation.</p>
        )}
      </section>
    </div>
  );
}
