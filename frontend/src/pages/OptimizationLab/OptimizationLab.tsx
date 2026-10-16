import { useState } from 'react';

import { ParetoChart } from '../../charts/ParetoChart';
import { useRelevantMission } from '../../components/mission/MissionIntelligence';
import { PROFILE_WEIGHTS, recommendedPlanId, scorePoints } from '../../services/profileScoring';
import { useEngine } from '../../state/EngineContext';
import { ConstellationNetwork } from './ConstellationNetwork';

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
  const relevant = useRelevantMission();
  const { snapshot, select, rescoreParetoFront, lastError } = useEngine();

  // `useRelevantMission` declines to guess when several missions are in flight, which is right
  // for Mission Intelligence's narrative but left this page blank whenever the fleet was busy.
  // Here the reader can choose, so the page falls back to the first in-flight mission with a
  // front and offers the rest in a picker.
  const withFront = (snapshot?.missions ?? []).filter((m) => m.paretoFront.length > 0);
  const mission =
    relevant ??
    withFront.find((m) => m.completedAtSec === null) ??
    withFront[0] ??
    null;
  const [pending, setPending] = useState<string | null>(null);

  const handleRescore = (profileName: string) => {
    if (!mission) return;
    setPending(profileName);
    void rescoreParetoFront(mission.id, profileName).finally(() => setPending(null));
  };

  return (
    <div className="optimization-lab-page">
      <section className="lab-panel">
        <div className="lab-panel__head">
          <h2>Pareto Front</h2>
          {withFront.length > 1 ? (
            <label className="lab-picker">
              Mission
              <select
                value={mission?.id ?? ''}
                onChange={(event) => select({ kind: 'mission', id: event.target.value })}
              >
                {withFront.map((m) => (
                  <option key={m.id} value={m.id}>
                    {m.id} · {m.vehicleId} → {m.emergencyId} ({m.state.toLowerCase().replace(/_/g, ' ')})
                  </option>
                ))}
              </select>
            </label>
          ) : null}
        </div>
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

      {mission && mission.paretoFront.length > 1 && (
        <section className="lab-panel lab-compare">
          <h2>What each profile would choose</h2>
          <p className="muted">
            The same front, scored under all four profiles at once with the engine&apos;s own weights
            (time / fuel / risk / distance). A plan that wins under several profiles is a robust
            choice; a front where every profile picks a different plan is one where the operator&apos;s
            priorities genuinely decide the outcome.
          </p>
          <div className="lab-compare__scroll">
            <table className="dash-table">
              <thead>
                <tr>
                  <th scope="col">Plan</th>
                  <th scope="col">Time (s)</th>
                  <th scope="col">Fuel</th>
                  <th scope="col">Risk</th>
                  <th scope="col">Distance (km)</th>
                  {PROFILES.map((p) => (
                    <th scope="col" key={p.name}>
                      {p.label}
                    </th>
                  ))}
                </tr>
              </thead>
              <tbody>
                {mission.paretoFront.map((point) => (
                  <tr key={point.planId} className={point.dominated ? 'lab-compare__dominated' : undefined}>
                    <td className="mono">
                      {point.planId}
                      {point.dominated ? <span className="lab-compare__tag">dominated</span> : null}
                    </td>
                    <td className="mono">{Math.round(point.cost.time)}</td>
                    <td className="mono">{point.cost.fuel.toFixed(3)}</td>
                    <td className="mono">{point.cost.risk.toFixed(3)}</td>
                    <td className="mono">{Math.round(point.cost.distance).toLocaleString()}</td>
                    {PROFILES.map((p) => {
                      const winner = recommendedPlanId(mission.paretoFront, p.name) === point.planId;
                      const score = scorePoints(mission.paretoFront, p.name).get(point.planId);
                      return (
                        <td key={p.name} className={`mono${winner ? ' lab-compare__win' : ''}`}>
                          {score === undefined ? '—' : score.toFixed(3)}
                          {winner ? <span aria-label="chosen"> ★</span> : null}
                        </td>
                      );
                    })}
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <p className="muted lab-compare__weights">
            {PROFILES.map((p) => {
              const w = PROFILE_WEIGHTS[p.name]!;
              return (
                <span key={p.name}>
                  <strong>{p.label}</strong> {w.join(' / ')}
                </span>
              );
            })}
          </p>
        </section>
      )}

      <ConstellationNetwork />

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
