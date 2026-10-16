/**
 * The browser model's contract: it must be deterministic, it must satisfy the same snapshot
 * schema the real engine does, and it must not report a state that the engine could never
 * produce (a mission with no legs, a fuel fraction above 1, a Pareto front with no
 * recommendation).
 */

import { describe, expect, it } from 'vitest';

import { demoSnapshot } from '../src/services/demoEngine';

const AT = (timeSec: number) =>
  demoSnapshot({ timeSec, status: 'RUNNING' as const, speedMultiplier: 1 });

describe('demoEngine', () => {
  it('is deterministic: the same simulation second yields the same snapshot', () => {
    expect(JSON.stringify(AT(7_200))).toBe(JSON.stringify(AT(7_200)));
  });

  it('matches the schema version the frontend expects', () => {
    expect(AT(7_200).schemaVersion).toBe(1);
  });

  it('names itself as a model, so no consumer can mistake it for engine output', () => {
    expect(AT(7_200).scenarioName).toMatch(/not the C\+\+ engine/i);
  });

  it('advances: entities move and the clock is reported back', () => {
    const early = AT(7_200);
    const later = AT(7_800);

    expect(later.simulationTimeSec).toBe(7_800);
    expect(later.satellites[0]?.position.x).not.toBe(early.satellites[0]?.position.x);
  });

  it('keeps every satellite on its stated orbit', () => {
    for (const satellite of AT(9_000).satellites) {
      const { x, y, z } = satellite.position;
      const radius = Math.sqrt(x * x + y * y + z * z);
      // Circular motion: the radius must equal Earth radius + altitude at every instant.
      expect(radius).toBeCloseTo(6_371 + satellite.orbit.altitudeKm, 3);
    }
  });

  it('reports fractions inside [0, 1] and non-negative budgets', () => {
    const snapshot = AT(11_000);
    for (const vehicle of snapshot.rescueVehicles) {
      expect(vehicle.fuelFraction).toBeGreaterThanOrEqual(0);
      expect(vehicle.fuelFraction).toBeLessThanOrEqual(1);
      expect(vehicle.deltaVRemainingKms).toBeGreaterThanOrEqual(0);
      expect(vehicle.deltaVRemainingKms).toBeLessThanOrEqual(vehicle.deltaVBudgetKms);
    }
    for (const satellite of snapshot.satellites) {
      for (const reading of Object.values(satellite.health)) {
        expect(reading).toBeGreaterThanOrEqual(0);
        expect(reading).toBeLessThanOrEqual(1);
      }
    }
  });

  it('has a populated queue and missions in flight at the default start time', () => {
    const snapshot = AT(7_200);
    expect(snapshot.emergencies.length).toBeGreaterThan(0);
    expect(snapshot.missions.length).toBeGreaterThan(0);
  });

  it('gives every mission real legs and a front with exactly one recommendation', () => {
    for (const mission of AT(9_600).missions) {
      expect(mission.legs.length).toBeGreaterThan(0);
      expect(mission.paretoFront.length).toBeGreaterThan(0);
      expect(mission.paretoFront.filter((point) => point.recommended)).toHaveLength(1);

      // The recommended plan is never one that some other plan beats on every objective.
      const recommended = mission.paretoFront.find((point) => point.recommended);
      expect(recommended?.dominated).toBe(false);
    }
  });

  it('marks a point dominated only when another really dominates it', () => {
    for (const mission of AT(9_600).missions) {
      for (const point of mission.paretoFront) {
        const dominators = mission.paretoFront.filter((other) => {
          if (other.planId === point.planId) return false;
          const a = other.cost;
          const b = point.cost;
          return (
            a.time <= b.time &&
            a.fuel <= b.fuel &&
            a.risk <= b.risk &&
            (a.time < b.time || a.fuel < b.fuel || a.risk < b.risk)
          );
        });
        expect(point.dominated).toBe(dominators.length > 0);
      }
    }
  });

  it('reports only events that have already happened, newest first', () => {
    const snapshot = AT(8_400);
    for (const event of snapshot.recentEvents) {
      expect(event.timestampSec).toBeLessThanOrEqual(snapshot.simulationTimeSec);
    }
    const times = snapshot.recentEvents.map((event) => event.timestampSec);
    expect([...times].sort((a, b) => b - a)).toEqual(times);
  });

  it('never marks an edge feasible that no vehicle could fly', () => {
    const snapshot = AT(7_200);
    const largestBudget = Math.max(...snapshot.rescueVehicles.map((v) => v.deltaVBudgetKms));
    for (const edge of snapshot.graph.edges) {
      if (edge.feasible) expect(edge.cost.fuel).toBeLessThanOrEqual(largestBudget);
    }
  });

  it('reports A* expanding no more nodes than Dijkstra on the same query', () => {
    const runs = AT(9_600).algorithmRuns;
    const dijkstra = runs.find((run) => run.algorithm === 'DIJKSTRA');
    const astar = runs.find((run) => run.algorithm === 'ASTAR');

    if (dijkstra?.nodesExpanded != null && astar?.nodesExpanded != null) {
      expect(astar.nodesExpanded).toBeLessThanOrEqual(dijkstra.nodesExpanded);
    }
  });

  it('leaves a counter null rather than zero where it does not apply', () => {
    const hungarian = AT(9_600).algorithmRuns.find((run) => run.algorithm === 'HUNGARIAN');
    // A matching solver has no graph frontier; reporting 0 would read as "expanded nothing".
    expect(hungarian?.nodesExpanded).toBeNull();
    expect(hungarian?.solverMatrixDimension).toBeGreaterThan(0);
  });
});
