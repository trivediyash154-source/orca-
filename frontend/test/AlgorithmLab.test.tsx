import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import { AlgorithmLabPage } from '../src/pages/AlgorithmLab/AlgorithmLab';
import { sampleSnapshot } from './fixtures';
import { withEngineContext } from './testUtils';

describe('AlgorithmLabPage', () => {
  it('shows an honest empty state before any algorithm has run', () => {
    render(withEngineContext({ snapshot: { ...sampleSnapshot, algorithmRuns: [] } }, <AlgorithmLabPage />));
    expect(screen.getByText(/No algorithm has run yet/)).toBeInTheDocument();
  });

  it('renders one card per algorithm with real metrics, never fabricated ones', () => {
    render(withEngineContext({ snapshot: sampleSnapshot }, <AlgorithmLabPage />));
    expect(screen.getByText('HUNGARIAN')).toBeInTheDocument();
    expect(screen.getByText('DIJKSTRA')).toBeInTheDocument();
    // Dijkstra's real nodesExpanded (4) from the fixture, scoped to its own card since
    // other real counters in the fixture also happen to equal "4".
    const dijkstraCard = screen.getByText('DIJKSTRA').closest('.algorithm-card')!;
    expect(dijkstraCard).toHaveTextContent('Nodes Expanded');
    expect(dijkstraCard.textContent).toContain('4');
  });

  it('never shows a field the algorithm does not actually produce', () => {
    render(withEngineContext({ snapshot: sampleSnapshot }, <AlgorithmLabPage />));
    // Hungarian's card must not claim a "Nodes Expanded" field (search-only, null in the fixture)
    const hungarianCard = screen.getByText('HUNGARIAN').closest('.algorithm-card')!;
    expect(hungarianCard).not.toHaveTextContent('Nodes Expanded');
  });

  it('Run DIJKSTRA calls the real runAlgorithm command (Phase 12)', async () => {
    const runAlgorithm = vi.fn().mockResolvedValue(true);
    render(withEngineContext({ snapshot: sampleSnapshot, runAlgorithm }, <AlgorithmLabPage />));
    fireEvent.click(screen.getByText('Run DIJKSTRA'));
    expect(runAlgorithm).toHaveBeenCalledWith('DIJKSTRA');
    await waitFor(() => expect(screen.getByText('Run DIJKSTRA')).not.toBeDisabled());
  });

  it('shows a real inline error when a run fails (e.g. HUNGARIAN with nothing to assign)', async () => {
    const runAlgorithm = vi.fn().mockResolvedValue(false);
    render(withEngineContext({ snapshot: sampleSnapshot, runAlgorithm }, <AlgorithmLabPage />));
    fireEvent.click(screen.getByText('Run HUNGARIAN'));
    await waitFor(() => expect(screen.getByText(/did not run/)).toBeInTheDocument());
  });
});
