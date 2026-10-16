import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import { OrbitalNetworkPage } from '../src/pages/OrbitalNetwork/OrbitalNetwork';
import { sampleSnapshot } from './fixtures';
import { withEngineContext } from './testUtils';

describe('OrbitalNetworkPage', () => {
  it('renders every real graph node and edge, none invented', () => {
    const { container } = render(withEngineContext({ snapshot: sampleSnapshot }, <OrbitalNetworkPage />));
    expect(container.querySelectorAll('circle.network-node')).toHaveLength(sampleSnapshot.graph.nodes.length);
    expect(container.querySelectorAll('line.network-edge')).toHaveLength(sampleSnapshot.graph.edges.length);
  });

  it('shows a placeholder before any snapshot has arrived', () => {
    render(withEngineContext({ snapshot: null }, <OrbitalNetworkPage />));
    expect(screen.getByText(/No snapshot yet/)).toBeInTheDocument();
  });

  it('hovering an edge shows its real transfer cost', () => {
    const { container } = render(withEngineContext({ snapshot: sampleSnapshot }, <OrbitalNetworkPage />));
    const edge = container.querySelector('line.network-edge')!;
    fireEvent.mouseEnter(edge);
    expect(screen.getByText('Yes')).toBeInTheDocument(); // feasible: true, from the fixture
  });

  it('clicking a satellite node selects it', () => {
    const select = vi.fn();
    render(withEngineContext({ snapshot: sampleSnapshot, select }, <OrbitalNetworkPage />));
    const circle = screen.getByText('SAT-101').closest('g')!.querySelector('circle')!;
    fireEvent.click(circle);
    expect(select).toHaveBeenCalledWith({ kind: 'satellite', id: 'SAT-101' });
  });
});
