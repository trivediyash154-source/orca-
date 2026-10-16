import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { AnalyticsPage } from '../src/pages/Analytics/Analytics';
import { sampleSnapshot } from './fixtures';
import { withEngineContext } from './testUtils';

describe('AnalyticsPage', () => {
  it('renders real metric cards', () => {
    render(withEngineContext({ snapshot: sampleSnapshot }, <AnalyticsPage />));
    expect(screen.getByText('Rescues')).toBeInTheDocument();
    expect(screen.getByText('Ticks')).toBeInTheDocument();
  });

  it('renders a real events-by-category breakdown from recentEvents (Phase 12)', () => {
    render(withEngineContext({ snapshot: sampleSnapshot }, <AnalyticsPage />));
    expect(screen.getByText(/Events by Category/)).toBeInTheDocument();
  });

  it('omits the breakdown section when there are no recorded events', () => {
    render(withEngineContext({ snapshot: { ...sampleSnapshot, recentEvents: [] } }, <AnalyticsPage />));
    expect(screen.queryByText(/Events by Category/)).not.toBeInTheDocument();
  });
});
