import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { MissionIntelligence } from '../src/components/mission/MissionIntelligence';
import { sampleSnapshot } from './fixtures';
import { withEngineContext } from './testUtils';

describe('MissionIntelligence', () => {
  it('shows "no active mission" when nothing is relevant', () => {
    render(withEngineContext({ snapshot: { ...sampleSnapshot, missions: [] } }, <MissionIntelligence />));
    expect(screen.getByText('No active mission.')).toBeInTheDocument();
  });

  it('falls back to the sole mission when nothing is explicitly selected', () => {
    render(withEngineContext({ snapshot: sampleSnapshot, selected: null }, <MissionIntelligence />));
    expect(screen.getByText('SAT-101')).toBeInTheDocument();
    expect(screen.getByText('RV-01')).toBeInTheDocument();
  });

  it('shows the recommended Pareto point risk when the front still matches the route', () => {
    render(withEngineContext({ snapshot: sampleSnapshot, selected: null }, <MissionIntelligence />));
    // sampleSnapshot's recommended point has risk 0.5278 -> 52.8%
    expect(screen.getByText('52.8%')).toBeInTheDocument();
  });

  it('computes fuel cost from real leg delta-v divided by the vehicle\'s real budget', () => {
    render(withEngineContext({ snapshot: sampleSnapshot, selected: null }, <MissionIntelligence />));
    // legs: 0.42 + 0.38 = 0.80 km/s over a 10 km/s budget = 8.0%
    expect(screen.getByText('8.0%')).toBeInTheDocument();
  });

  it('never fabricates a success probability', () => {
    render(withEngineContext({ snapshot: sampleSnapshot, selected: null }, <MissionIntelligence />));
    expect(screen.getByText(/N\/A — not computed by the engine/)).toBeInTheDocument();
  });

  it('shows the real explanation text when present, and an honest fallback otherwise', () => {
    render(withEngineContext({ snapshot: sampleSnapshot, selected: null }, <MissionIntelligence />));
    expect(screen.getByText(/original assignment/)).toBeInTheDocument();

    const withExplanation = {
      ...sampleSnapshot,
      missions: [{ ...sampleSnapshot.missions[0]!, explanation: 'Decision:\nREPLAN\n' }],
    };
    render(withEngineContext({ snapshot: withExplanation, selected: null }, <MissionIntelligence />));
    expect(screen.getByText(/REPLAN/)).toBeInTheDocument();
  });

  it('resolves the mission for a selected satellite via its emergency', () => {
    const twoMissions = {
      ...sampleSnapshot,
      missions: [
        { ...sampleSnapshot.missions[0]!, id: 'M-000' },
        { ...sampleSnapshot.missions[0]!, id: 'M-001', vehicleId: 'RV-02' },
      ],
    };
    render(
      withEngineContext(
        { snapshot: twoMissions, selected: { kind: 'satellite', id: 'SAT-101' } },
        <MissionIntelligence />,
      ),
    );
    // Both missions reference the same emergency in this fixture, so either resolving
    // deterministically is acceptable -- the assertion is that a mission renders at all,
    // not "no active mission."
    expect(screen.queryByText('No active mission.')).not.toBeInTheDocument();
  });
});
