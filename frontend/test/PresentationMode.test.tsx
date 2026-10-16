import { fireEvent, render } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import { usePresentationKeyboardShortcuts } from '../src/pages/PresentationMode/PresentationMode';
import { UIProvider, useUI } from '../src/state/UIContext';
import { EngineContext, type EngineContextValue } from '../src/state/EngineContext';
import { sampleSnapshot } from './fixtures';
import { makeContextValue } from './testUtils';

/** Exercises the real hook directly (`usePresentationKeyboardShortcuts`), not the full
 * `PresentationModeView` page -- the page also renders `<Scene>`, a react-three-fiber
 * `<Canvas>` that needs a real WebGL context jsdom does not provide. The hook itself has no
 * rendering dependency on `Scene`, so testing it in isolation is both more robust and a more
 * precise test of brief §61's actual behavior (which keys send which real commands). */
function Harness() {
  usePresentationKeyboardShortcuts();
  const { presentationMode, setPresentationMode } = useUI();
  return (
    <>
      <span data-testid="presentation">{String(presentationMode)}</span>
      <button onClick={() => setPresentationMode(true)}>enter</button>
    </>
  );
}

function renderHarness(engineOverrides: Partial<EngineContextValue> = {}) {
  return render(
    <EngineContext.Provider value={makeContextValue({ snapshot: sampleSnapshot, ...engineOverrides })}>
      <UIProvider>
        <Harness />
      </UIProvider>
    </EngineContext.Provider>,
  );
}

describe('usePresentationKeyboardShortcuts (brief §61)', () => {
  it('Space sends start when paused', () => {
    const sendCommand = vi.fn().mockResolvedValue(undefined);
    renderHarness({ snapshot: { ...sampleSnapshot, simulationStatus: 'PAUSED' }, sendCommand });
    fireEvent.keyDown(window, { key: ' ' });
    expect(sendCommand).toHaveBeenCalledWith('start');
  });

  it('Space sends pause when running', () => {
    const sendCommand = vi.fn().mockResolvedValue(undefined);
    renderHarness({ snapshot: { ...sampleSnapshot, simulationStatus: 'RUNNING' }, sendCommand });
    fireEvent.keyDown(window, { key: ' ' });
    expect(sendCommand).toHaveBeenCalledWith('pause');
  });

  it('R sends reset, S sends step', () => {
    const sendCommand = vi.fn().mockResolvedValue(undefined);
    renderHarness({ sendCommand });
    fireEvent.keyDown(window, { key: 'r' });
    expect(sendCommand).toHaveBeenCalledWith('reset');
    fireEvent.keyDown(window, { key: 's' });
    expect(sendCommand).toHaveBeenCalledWith('step');
  });

  it('1/2/3 set real speed multipliers', () => {
    const setSpeed = vi.fn().mockResolvedValue(undefined);
    renderHarness({ setSpeed });
    fireEvent.keyDown(window, { key: '1' });
    expect(setSpeed).toHaveBeenCalledWith(1);
    fireEvent.keyDown(window, { key: '2' });
    expect(setSpeed).toHaveBeenCalledWith(4);
    fireEvent.keyDown(window, { key: '3' });
    expect(setSpeed).toHaveBeenCalledWith(16);
  });

  it('Escape exits presentation mode via the real UIProvider', () => {
    const { getByTestId, getByText } = renderHarness();
    fireEvent.click(getByText('enter'));
    expect(getByTestId('presentation').textContent).toBe('true');
    fireEvent.keyDown(window, { key: 'Escape' });
    expect(getByTestId('presentation').textContent).toBe('false');
  });

  it('unrelated keys do nothing', () => {
    const sendCommand = vi.fn();
    const setSpeed = vi.fn();
    renderHarness({ sendCommand, setSpeed });
    fireEvent.keyDown(window, { key: 'x' });
    expect(sendCommand).not.toHaveBeenCalled();
    expect(setSpeed).not.toHaveBeenCalled();
  });
});
