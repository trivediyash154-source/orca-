import { render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { BenchmarkEvidence } from '../src/components/analytics/BenchmarkEvidence';
import { withEngineContext } from './testUtils';

const SEARCH_CSV = [
  'scenarioFamily,objectCount,seed,algorithm,repetition,found,executionTimeMs',
  'sparse,25,1,DIJKSTRA,-1,true,0.05', // warm-up -- must be excluded
  'sparse,25,1,DIJKSTRA,0,true,0.01',
  'sparse,25,1,DIJKSTRA,1,true,0.03',
  'sparse,25,1,ASTAR,0,true,0.09',
].join('\n');

const ASSIGNMENT_CSV = [
  'scenarioFamily,objectCount,seed,algorithm,repetition,found,solverMatrixDimension,candidateGenerationTimeMs,solverTimeMs',
  'normal,2,1,HUNGARIAN,-1,true,2,10.0,0.1',
  'normal,2,1,HUNGARIAN,0,true,2,4.0,0.2',
  'normal,2,1,HUNGARIAN,1,true,2,6.0,0.4',
].join('\n');

describe('BenchmarkEvidence (Phase 12 brief §17: real offline CSV evidence)', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('fetches both real CSVs and renders a correct mean, excluding warm-up repetitions', async () => {
    const fetchMock = vi.fn((url: string) => {
      const path = String(url);
      if (path.includes('/benchmarks/search')) {
        return Promise.resolve({ ok: true, status: 200, text: async () => SEARCH_CSV });
      }
      if (path.includes('/benchmarks/assignment')) {
        return Promise.resolve({ ok: true, status: 200, text: async () => ASSIGNMENT_CSV });
      }
      throw new Error(`unexpected fetch ${path}`);
    });
    vi.stubGlobal('fetch', fetchMock);

    render(withEngineContext({}, <BenchmarkEvidence />));

    // mean of [0.01, 0.03] = 0.02 -- the -1 (warm-up) row must not be included
    await waitFor(() => expect(screen.getByText(/0\.020 ms/)).toBeInTheDocument());
    expect(screen.getByText(/DIJKSTRA @ 25/)).toBeInTheDocument();
    // mean candidateGenerationTimeMs of [4.0, 6.0] = 5.0, mean solverTimeMs of [0.2, 0.4] = 0.3
    await waitFor(() => expect(screen.getByText(/gen 5\.00ms \/ solve 0\.3000ms/)).toBeInTheDocument());
  });

  it('shows a real, honest message when the benchmark CSVs are not available', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: false,
        status: 404,
        statusText: 'Not Found',
        json: async () => ({ error: { message: 'not generated on this machine yet' } }),
      }),
    );

    render(withEngineContext({}, <BenchmarkEvidence />));
    await waitFor(() => expect(screen.getByText(/Not available/)).toBeInTheDocument());
  });
});
