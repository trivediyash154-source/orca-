import { useEngine } from '../../state/EngineContext';
import { BenchmarkEvidence } from '../../components/analytics/BenchmarkEvidence';
import { eventCategory } from '../../components/timeline/eventCategories';
import { formatPercent } from '../../utils/format';

const CATEGORY_LABEL: Record<string, string> = {
  environmental: 'Environmental',
  emergency: 'Emergency',
  mission: 'Mission',
  planning: 'Planning',
  success: 'Success',
  failure: 'Failure',
};
const CATEGORY_ORDER = ['emergency', 'planning', 'mission', 'environmental', 'success', 'failure'];

/** Brief §46: real metrics only -- `snapshot.metrics` directly, no fabricated historical
 * series (no time-series data is retained by the engine yet, so no chart is drawn rather
 * than inventing one, per brief §61-62). The category breakdown below is real too -- a
 * count over `snapshot.recentEvents` (the same bounded, real event log the Mission Control
 * timeline reads), not a time series, so it stays honest about what the engine actually
 * retains (Phase 12 brief §5: fill real space with real data rather than leaving it empty). */
export function AnalyticsPage() {
  const { snapshot } = useEngine();

  if (!snapshot) {
    return <div className="page-placeholder">No snapshot yet.</div>;
  }

  const { metrics } = snapshot;
  const totalOutcomes = metrics.missionSuccesses + metrics.missionFailures + metrics.missionAborts;
  const successRate = totalOutcomes > 0 ? metrics.missionSuccesses / totalOutcomes : null;

  const cards: Array<{ label: string; value: string }> = [
    { label: 'Rescues', value: String(metrics.missionSuccesses) },
    { label: 'Success Rate', value: successRate !== null ? formatPercent(successRate) : 'N/A' },
    { label: 'Replans Triggered', value: String(metrics.replansTriggered) },
    { label: 'Replans Completed', value: String(metrics.replansCompleted) },
    { label: 'Active Missions', value: String(snapshot.missions.filter((m) => m.state === 'IN_TRANSIT').length) },
    { label: 'Events Processed', value: String(metrics.eventsProcessed) },
    { label: 'Ticks', value: String(metrics.ticks) },
    { label: 'Mission Aborts', value: String(metrics.missionAborts) },
  ];

  const categoryCounts = new Map<string, number>();
  for (const event of snapshot.recentEvents) {
    const cat = eventCategory(event.kind);
    categoryCounts.set(cat, (categoryCounts.get(cat) ?? 0) + 1);
  }
  const maxCount = Math.max(1, ...categoryCounts.values());

  return (
    <div className="analytics-page">
      <h2 className="analytics-section-label">Live Session Data</h2>
      <div className="metric-cards">
        {cards.map((c) => (
          <div key={c.label} className="metric-card">
            <span className="metric-card-value">{c.value}</span>
            <span className="metric-card-label">{c.label}</span>
          </div>
        ))}
      </div>

      {snapshot.recentEvents.length > 0 && (
        <section className="analytics-breakdown">
          <h2>Events by Category (retained log, last {snapshot.recentEvents.length})</h2>
          <div className="analytics-bars">
            {CATEGORY_ORDER.filter((cat) => categoryCounts.has(cat)).map((cat) => {
              const count = categoryCounts.get(cat) ?? 0;
              return (
                <div key={cat} className="analytics-bar-row">
                  <span className="analytics-bar-label">{CATEGORY_LABEL[cat]}</span>
                  <div className="analytics-bar-track">
                    <div
                      className={`analytics-bar-fill analytics-bar-fill-${cat}`}
                      style={{ width: `${(count / maxCount) * 100}%` }}
                    />
                  </div>
                  <span className="analytics-bar-count">{count}</span>
                </div>
              );
            })}
          </div>
        </section>
      )}

      <p className="muted">
        Time-series charts (rescue time, fuel, risk trends) require historical persistence
        not yet implemented by the engine — shown here only as current-session totals.
      </p>

      <h2 className="analytics-section-label analytics-section-offline">Offline Benchmark Evidence</h2>
      <BenchmarkEvidence />
    </div>
  );
}
