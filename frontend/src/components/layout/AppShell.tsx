import type { ReactNode } from 'react';

import { DataSourceBanner } from '../DataSourceBanner';
import { EngineOfflineOverlay } from '../EngineOfflineOverlay';
import { Sidebar } from '../navigation/Sidebar';
import { EventTimeline } from '../timeline/EventTimeline';
import { TopBar } from './TopBar';

export interface AppShellProps {
  readonly children: ReactNode;
}

/** Brief §9: TopBar / Sidebar / MainContent / EventTimeline. The right-hand
 * Mission-Intelligence column is a Mission-Control-specific concern, not global shell
 * layout, so it lives inside that page rather than here (other pages -- Network,
 * Optimization Lab -- do not want a mission panel crowding them, brief §69's "cards
 * stealing attention" is exactly what this avoids). */
export function AppShell({ children }: AppShellProps) {
  return (
    <div className="app-shell">
      <TopBar />
      <DataSourceBanner />
      <div className="app-body">
        <Sidebar />
        <main className="main-content">{children}</main>
      </div>
      <EventTimeline />
      <EngineOfflineOverlay />
    </div>
  );
}
