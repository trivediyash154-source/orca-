/**
 * The rescue operation — ORCA's actual subject, told as one continuous story.
 *
 * ## Why this component exists
 *
 * Everything needed to explain a rescue was already in the snapshot, scattered across panels
 * that each showed a fragment: an emergency list here, a mission table there, a Pareto chart on
 * another screen. A reader could infer the operation only by holding four screens in their head.
 *
 * So this is not another panel. It is the operator's question list, answered in the order the
 * questions actually arise:
 *
 *   WHAT IS IN DANGER, AND HOW URGENT?     -> the emergency, ranked by the engine's own urgency
 *   WHAT DID ORCA DECIDE, AND WHY?         -> the recommended plan, with its objective costs
 *   WHY NOT THE ALTERNATIVES?              -> the other candidates the engine evaluated
 *   WHAT IS THE MISSION DOING?             -> legs, progress, replans
 *   DID IT WORK?                           -> the outcome
 *
 * ## Every claim traces to engine state
 *
 * The plan marked `recommended` is the engine's choice, not this component's. The alternatives
 * are the rest of that mission's Pareto front, with the four objective costs the C++ optimiser
 * actually computes (`ObjectiveVector.hpp`: time, fuel, risk, distance — all minimised). Where
 * the engine does not compute something, this says so rather than filling the gap.
 */

import { useCallback, useMemo, useRef, useState } from 'react';

import { useEngine } from '../../state/EngineContext';
import type { EmergencyView, MissionView, ParetoPointView } from '../../types/engine';

