import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import { EventTimeline } from '../src/components/timeline/EventTimeline';
import { sampleSnapshot } from './fixtures';
import { withEngineContext } from './testUtils';

describe('EventTimeline', () => {
  it('shows a placeholder when there are no events yet', () => {
    render(withEngineContext({ snapshot: { ...sampleSnapshot, recentEvents: [] } }, <EventTimeline />));
    expect(screen.getByText('No events yet.')).toBeInTheDocument();
  });

  it('renders every real event from the snapshot, in order', () => {
    render(withEngineContext({ snapshot: sampleSnapshot }, <EventTimeline />));
    expect(screen.getByText('SATELLITE FAILURE')).toBeInTheDocument();
  });

  it('clicking an event with a satellite subject selects that satellite', () => {
    const select = vi.fn();
    render(withEngineContext({ snapshot: sampleSnapshot, select }, <EventTimeline />));
    fireEvent.click(screen.getByText('SATELLITE FAILURE'));
    expect(select).toHaveBeenCalledWith({ kind: 'satellite', id: 'SAT-101' });
  });

  it('an event with no related entity is not clickable', () => {
    const withOrphanEvent = {
      ...sampleSnapshot,
      recentEvents: [
        {
          id: 'EVT-000001',
          kind: 'MISSION_START',
          timestampSec: 0,
          satelliteId: null,
          vehicleId: null,
          emergencyId: null,
          missionId: null,
          debrisId: null,
          detail: 'simulation start',
        },
      ],
    };
    render(withEngineContext({ snapshot: withOrphanEvent }, <EventTimeline />));
    expect(screen.getByText('MISSION START').closest('button')).toBeDisabled();
  });
});
