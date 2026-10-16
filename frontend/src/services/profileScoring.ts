/**
 * The engine's weighted plan selection (`MissionScorer::selectRecommendedPlan`), reproduced for
 * the browser model.
 *
 * Weights are copied from `cpp/optimization/OptimizationProfile.cpp` and must be kept in step
 * with it by hand, like every other contract between the two layers. The order is (time, fuel,
 * risk, distance).
 *
 * Selection is over the non-dominated points only — a dominated plan is never recommended
 * under any weighting, because some other plan beats it on every objective at once. Each
 * objective is min-max normalised across those points first, so a weight means "how much this
 * objective's spread matters" rather than "how big its units happen to be": without it, transfer
 * time in seconds would swamp fuel in km/s under every profile.
 */

import type { ObjectiveVectorView, ParetoPointView } from '../types/engine';

export const PROFILE_WEIGHTS: Readonly<Record<string, readonly [number, number, number, number]>> = {
  FASTEST: [0.7, 0.1, 0.15, 0.05],
  FUEL_SAVING: [0.1, 0.7, 0.15, 0.05],
  SAFETY_FIRST: [0.15, 0.1, 0.7, 0.05],
  BALANCED: [0.3, 0.3, 0.3, 0.1],
};

const KEYS: readonly (keyof ObjectiveVectorView)[] = ['time', 'fuel', 'risk', 'distance'];

export function scorePoints(
  points: readonly ParetoPointView[],
  profile: string,
): Map<string, number> {
  const weights = PROFILE_WEIGHTS[profile] ?? PROFILE_WEIGHTS.BALANCED!;
  const pool = points.filter((point) => !point.dominated);
  const candidates = pool.length > 0 ? pool : points;

  const ranges = KEYS.map((key) => {
    const values = candidates.map((point) => point.cost[key]);
    return { min: Math.min(...values), max: Math.max(...values) };
  });

  const scores = new Map<string, number>();
  for (const point of candidates) {
    let score = 0;
    KEYS.forEach((key, i) => {
      const { min, max } = ranges[i]!;
      const normalised = max === min ? 0 : (point.cost[key] - min) / (max - min);
      score += weights[i]! * normalised;
    });
    scores.set(point.planId, score);
  }
  return scores;
}

/** The lowest score wins; ties go to the lexicographically smaller plan id so the choice is
 * deterministic, mirroring the engine's tie-break chain. */
export function recommendedPlanId(points: readonly ParetoPointView[], profile: string): string | null {
  const scores = scorePoints(points, profile);
  let best: string | null = null;
  let bestScore = Number.POSITIVE_INFINITY;
  for (const [planId, score] of scores) {
    if (score < bestScore || (score === bestScore && best !== null && planId < best)) {
      best = planId;
      bestScore = score;
    }
  }
  return best;
}

export function rescoreFront(points: readonly ParetoPointView[], profile: string): ParetoPointView[] {
  const winner = recommendedPlanId(points, profile);
  return points.map((point) => ({ ...point, recommended: point.planId === winner }));
}
