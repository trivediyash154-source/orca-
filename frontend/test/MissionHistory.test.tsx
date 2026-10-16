import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import { MissionHistoryPage } from '../src/pages/MissionHistory/MissionHistory';
import { sampleSnapshot } from './fixtures';
import { withEngineContext } from './testUtils';

describe('MissionHistoryPage', () => {
  it('shows an honest empty state rather than fabricated rows', () => {
    render(withEngineContext({ snapshot: { ...sampleSnapshot, missions: [] } }, <MissionHistoryPage />));
    expect(screen.getByText(/No missions have been created yet/)).toBeInTheDocument();
  });

  it('renders every real mission with its real fields', () => {
    render(withEngineContext({ snapshot: sampleSnapshot }, <MissionHistoryPage />));
    expect(screen.getByText('M-000')).toBeInTheDocument();
    expect(screen.getByText('RV-01')).toBeInTheDocument();
    expect(screen.getByText('IN_TRANSIT')).toBeInTheDocument();
  });

  it('clicking a row selects that mission', () => {
    const select = vi.fn();
    render(withEngineContext({ snapshot: sampleSnapshot, select }, <MissionHistoryPage />));
    fireEvent.click(screen.getByText('M-000'));
    expect(select).toHaveBeenCalledWith({ kind: 'mission', id: 'M-000' });
  });

  it('clicking Export CSV downloads a real file built from the current missions', () => {
    // jsdom does not implement the Blob URL APIs -- stubbed here just enough to observe the
    // real download was triggered, not to test browser download mechanics themselves.
    const createObjectURL = vi.fn(() => 'blob:mock');
    const revokeObjectURL = vi.fn();
    vi.stubGlobal('URL', { ...URL, createObjectURL, revokeObjectURL });
    const clickSpy = vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => {});

    render(withEngineContext({ snapshot: sampleSnapshot }, <MissionHistoryPage />));
    fireEvent.click(screen.getByText('Export CSV'));

    expect(createObjectURL).toHaveBeenCalledTimes(1);
    expect(clickSpy).toHaveBeenCalledTimes(1);
    expect(revokeObjectURL).toHaveBeenCalledWith('blob:mock');

    clickSpy.mockRestore();
    vi.unstubAllGlobals();
  });
});
