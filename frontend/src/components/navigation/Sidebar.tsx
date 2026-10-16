import { useUI, type PageId } from '../../state/UIContext';

const NAV_ITEMS: ReadonlyArray<{ id: PageId; label: string; glyph: string; implemented: boolean }> = [
  { id: 'overview', label: 'Dashboard', glyph: '▤', implemented: true },
  { id: 'mission-control', label: 'Mission Control', glyph: '◉', implemented: true },
  { id: 'mission-planner', label: 'Mission Planner', glyph: '⌘', implemented: false },
  { id: 'orbital-catalog', label: 'Orbital Catalog', glyph: '✦', implemented: true },
  { id: 'orbital-network', label: 'Orbital Network', glyph: '⬡', implemented: true },
  { id: 'optimization-lab', label: 'Optimization Lab', glyph: '◈', implemented: true },
  { id: 'algorithm-lab', label: 'Algorithm Lab', glyph: '⚙', implemented: false },
  { id: 'analytics', label: 'Analytics', glyph: '▦', implemented: true },
  { id: 'mission-history', label: 'Mission History', glyph: '⧖', implemented: true },
  { id: 'scenario', label: 'Scenario', glyph: '▢', implemented: true },
  { id: 'settings', label: 'Settings', glyph: '⚙', implemented: true },
];

/** Brief §10: sidebar navigation. Pages with only placeholder-depth data are marked, not
 * hidden -- clicking still works, they just say so honestly (brief §10's "do not implement
 * pages that have no backend data yet beyond sensible placeholders" is about content depth,
 * not about withholding navigation entirely). */
export function Sidebar() {
  const { page, setPage } = useUI();

  return (
    <nav className="sidebar-nav" aria-label="Main navigation">
      {NAV_ITEMS.map((item) => (
        <button
          key={item.id}
          className={`nav-item${page === item.id ? ' nav-item-active' : ''}`}
          onClick={() => setPage(item.id)}
          aria-current={page === item.id ? 'page' : undefined}
        >
          <span className="nav-glyph" aria-hidden="true">
            {item.glyph}
          </span>
          <span className="nav-label">{item.label}</span>
        </button>
      ))}
    </nav>
  );
}
