import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { MissionPlannerPage } from '../src/pages/MissionPlanner/MissionPlanner';
import { sampleSnapshot } from './fixtures';
import { withEngineContext } from './testUtils';

describe('MissionPlannerPage (Phase 12 rework)', () => {
  it('shows an honest empty state when no missions have been dispatched', () => {
    render(withEngineContext({ snapshot: { ...sampleSnapshot, missions: [] } }, <MissionPlannerPage />));
    expect(screen.getByText(/No missions have been dispatched yet/)).toBeInTheDocument();
  });

  it('renders every real mission with its real route legs', () => {
    render(withEngineContext({ snapshot: sampleSnapshot }, <MissionPlannerPage />));
    expect(screen.getByText('M-000')).toBeInTheDocument();
    expect(screen.getByText('RV-01')).toBeInTheDocument();
    // real leg endpoints from the fixture (SAT-201 appears in both legs, as leg 1's
    // destination and leg 2's origin)
    expect(screen.getAllByText('SAT-201').length).toBeGreaterThan(0);
    expect(screen.getByText('SAT-101')).toBeInTheDocument();
  });

  it('is not the old dead placeholder text', () => {
    render(withEngineContext({ snapshot: sampleSnapshot }, <MissionPlannerPage />));
    expect(screen.queryByText(/not exposed by the engine in this build/)).not.toBeInTheDocument();
  });
});
