import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import { ControlPanel } from '../src/components/ControlPanel';
import { sampleSnapshot } from './fixtures';
import { withEngineContext } from './testUtils';

describe('ControlPanel', () => {
  it('disables every control while disconnected', () => {
    render(withEngineContext({ connectionStatus: 'Disconnected', snapshot: null }, <ControlPanel />));
    for (const label of ['▶ Start', '⏸ Pause', '↻ Reset', 'Step']) {
      expect(screen.getByText(label)).toBeDisabled();
    }
  });

  it('Start sends the start command', () => {
    const sendCommand = vi.fn().mockResolvedValue(undefined);
    render(
      withEngineContext(
        { connectionStatus: 'Connected', snapshot: { ...sampleSnapshot, simulationStatus: 'PAUSED' }, sendCommand },
        <ControlPanel />,
      ),
    );
    fireEvent.click(screen.getByText('▶ Start'));
    expect(sendCommand).toHaveBeenCalledWith('start');
  });

  it('Pause is disabled while paused, enabled while running', () => {
    render(
      withEngineContext(
        { connectionStatus: 'Connected', snapshot: { ...sampleSnapshot, simulationStatus: 'PAUSED' } },
        <ControlPanel />,
      ),
    );
    expect(screen.getByText('⏸ Pause')).toBeDisabled();
  });

  it('Advance sends the current input value in seconds', () => {
    const advance = vi.fn().mockResolvedValue(undefined);
    render(
      withEngineContext(
        { connectionStatus: 'Connected', snapshot: sampleSnapshot, advance },
        <ControlPanel />,
      ),
    );
    const input = screen.getByLabelText('Advance seconds');
    fireEvent.change(input, { target: { value: '120' } });
    fireEvent.click(screen.getByText('Advance 120s'));
    expect(advance).toHaveBeenCalledWith(120);
  });

  it('changing speed sends setSpeed with the selected multiplier', () => {
    const setSpeed = vi.fn().mockResolvedValue(undefined);
    render(
      withEngineContext(
        { connectionStatus: 'Connected', snapshot: sampleSnapshot, setSpeed },
        <ControlPanel />,
      ),
    );
    fireEvent.change(screen.getByLabelText('Speed'), { target: { value: '4' } });
    expect(setSpeed).toHaveBeenCalledWith(4);
  });
});
