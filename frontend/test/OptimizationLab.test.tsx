import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import { OptimizationLabPage } from '../src/pages/OptimizationLab/OptimizationLab';
import { sampleSnapshot } from './fixtures';
import { withEngineContext } from './testUtils';

describe('OptimizationLabPage', () => {
  it('shows an honest empty state when no mission has a captured front', () => {
    render(withEngineContext({ snapshot: { ...sampleSnapshot, missions: [] } }, <OptimizationLabPage />));
    expect(screen.getByText(/No mission with a captured Pareto front/)).toBeInTheDocument();
  });

  it('renders the current front and every retained planning pass', () => {
    render(withEngineContext({ snapshot: sampleSnapshot }, <OptimizationLabPage />));
    expect(screen.getByText(/Mission M-000/)).toBeInTheDocument();
    expect(screen.getByText('Pass 1')).toBeInTheDocument();
  });

  it('clicking a profile button calls rescoreParetoFront with the mission id and profile name', () => {
    const rescoreParetoFront = vi.fn().mockResolvedValue(undefined);
    render(withEngineContext({ snapshot: sampleSnapshot, rescoreParetoFront }, <OptimizationLabPage />));
    fireEvent.click(screen.getByText('Fastest'));
    expect(rescoreParetoFront).toHaveBeenCalledWith('M-000', 'FASTEST');
  });

  it('shows the active rescore profile once one has been requested', () => {
    render(
      withEngineContext(
        { snapshot: { ...sampleSnapshot, missions: [{ ...sampleSnapshot.missions[0]!, activeRescoreProfile: 'FASTEST' }] } },
        <OptimizationLabPage />,
      ),
    );
    expect(screen.getByText(/re-scored for the "FASTEST" profile/)).toBeInTheDocument();
  });
});
