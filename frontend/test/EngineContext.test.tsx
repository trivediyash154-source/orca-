import { act, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { EngineProvider, useEngine, EXPECTED_SCHEMA_VERSION } from '../src/state/EngineContext';
import { sampleSnapshot } from './fixtures';

function Probe() {
  const { connectionStatus, snapshot } = useEngine();
  return (
    <div>
      <span data-testid="status">{connectionStatus}</span>
      <span data-testid="missions">{snapshot ? snapshot.missions.length : 'none'}</span>
      <span data-testid="time">{snapshot ? snapshot.simulationTimeSec : 'none'}</span>
    </div>
  );
}

describe('EngineProvider (polling integration, mocked fetch)', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
    vi.useRealTimers();
  });

  it('reaches Connected and renders a real snapshot once the engine is up', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn((url: string) => {
        if (url.endsWith('/health')) {
          return Promise.resolve({
            ok: true,
            status: 200,
            json: async () => ({ status: 'ok', engineUp: true, engineStatus: 'READY', schemaVersion: 1, uptimeSec: 1 }),
          });
        }
        return Promise.resolve({ ok: true, status: 200, json: async () => sampleSnapshot });
      }),
    );

    render(
      <EngineProvider baseUrl="http://x" pollIntervalMs={50}>
        <Probe />
      </EngineProvider>,
    );

    await waitFor(() => expect(screen.getByTestId('status').textContent).toBe('Connected'));
    expect(screen.getByTestId('missions').textContent).toBe('1');
  });

  it('reports EngineStarting while the engine process is up but not ready', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        json: async () => ({ status: 'degraded', engineUp: false, engineStatus: 'STARTING', schemaVersion: null, uptimeSec: 1 }),
      }),
    );

    render(
      <EngineProvider baseUrl="http://x" pollIntervalMs={50}>
        <Probe />
      </EngineProvider>,
    );

    await waitFor(() => expect(screen.getByTestId('status').textContent).toBe('EngineStarting'));
  });

  it('a fresh snapshot REPLACES the old one wholesale, never merges (brief #82)', async () => {
    let callCount = 0;
    vi.stubGlobal(
      'fetch',
      vi.fn((url: string) => {
        if (url.endsWith('/health')) {
          return Promise.resolve({
            ok: true,
            status: 200,
            json: async () => ({ status: 'ok', engineUp: true, engineStatus: 'READY', schemaVersion: 1, uptimeSec: 1 }),
          });
        }
        callCount += 1;
        // First poll: the full demo snapshot (1 mission). Second poll onward: simulates a
        // backend restart producing a fresh, empty-missions snapshot -- if the store were
        // merging instead of replacing, the old mission would still be visible.
        const body = callCount === 1 ? sampleSnapshot : { ...sampleSnapshot, missions: [], simulationTimeSec: 0 };
        return Promise.resolve({ ok: true, status: 200, json: async () => body });
      }),
    );

    render(
      <EngineProvider baseUrl="http://x" pollIntervalMs={20}>
        <Probe />
      </EngineProvider>,
    );

    await waitFor(() => expect(screen.getByTestId('missions').textContent).toBe('1'));
    await waitFor(() => expect(screen.getByTestId('missions').textContent).toBe('0'), { timeout: 2000 });
    expect(screen.getByTestId('time').textContent).toBe('0');
  });

  it('falls back to Disconnected on a network failure after having connected once', async () => {
    let up = true;
    vi.stubGlobal(
      'fetch',
      vi.fn((url: string) => {
        if (!up) return Promise.reject(new Error('network down'));
        if (url.endsWith('/health')) {
          return Promise.resolve({
            ok: true,
            status: 200,
            json: async () => ({ status: 'ok', engineUp: true, engineStatus: 'READY', schemaVersion: 1, uptimeSec: 1 }),
          });
        }
        return Promise.resolve({ ok: true, status: 200, json: async () => sampleSnapshot });
      }),
    );

    render(
      <EngineProvider baseUrl="http://x" pollIntervalMs={20}>
        <Probe />
      </EngineProvider>,
    );
    await waitFor(() => expect(screen.getByTestId('status').textContent).toBe('Connected'));

    act(() => {
      up = false;
    });
    await waitFor(() => expect(screen.getByTestId('status').textContent).toBe('Disconnected'), {
      timeout: 2000,
    });
  });

  // Self-review finding (INTEGRATION_ARCHITECTURE.md's adversarial review, brief #79 "can
  // C++ crash without the frontend recognizing it?"): a crashed engine was previously
  // reported identically to a still-starting one ('EngineStarting'), which wrongly implies
  // it will become ready on its own.
  it('reports a crashed engine as Error (never having connected) rather than EngineStarting', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        json: async () => ({
          status: 'degraded',
          engineUp: false,
          engineStatus: 'CRASHED',
          schemaVersion: null,
          uptimeSec: 1,
        }),
      }),
    );

    render(
      <EngineProvider baseUrl="http://x" pollIntervalMs={50}>
        <Probe />
      </EngineProvider>,
    );

    await waitFor(() => expect(screen.getByTestId('status').textContent).toBe('Error'));
  });

  // Self-review finding (brief #54/#79 "can schema changes silently break the UI?"):
  // nothing previously compared the engine's reported schemaVersion against what this
  // frontend build actually expects.
  it('reports a schema-version mismatch as Error and does not apply the snapshot', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn((url: string) => {
        if (url.endsWith('/health')) {
          return Promise.resolve({
            ok: true,
            status: 200,
            json: async () => ({
              status: 'ok',
              engineUp: true,
              engineStatus: 'READY',
              schemaVersion: EXPECTED_SCHEMA_VERSION + 1,
              uptimeSec: 1,
            }),
          });
        }
        return Promise.resolve({ ok: true, status: 200, json: async () => sampleSnapshot });
      }),
    );

    render(
      <EngineProvider baseUrl="http://x" pollIntervalMs={50}>
        <Probe />
      </EngineProvider>,
    );

    await waitFor(() => expect(screen.getByTestId('status').textContent).toBe('Error'));
    expect(screen.getByTestId('missions').textContent).toBe('none');
  });
});
