import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { ParetoChart } from '../src/charts/ParetoChart';
import { sampleSnapshot } from './fixtures';

describe('ParetoChart', () => {
  it('shows a placeholder for an empty front rather than an empty chart', () => {
    render(<ParetoChart points={[]} />);
    expect(screen.getByText(/No Pareto front available/)).toBeInTheDocument();
  });

  it('renders every real point from the front and never computes dominance itself', () => {
    const points = sampleSnapshot.missions[0]!.paretoFront;
    const { container } = render(<ParetoChart points={points} />);
    expect(container.querySelectorAll('circle.pareto-point')).toHaveLength(points.length);
    // dominance/recommended classes are applied directly from the engine-provided flags,
    // not recalculated -- one recommended point should be present per the fixture.
    expect(container.querySelectorAll('circle.pareto-point-recommended')).toHaveLength(1);
  });

  it('hovering a point shows its real cost values', () => {
    const points = sampleSnapshot.missions[0]!.paretoFront;
    const { container } = render(<ParetoChart points={points} />);
    const firstPoint = container.querySelector('circle.pareto-point')!;
    fireEvent.mouseEnter(firstPoint);
    expect(screen.getByText(points[0]!.planId)).toBeInTheDocument();
  });
});
