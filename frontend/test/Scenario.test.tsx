import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { ScenarioPage } from '../src/pages/Scenario/Scenario';
import { sampleSnapshot } from './fixtures';
import { withEngineContext } from './testUtils';

describe('ScenarioPage', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('shows the real active scenario name', () => {
    render(withEngineContext({ snapshot: sampleSnapshot }, <ScenarioPage />));
    expect(screen.getByText(sampleSnapshot.scenarioName)).toBeInTheDocument();
  });

  it('shows real object counts from the live snapshot (Phase 12)', () => {
    render(withEngineContext({ snapshot: sampleSnapshot }, <ScenarioPage />));
    const satellitesCard = screen.getByText('Satellites').closest('.metric-card');
    expect(satellitesCard).toHaveTextContent(String(sampleSnapshot.satellites.length));
    const vehiclesCard = screen.getByText('Rescue Vehicles').closest('.metric-card');
    expect(vehiclesCard).toHaveTextContent(String(sampleSnapshot.rescueVehicles.length));
  });

  it('Generate calls the real /visual/constellation endpoint and renders real returned counts (Phase 12)', async () => {
    const constellationBody = {
      type: 'visualConstellation',
      scenarioName: 'dense-2000',
      timeSec: 0,
      generationMs: 9.8,
      satelliteCount: 2000,
      vehicleCount: 250,
      debrisCount: 133,
      satellites: [],
      vehicles: [],
      debris: [],
    };
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => constellationBody,
    });
    vi.stubGlobal('fetch', fetchMock);

    render(withEngineContext({ snapshot: sampleSnapshot }, <ScenarioPage />));
    fireEvent.click(screen.getByText('Generate'));

    await waitFor(() => expect(screen.getByText('2,000')).toBeInTheDocument());
    const call = fetchMock.mock.calls[0]!;
    expect(String(call[0])).toContain('/visual/constellation?');
    expect(String(call[0])).toContain('family=dense');
    expect(String(call[0])).toContain('size=2000');
  });

  it('shows a real error message when generation fails, not a silent no-op', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: false,
        status: 400,
        statusText: 'Bad Request',
        json: async () => ({ error: { message: 'family must be one of: sparse, normal, dense' } }),
      }),
    );

    render(withEngineContext({ snapshot: sampleSnapshot }, <ScenarioPage />));
    fireEvent.click(screen.getByText('Generate'));

    await waitFor(() => expect(screen.getByText(/Generation failed/)).toBeInTheDocument());
  });
});
