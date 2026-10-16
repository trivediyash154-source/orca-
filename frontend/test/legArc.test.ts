import { describe, expect, it } from 'vitest';

import { legArc } from '../src/visualization/trajectories/legArc';

const norm = (v: readonly number[]) => Math.hypot(v[0]!, v[1]!, v[2]!);

describe('legArc', () => {
  it('keeps the reported endpoints exactly', () => {
    const from = [1.2, 0, 0] as const;
    const to = [0, 1.3, 0] as const;
    const arc = legArc(from, to);
    expect(arc[0]).toEqual(from);
    expect(arc[arc.length - 1]).toEqual(to);
  });

  it('never dips below the lower endpoint radius, so it cannot pass through the planet', () => {
    // A quarter of the way round the planet: a chord here would pass well inside radius 1.
    const arc = legArc([1.1, 0, 0], [0, 1.15, 0]);
    for (const point of arc) expect(norm(point)).toBeGreaterThanOrEqual(1.1 - 1e-9);
  });

  it('handles exactly antipodal endpoints without producing NaN', () => {
    const arc = legArc([1.2, 0, 0], [-1.2, 0, 0]);
    for (const point of arc) {
      expect(point.every(Number.isFinite)).toBe(true);
      expect(norm(point)).toBeCloseTo(1.2, 6);
    }
  });

  it('returns a straight segment for coincident directions', () => {
    expect(legArc([1.1, 0, 0], [1.2, 0, 0])).toHaveLength(2);
  });
});
