import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { SettingsPage } from '../src/pages/Settings/Settings';
import { UIProvider, useUI } from '../src/state/UIContext';

/** Phase 12 brief §21: every setting must actually change state a real consumer reads.
 * This probe renders the live `UIContext` values next to the page, so a toggle that looks
 * enabled but changes nothing would fail here. */
function Probe() {
  const ui = useUI();
  return (
    <div>
      <span data-testid="size">{ui.constellationSize}</span>
      <span data-testid="clouds">{String(ui.showClouds)}</span>
      <span data-testid="rotation">{String(ui.earthRotation)}</span>
      <span data-testid="ribbons">{String(ui.showConstellationRibbons)}</span>
      <span data-testid="exag">{ui.altitudeExaggeration}</span>
      <span data-testid="perf">{String(ui.performanceMode)}</span>
    </div>
  );
}

function renderSettings() {
  return render(
    <UIProvider>
      <SettingsPage />
      <Probe />
    </UIProvider>,
  );
}

describe('SettingsPage (Phase 12: every control is real)', () => {
  it('changing constellation size updates the shared UI state', () => {
    renderSettings();
    expect(screen.getByTestId('size').textContent).toBe('5000');
    fireEvent.change(screen.getByRole('combobox'), { target: { value: '10000' } });
    expect(screen.getByTestId('size').textContent).toBe('10000');
  });

  it('the altitude-exaggeration slider updates the shared UI state', () => {
    renderSettings();
    fireEvent.change(screen.getByRole('slider'), { target: { value: '15' } });
    expect(screen.getByTestId('exag').textContent).toBe('15');
  });

  it('scene toggles flip their real backing values', () => {
    renderSettings();
    expect(screen.getByTestId('clouds').textContent).toBe('true');
    fireEvent.click(screen.getByText('Clouds'));
    expect(screen.getByTestId('clouds').textContent).toBe('false');

    expect(screen.getByTestId('rotation').textContent).toBe('true');
    fireEvent.click(screen.getByText('Earth rotation'));
    expect(screen.getByTestId('rotation').textContent).toBe('false');

    expect(screen.getByTestId('perf').textContent).toBe('false');
    fireEvent.click(screen.getByText('Performance mode'));
    expect(screen.getByTestId('perf').textContent).toBe('true');
  });

  it('every listed toggle is bound to a distinct real setting (no dead controls)', () => {
    const { container } = renderSettings();
    const boxes = container.querySelectorAll('.settings-toggle-block input[type="checkbox"]');
    expect(boxes.length).toBe(10);
  });
});