/** `01:12:09` from engine seconds. */
function clock(seconds: number): string {
  if (!Number.isFinite(seconds)) return '—';
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = Math.floor(seconds % 60);
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${pad(h)}:${pad(m)}:${pad(s)}`;
}

function humanKind(kind: string): string {
  return kind
    .split('_')
    .map((w) => w.charAt(0) + w.slice(1).toLowerCase())
    .join(' ');
}

/** The four objectives the C++ engine actually minimises, with the units it reports them in. */
const OBJECTIVES = [
  { key: 'time' as const, label: 'Transfer time', unit: 's', digits: 0 },
  { key: 'fuel' as const, label: 'Fuel', unit: 'fraction of Δv budget', digits: 4 },
  { key: 'risk' as const, label: 'Risk', unit: 'index', digits: 4 },
  { key: 'distance' as const, label: 'Path length', unit: 'km', digits: 0 },
];

type DemoPhase = 'idle' | 'resetting' | 'running' | 'done' | 'failed';

export function RescueOperations() {
  const { snapshot, sendCommand, advance, select } = useEngine();
  const [demoPhase, setDemoPhase] = useState<DemoPhase>('idle');
  const [demoNote, setDemoNote] = useState<string | null>(null);
  const cancelled = useRef(false);

  /**
   * Runs a rescue through the real engine: reset to t=0, start, then advance in steps so the
   * operation unfolds on screen rather than jumping to its end state. Nothing here fabricates a
   * result — the engine decides what happens, and this only drives its clock.
   */
  const runDemo = useCallback(async () => {
    cancelled.current = false;
    setDemoPhase('resetting');
    setDemoNote('Resetting the scenario to t = 0.');
    try {
      await sendCommand('reset');
      await sendCommand('start');
      setDemoPhase('running');
      // Six hours in twelve half-hour steps: enough simulated time for the seeded failure
      // schedule to fire and for the planner to dispatch and complete a mission.
      const stepSec = 1800;
      for (let step = 0; step < 12; step++) {
        if (cancelled.current) return;
        setDemoNote(`Advancing the simulation — ${clock((step + 1) * stepSec)} elapsed.`);
        await advance(stepSec);
      }
      if (cancelled.current) return;
      setDemoPhase('done');
      setDemoNote('Six hours simulated. The operation below is the engine’s own result.');
    } catch (err) {
      if (cancelled.current) return;
      setDemoPhase('failed');
      setDemoNote(
        err instanceof Error
          ? `The engine did not complete the run: ${err.message}`
          : 'The engine did not complete the run.',
      );
    }
  }, [sendCommand, advance]);

  /** The operation currently worth the operator's attention. */
  const operation = useMemo(() => {
    if (!snapshot) return null;

    const missionByEmergency = new Map<string, MissionView>();
    for (const mission of snapshot.missions) {
      if (mission.emergencyId) missionByEmergency.set(mission.emergencyId, mission);
    }

    // Prefer a live operation; fall back to the most recent completed one so the screen tells a
    // story even after every mission has finished.
    const active = snapshot.missions.find(
      (m) => m.state !== 'SUCCESS' && m.state !== 'FAILED' && m.state !== 'ABORTED',
    );
    const finished = [...snapshot.missions]
      .filter((m) => m.state === 'SUCCESS' || m.state === 'FAILED' || m.state === 'ABORTED')
      .sort((a, b) => (b.completedAtSec ?? 0) - (a.completedAtSec ?? 0))[0];
    const mission = active ?? finished ?? null;

    // The most pressing unserved emergency, by the engine's own urgency.
    const unserved = [...snapshot.emergencies]
      .filter((e) => e.state !== 'RESOLVED' && e.state !== 'EXPIRED' && !missionByEmergency.has(e.id))
      .sort((a, b) => b.urgency - a.urgency)[0] ?? null;

    const emergency: EmergencyView | null =
      (mission && snapshot.emergencies.find((e) => e.id === mission.emergencyId)) ?? unserved;

    const vehicle = mission ? snapshot.rescueVehicles.find((v) => v.id === mission.vehicleId) ?? null : null;
    const front = mission?.paretoFront ?? [];
    const chosen = front.find((p) => p.recommended) ?? null;
    const alternatives = front.filter((p) => !p.recommended);

    return { mission, emergency, vehicle, chosen, alternatives, unserved, missionByEmergency };
  }, [snapshot]);

  if (!snapshot) {
    return (
      <section className="rescue-ops" aria-label="Rescue operation">
        <h2>Rescue operation</h2>
        <p className="muted">Waiting for the first engine snapshot.</p>
      </section>
    );
  }

  const busy = demoPhase === 'resetting' || demoPhase === 'running';
  const op = operation;

  return (
    <section className="rescue-ops" aria-label="Rescue operation">
      <header className="rescue-ops-head">
        <h2>Rescue operation</h2>
        <button type="button" className="rescue-demo-button" onClick={() => void runDemo()} disabled={busy}>
          {busy ? 'Running…' : 'Run rescue demo'}
        </button>
      </header>

      {demoNote !== null && (
        <p className={`rescue-demo-note${demoPhase === 'failed' ? ' is-error' : ''}`} role="status">
          {demoNote}
        </p>
      )}

      {op === null || (op.mission === null && op.emergency === null) ? (
        <p className="rescue-empty">
          No emergency has occurred yet. The scenario&apos;s failure schedule is seeded and
          time-based — run the demo, or advance the simulation, and the engine will generate one.
        </p>
      ) : (
        <ol className="rescue-story">
          {/* 1 — WHAT IS IN DANGER, AND WHY */}
          <li className="rescue-step">
            <span className="rescue-step-label">The emergency</span>
            {op.emergency === null ? (
              <p className="muted">No emergency is associated with this mission.</p>
            ) : (
              <>
                <p className="rescue-headline">
                  <button
                    type="button"
                    className="rescue-subject"
                    onClick={() => select({ kind: 'satellite', id: op.emergency!.subjectId })}
                  >
                    {op.emergency.subjectId}
                  </button>{' '}
                  suffered a <strong>{humanKind(op.emergency.kind).toLowerCase()}</strong>, rated{' '}
                  <strong>{op.emergency.severity.toLowerCase()}</strong>.
                </p>
                <dl className="rescue-facts">
                  <div>
                    <dt>Detected</dt>
                    <dd>{clock(op.emergency.detectedAtSec)}</dd>
                  </div>
                  <div>
                    <dt>Survival deadline</dt>
                    <dd>{clock(op.emergency.deadlineSec)}</dd>
                  </div>
                  <div>
                    <dt>Window left</dt>
                    <dd>
                      {op.emergency.remainingWindowSec > 0
                        ? clock(op.emergency.remainingWindowSec)
                        : 'elapsed'}
                    </dd>
                  </div>
                  <div>
                    <dt>Urgency</dt>
                    <dd>{op.emergency.urgency.toFixed(2)}</dd>
                  </div>
                </dl>
              </>
            )}
          </li>

          {/* 2 — WHAT ORCA DECIDED, AND WHY */}
          <li className="rescue-step">
            <span className="rescue-step-label">ORCA&apos;s decision</span>
            {op.mission === null ? (
              <p className="rescue-headline">
                No vehicle has been dispatched. The emergency is queued and the planner has not
                assigned it.
              </p>
            ) : (
              <>
                <p className="rescue-headline">
                  Dispatched{' '}
                  <button
                    type="button"
                    className="rescue-subject"
                    onClick={() => select({ kind: 'vehicle', id: op.mission!.vehicleId })}
                  >
                    {op.mission.vehicleId}
                  </button>
                  {op.chosen !== null && (
                    <>
                      {' '}on plan <strong>{op.chosen.planId}</strong>
                    </>
                  )}
                  .
                </p>
                {op.chosen === null ? (
                  <p className="muted">
                    The engine did not retain a scored plan for this mission, so the objective
                    values behind the choice are not available.
                  </p>
                ) : (
                  <table className="rescue-objectives">
                    <caption>
                      The four objectives the engine minimises, for the plan it recommended.
                    </caption>
                    <tbody>
                      {OBJECTIVES.map((objective) => (
                        <tr key={objective.key}>
                          <th scope="row">{objective.label}</th>
                          <td>{op.chosen!.cost[objective.key].toFixed(objective.digits)}</td>
                          <td className="rescue-unit">{objective.unit}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                )}
                {op.vehicle !== null && (
                  <p className="rescue-aside">
                    {op.mission.vehicleId} carried {op.vehicle.deltaVRemainingKms.toFixed(2)} km/s
                    of Δv when selected; this mission has spent{' '}
                    {op.mission.deltaVSpentKms.toFixed(3)} km/s.
                  </p>
                )}
              </>
            )}
          </li>

          {/* 3 — WHY NOT THE ALTERNATIVES */}
          <li className="rescue-step">
            <span className="rescue-step-label">The alternatives</span>
            {op.mission === null ? (
              <p className="muted">No plans were generated, because no mission was dispatched.</p>
            ) : op.alternatives.length === 0 ? (
              <p className="rescue-headline">
                The search returned a single non-dominated plan, so there was no trade-off to
                make: nothing else reached the front for this target.
              </p>
            ) : (
              <>
                <p className="rescue-headline">
                  {op.alternatives.length} other plan{op.alternatives.length === 1 ? '' : 's'}{' '}
                  reached the Pareto front. Each is better on some objective and worse on another —
                  that is what being on the front means.
                </p>
                <table className="rescue-alternatives">
                  <thead>
                    <tr>
                      <th scope="col">Plan</th>
                      <th scope="col">Time (s)</th>
                      <th scope="col">Fuel</th>
                      <th scope="col">Risk</th>
                      <th scope="col">Distance (km)</th>
                    </tr>
                  </thead>
                  <tbody>
                    {op.chosen !== null && (
                      <tr className="is-chosen">
                        <th scope="row">{op.chosen.planId} · chosen</th>
                        {renderCosts(op.chosen)}
                      </tr>
                    )}
                    {op.alternatives.map((plan) => (
                      <tr key={plan.planId}>
                        <th scope="row">{plan.planId}</th>
                        {renderCosts(plan)}
                      </tr>
                    ))}
                  </tbody>
                </table>
              </>
            )}
          </li>

          {/* 4 — WHAT THE MISSION IS DOING */}
          {op.mission !== null && (
            <li className="rescue-step">
              <span className="rescue-step-label">Execution</span>
              <p className="rescue-headline">
                Mission {op.mission.id} is <strong>{op.mission.state.toLowerCase()}</strong>, on leg{' '}
                {op.mission.currentLeg + 1} of {op.mission.legs.length}.
              </p>
              <ul className="rescue-legs">
                {op.mission.legs.map((leg, index) => (
                  <li key={`${leg.fromEntityId}-${leg.toEntityId}-${index}`} className={`leg-${leg.status}`}>
                    <span className="leg-route">
                      {leg.fromEntityId} → {leg.toEntityId}
                    </span>
                    <span className="leg-detail">
                      depart {clock(leg.departureTimeSec)} · arrive {clock(leg.arrivalTimeSec)} ·{' '}
                      {leg.deltaVKms.toFixed(3)} km/s
                    </span>
                    <span className="leg-status">{leg.status}</span>
                  </li>
                ))}
              </ul>
              <p className="rescue-aside">
                {op.mission.replanCount === 0
                  ? 'No replan was required: the route held for the whole transfer.'
                  : `ORCA replanned ${op.mission.replanCount} time${op.mission.replanCount === 1 ? '' : 's'} after the route was invalidated in flight.`}
              </p>
            </li>
          )}

          {/* 5 — DID IT WORK */}
          {op.mission !== null && (
            <li className="rescue-step">
              <span className="rescue-step-label">Outcome</span>
              <p className={`rescue-outcome outcome-${op.mission.state.toLowerCase()}`}>
                {op.mission.state === 'SUCCESS'
                  ? `${op.emergency?.subjectId ?? 'The target'} was rescued.`
                  : op.mission.state === 'FAILED'
                    ? `The mission failed; ${op.emergency?.subjectId ?? 'the target'} was not recovered.`
                    : op.mission.state === 'ABORTED'
                      ? 'The mission was aborted.'
                      : 'The mission is still under way.'}
              </p>
              {op.mission.completedAtSec !== null && (
                <p className="rescue-aside">
                  Started {clock(op.mission.startedAtSec)}, completed{' '}
                  {clock(op.mission.completedAtSec)} — {clock(op.mission.completedAtSec - op.mission.startedAtSec)}{' '}
                  elapsed.
                </p>
              )}
            </li>
          )}
        </ol>
      )}
    </section>
  );
}

/** The four objective costs as table cells, in the engine's own order. */
function renderCosts(plan: ParetoPointView) {
  return (
    <>
      <td>{plan.cost.time.toFixed(0)}</td>
      <td>{plan.cost.fuel.toFixed(4)}</td>
      <td>{plan.cost.risk.toFixed(4)}</td>
      <td>{plan.cost.distance.toFixed(0)}</td>
    </>
  );
}
