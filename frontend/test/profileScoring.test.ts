import { describe, expect, it } from 'vitest';

import { recommendedPlanId, rescoreFront } from '../src/services/profileScoring';
import type { ParetoPointView } from '../src/types/engine';

const point = (planId: string, time: number, fuel: number, risk: number, dominated = false): ParetoPointView => ({
  planId,
  cost: { time, fuel, risk, distance: 1_000 },
  dominated,
  recommended: false,
});

const front = [
  point('FAST', 900, 2.0, 0.3),
  point('THRIFTY', 4_000, 0.8, 0.3),
  point('SAFE', 2_500, 1.4, 0.05),
  point('BAD', 5_000, 2.5, 0.5, true),
];

describe('profileScoring', () => {
  it('picks the plan each profile is named for', () => {
    expect(recommendedPlanId(front, 'FASTEST')).toBe('FAST');
    expect(recommendedPlanId(front, 'FUEL_SAVING')).toBe('THRIFTY');
    expect(recommendedPlanId(front, 'SAFETY_FIRST')).toBe('SAFE');
  });

  it('never recommends a dominated plan', () => {
    for (const profile of ['FASTEST', 'FUEL_SAVING', 'SAFETY_FIRST', 'BALANCED']) {
      expect(recommendedPlanId(front, profile)).not.toBe('BAD');
    }
  });

  it('marks exactly one point recommended and leaves costs untouched', () => {
    const rescored = rescoreFront(front, 'BALANCED');
    expect(rescored.filter((p) => p.recommended)).toHaveLength(1);
    expect(rescored.map((p) => p.cost)).toEqual(front.map((p) => p.cost));
  });
});
