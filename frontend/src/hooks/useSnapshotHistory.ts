/**
 * A rolling window of past snapshots, so the dashboard can draw a line rather than a number.
 *
 * The engine's contract is a point-in-time snapshot: it reports what is true now and keeps no
 * series. A trend chart therefore needs a short client-side history, and this is the one place
 * that keeps one. Two rules make that safe:
 *
 *  1. Nothing is ever *derived* here that the engine could have reported. Each sample is a
 *     verbatim copy of values the snapshot already carried, tagged with the simulation time the
 *     snapshot itself declared -- never with wall-clock time, which would misplot every sample
 *     while the simulation is paused or running at a non-unity speed.
 *  2. The buffer resets when the simulation clock goes backwards (a reset, a scenario reload, a
 *     reconnect to a fresh engine). Splicing the old run onto the new one would draw a cliff
 *     that never happened.
 */

import { useEffect, useRef, useState } from 'react';

import type { SimulationSnapshot } from '../types/engine';

export interface HistorySample {
  readonly timeSec: number;
  readonly activeEmergencies: number;
  readonly criticalEmergencies: number;
  readonly activeMissions: number;
  readonly completedMissions: number;
  /** Mean across the fleet, in km/s -- the quantity the delta-V budget chart plots. */
  readonly deltaVRemainingMean: number;
  readonly deltaVSpentTotal: number;
  readonly fleetHealthMean: number;
  readonly satellitesNominal: number;
  readonly satellitesDegraded: number;
  readonly debrisTracked: number;
  readonly eventsProcessed: number;
  readonly replansTriggered: number;
  /** Mean of the most recent run per algorithm, in ms. `null` when nothing has run yet, which
   * the chart draws as a gap. */
  readonly searchLatencyMs: number | null;
}

/** 240 samples at the provider's 500 ms poll is two minutes of wall clock, and at the demo
 * model's 1 s tick it is four. Both are long enough to read a trend and short enough that the
 * array never costs anything measurable. */
const CAPACITY = 240;

function mean(values: readonly number[]): number {
  if (values.length === 0) return 0;
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function sampleFrom(snapshot: SimulationSnapshot): HistorySample {
  const active = snapshot.emergencies.filter(
    (entry) => entry.state !== 'RESOLVED' && entry.state !== 'EXPIRED' && entry.state !== 'UNREACHABLE',
  );
  const missions = snapshot.missions;
  const latencies = snapshot.algorithmRuns.map((run) => run.executionTimeMs);

  return {
    timeSec: snapshot.simulationTimeSec,
    activeEmergencies: active.length,
    criticalEmergencies: active.filter((entry) => entry.severity === 'CRITICAL').length,
    activeMissions: missions.filter((entry) => entry.completedAtSec === null).length,
    completedMissions: missions.filter((entry) => entry.completedAtSec !== null).length,
    deltaVRemainingMean: mean(snapshot.rescueVehicles.map((v) => v.deltaVRemainingKms)),
    deltaVSpentTotal: missions.reduce((sum, entry) => sum + entry.deltaVSpentKms, 0),
    fleetHealthMean: mean(
      snapshot.rescueVehicles.flatMap((v) => [
        v.health.power,
        v.health.propulsion,
        v.health.comms,
        v.health.thermal,
      ]),
    ),
    satellitesNominal: snapshot.satellites.filter((s) => s.status === 'NOMINAL').length,
    satellitesDegraded: snapshot.satellites.filter((s) => s.status !== 'NOMINAL').length,
    debrisTracked: snapshot.debris.filter((d) => d.detected).length,
    eventsProcessed: snapshot.metrics.eventsProcessed,
    replansTriggered: snapshot.metrics.replansTriggered,
    searchLatencyMs: latencies.length > 0 ? mean(latencies) : null,
  };
}

export function useSnapshotHistory(snapshot: SimulationSnapshot | null): readonly HistorySample[] {
  const [history, setHistory] = useState<readonly HistorySample[]>([]);
  const lastTimeRef = useRef<number | null>(null);

  useEffect(() => {
    if (snapshot === null) return;

    const time = snapshot.simulationTimeSec;
    const previous = lastTimeRef.current;

    // Same second, same sample: the provider polls faster than the clock advances when the
    // simulation is paused, and appending identical points would flatten the window into one
    // value.
    if (previous !== null && time === previous) return;

    const sample = sampleFrom(snapshot);
    lastTimeRef.current = time;

    setHistory((current) => {
      if (previous !== null && time < previous) return [sample];
      const next = [...current, sample];
      return next.length > CAPACITY ? next.slice(next.length - CAPACITY) : next;
    });
  }, [snapshot]);

  return history;
}
