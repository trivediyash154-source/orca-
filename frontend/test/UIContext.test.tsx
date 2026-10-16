import { fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { Sidebar } from '../src/components/navigation/Sidebar';
import { UIProvider, useUI } from '../src/state/UIContext';

function Probe() {
  const { page, presentationMode, setPresentationMode, reducedMotion, setReducedMotion } = useUI();
  return (
    <div>
      <span data-testid="page">{page}</span>
      <span data-testid="presentation">{String(presentationMode)}</span>
      <span data-testid="reducedMotion">{String(reducedMotion)}</span>
      <button onClick={() => setPresentationMode(true)}>enter-presentation</button>
      <button onClick={() => setReducedMotion(true)}>enable-reduced-motion</button>
    </div>
  );
}

describe('UIProvider', () => {
  it('defaults to Mission Control, not presenting, motion not reduced', () => {
    render(
      <UIProvider>
        <Probe />
      </UIProvider>,
    );
    expect(screen.getByTestId('page').textContent).toBe('mission-control');
    expect(screen.getByTestId('presentation').textContent).toBe('false');
    expect(screen.getByTestId('reducedMotion').textContent).toBe('false');
  });

  it('setPresentationMode and setReducedMotion actually change state', () => {
    render(
      <UIProvider>
        <Probe />
      </UIProvider>,
    );
    fireEvent.click(screen.getByText('enter-presentation'));
    expect(screen.getByTestId('presentation').textContent).toBe('true');
    fireEvent.click(screen.getByText('enable-reduced-motion'));
    expect(screen.getByTestId('reducedMotion').textContent).toBe('true');
  });

  it('Sidebar navigation changes the current page', () => {
    render(
      <UIProvider>
        <Sidebar />
        <Probe />
      </UIProvider>,
    );
    fireEvent.click(screen.getByText('Orbital Network'));
    expect(screen.getByTestId('page').textContent).toBe('orbital-network');
  });

  it('the active nav item is marked aria-current', () => {
    render(
      <UIProvider>
        <Sidebar />
      </UIProvider>,
    );
    expect(screen.getByText('Mission Control').closest('button')).toHaveAttribute('aria-current', 'page');
  });

  describe('reducedMotion honors the OS-level prefers-reduced-motion preference', () => {
    afterEach(() => {
      vi.unstubAllGlobals();
    });

    it('defaults reducedMotion to true when the OS reports prefers-reduced-motion: reduce', () => {
      vi.stubGlobal('matchMedia', (query: string) => ({
        matches: query === '(prefers-reduced-motion: reduce)',
        media: query,
        onchange: null,
        addListener: () => {},
        removeListener: () => {},
        addEventListener: () => {},
        removeEventListener: () => {},
        dispatchEvent: () => false,
      }));
      render(
        <UIProvider>
          <Probe />
        </UIProvider>,
      );
      expect(screen.getByTestId('reducedMotion').textContent).toBe('true');
    });
  });
});
