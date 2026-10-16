import { useState } from 'react';

import { ParetoChart } from '../../charts/ParetoChart';
import { useRelevantMission } from '../../components/mission/MissionIntelligence';
import { useEngine } from '../../state/EngineContext';

/** The exact profile names `cpp/app/main.cpp`'s `resolveNamedProfile()` recognizes -- no
 * other string is a valid `rescoreParetoFront` command (brief §50). */
const PROFILES = [
  { name: 'FASTEST', label: 'Fastest' },
  { name: 'FUEL_SAVING', label: 'Fuel Saving' },
  { name: 'SAFETY_FIRST', label: 'Safety First' },
  { name: 'BALANCED', label: 'Balanced' },
] as const;

/** Brief §50-54: live re-scoring against an already-captured front (no new search --
 * `MissionScorer::selectRecommendedPlan()` applied directly by the engine) and Pareto
 * history across every retained planning pass. */
export function OptimizationLabPage() {
  const mission = useRelevantMission();
  const { rescoreParetoFront, lastError } = useEngine();
  const [pending, setPending] = useState<string | null>(null);

  const handleRescore = (profileName: string) => {
    if (!mission) return;
    setPending(profileName);
    void rescoreParetoFront(mission.id, profileName).finally(() => setPending(null));
  };

  return (
    <div className="optimization-lab-page">
      <section className="lab-panel">
        <h2>Pareto Front</h2>
        {mission ? (
          <>
            <p className="muted">
              Mission {mission.id} — {mission.paretoFront.length} candidate plan
              {mission.paretoFront.length === 1 ? '' : 's'} in the current planning pass.
              {mission.activeRescoreProfile &&
                ` Recommendation re-scored for the "${mission.activeRescoreProfile}" profile.`}
            </p>
            <ParetoChart points={mission.paretoFront} />
          </>
        ) : (
          <p className="muted">No mission with a captured Pareto front is currently active.</p>
        )}
      </section>

      <aside className="lab-panel lab-profiles">
        <h2>Profile</h2>
        <div className="profile-buttons">
          {PROFILES.map((p) => (
            <button
              key={p.name}
              disabled={!mission || pending !== null}
              aria-busy={pending === p.name}
              onClick={() => handleRescore(p.name)}
              title="Re-select the recommended plan on the current front (no new search)"
            >
              {p.label}
            </button>
          ))}
        </div>
        <p className="muted">
          Re-scoring applies {"MissionScorer::selectRecommendedPlan()"} directly to the mission&apos;s
          already-captured front — it never re-runs a search, and the front&apos;s own points/costs
          never change; only which point is marked recommended can change.
        </p>
        {lastError && <p className="lab-error">{lastError}</p>}
      </aside>

      {mission && mission.paretoHistory.length > 0 && (
        <section className="lab-panel lab-history">
          <h2>Planning History</h2>
          <p className="muted">
            Every retained planning pass for this mission (brief §52-54) — pass 1 is the original
            dispatch-time front; a later pass, if any, comes from a local replan and honestly
            carries only the single plan actually adopted, not a re-derived multi-point front.
          </p>
          <ul className="pareto-history-list">
            {mission.paretoHistory.map((entry) => (
              <li key={entry.planningPass}>
                <span className="pareto-history-pass">Pass {entry.planningPass}</span>
                <span className="pareto-history-time">t = {entry.timestampSec.toFixed(1)}s</span>
                <span className="pareto-history-count">
                  {entry.points.length} point{entry.points.length === 1 ? '' : 's'}
                </span>
              </li>
            ))}
          </ul>
        </section>
      )}
    </div>
  );
}
