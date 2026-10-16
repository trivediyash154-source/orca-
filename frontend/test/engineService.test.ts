import { afterEach, describe, expect, it, vi } from 'vitest';

import {
  EngineDownError,
  EngineRequestError,
  fetchHealth,
  fetchSnapshot,
  fetchVisualConstellation,
  sendAdvance,
  sendRescoreParetoFront,
  sendSetSpeed,
  sendSimCommand,
} from '../src/services/engineService';
import { sampleSnapshot } from './fixtures';

function mockFetchOnce(status: number, body: unknown) {
  return vi.fn().mockResolvedValue({
    ok: status >= 200 && status < 300,
    status,
    statusText: `status ${status}`,
    json: async () => body,
  });
}

describe('engineService', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('fetchHealth returns the parsed health body on success', async () => {
    vi.stubGlobal(
      'fetch',
      mockFetchOnce(200, { status: 'ok', engineUp: true, engineStatus: 'READY', schemaVersion: 1, uptimeSec: 12.3 }),
    );
    const health = await fetchHealth('http://x');
    expect(health.engineUp).toBe(true);
    expect(health.schemaVersion).toBe(1);
  });

  it('fetchSnapshot returns the real snapshot shape on success', async () => {
    vi.stubGlobal('fetch', mockFetchOnce(200, sampleSnapshot));
    const snap = await fetchSnapshot('http://x');
    expect(snap.type).toBe('snapshot');
    expect(snap.satellites).toHaveLength(2);
  });

  it('fetchSnapshot throws EngineDownError on 503', async () => {
    vi.stubGlobal('fetch', mockFetchOnce(503, { error: { code: 'ENGINE_DOWN', message: 'engine not running' } }));
    await expect(fetchSnapshot('http://x')).rejects.toBeInstanceOf(EngineDownError);
  });

  it('fetchSnapshot throws EngineRequestError on other non-ok statuses', async () => {
    vi.stubGlobal('fetch', mockFetchOnce(500, { error: { message: 'boom' } }));
    await expect(fetchSnapshot('http://x')).rejects.toBeInstanceOf(EngineRequestError);
  });

  it('sendSimCommand posts to the correct path and unwraps the snapshot', async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ ok: true, snapshot: sampleSnapshot }),
    });
    vi.stubGlobal('fetch', fetchMock);
    const snap = await sendSimCommand('http://x', 'start');
    expect(fetchMock).toHaveBeenCalledWith('http://x/sim/start', expect.objectContaining({ method: 'POST' }));
    expect(snap).toBe(sampleSnapshot);
  });

  it('sendAdvance posts seconds as JSON', async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ ok: true, snapshot: sampleSnapshot }),
    });
    vi.stubGlobal('fetch', fetchMock);
    await sendAdvance('http://x', 42);
    const call = fetchMock.mock.calls[0]!;
    expect(call[0]).toBe('http://x/sim/advance');
    expect(JSON.parse(call[1].body as string)).toEqual({ seconds: 42 });
  });

  it('sendSetSpeed posts multiplier as JSON', async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ ok: true, snapshot: sampleSnapshot }),
    });
    vi.stubGlobal('fetch', fetchMock);
    await sendSetSpeed('http://x', 4);
    const call = fetchMock.mock.calls[0]!;
    expect(JSON.parse(call[1].body as string)).toEqual({ multiplier: 4 });
  });

  it('sendRescoreParetoFront posts missionId and profile as JSON', async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ ok: true, snapshot: sampleSnapshot }),
    });
    vi.stubGlobal('fetch', fetchMock);
    const snap = await sendRescoreParetoFront('http://x', 'M-000', 'FASTEST');
    const call = fetchMock.mock.calls[0]!;
    expect(call[0]).toBe('http://x/sim/rescoreParetoFront');
    expect(JSON.parse(call[1].body as string)).toEqual({ missionId: 'M-000', profile: 'FASTEST' });
    expect(snap).toBe(sampleSnapshot);
  });

  it('fetchVisualConstellation requests the real GET endpoint with query params', async () => {
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
    const result = await fetchVisualConstellation('http://x', 'dense', 2000, 1, 0);
    const call = fetchMock.mock.calls[0]!;
    expect(call[0]).toBe('http://x/visual/constellation?family=dense&size=2000&seed=1&timeSec=0');
    expect(result).toEqual(constellationBody);
  });

  it('fetchVisualConstellation throws EngineRequestError on a non-ok response', async () => {
    vi.stubGlobal('fetch', mockFetchOnce(400, { error: { message: 'bad family' } }));
    await expect(fetchVisualConstellation('http://x', 'dense', 2000, 1, 0)).rejects.toBeInstanceOf(
      EngineRequestError,
    );
  });
});
