import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { EngineOfflineOverlay } from '../src/components/EngineOfflineOverlay';
import { withEngineContext } from './testUtils';

describe('EngineOfflineOverlay', () => {
  it('renders nothing while connected -- never masks a working dashboard', () => {
    const { container } = render(withEngineContext({ connectionStatus: 'Connected' }, <EngineOfflineOverlay />));
    expect(container).toBeEmptyDOMElement();
  });

  it('shows an explicit offline message when disconnected, not a frozen/blank screen', () => {
    render(withEngineContext({ connectionStatus: 'Disconnected', lastError: 'fetch failed' }, <EngineOfflineOverlay />));
    expect(screen.getByText('ORCA ENGINE OFFLINE')).toBeInTheDocument();
    expect(screen.getByText('fetch failed')).toBeInTheDocument();
  });

  it('shows a distinct starting message while the engine process is still coming up', () => {
    render(withEngineContext({ connectionStatus: 'EngineStarting' }, <EngineOfflineOverlay />));
    expect(screen.getByText('ORCA ENGINE STARTING')).toBeInTheDocument();
  });

  it('shows a distinct message for a crashed engine or schema mismatch (Error state)', () => {
    render(
      withEngineContext(
        { connectionStatus: 'Error', lastError: 'engine process crashed' },
        <EngineOfflineOverlay />,
      ),
    );
    expect(screen.getByText('ORCA ENGINE OFFLINE')).toBeInTheDocument();
    expect(screen.getByText(/crashed, or an incompatible version/)).toBeInTheDocument();
  });
});
