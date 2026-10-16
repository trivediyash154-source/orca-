import { useEngine } from '../../state/EngineContext';
import { formatSimTime } from '../../utils/format';
import { eventCategory, eventGlyph } from './eventCategories';
import type { EventView, SelectableEntity } from '../../types/engine';

function selectableFor(e: EventView): SelectableEntity | null {
  if (e.satelliteId) return { kind: 'satellite', id: e.satelliteId };
  if (e.vehicleId) return { kind: 'vehicle', id: e.vehicleId };
  if (e.missionId) return { kind: 'mission', id: e.missionId };
  return null;
}

/** Brief §34-35: bottom event timeline, real engine events, click to select the related
 * object. `snapshot.recentEvents` is already chronological (`SimulationState.hpp`'s own
 * append-only log) -- nothing here re-sorts or re-derives order. */
export function EventTimeline() {
  const { snapshot, select } = useEngine();
  const events = snapshot?.recentEvents ?? [];

  if (events.length === 0) {
    return (
      <footer className="event-timeline">
        <span className="muted">No events yet.</span>
      </footer>
    );
  }

  return (
    <footer className="event-timeline" aria-label="Event timeline">
      <ol className="event-timeline-list">
        {events.map((e) => {
          const target = selectableFor(e);
          return (
            <li key={e.id} className={`event-item event-cat-${eventCategory(e.kind)}`}>
              <button
                className="event-item-button"
                onClick={() => target && select(target)}
                disabled={!target}
                title={e.detail}
              >
                <span className="event-time">{formatSimTime(e.timestampSec)}</span>
                <span className="event-glyph" aria-hidden="true">
                  {eventGlyph(e.kind)}
                </span>
                <span className="event-label">{e.kind.replaceAll('_', ' ')}</span>
              </button>
            </li>
          );
        })}
      </ol>
    </footer>
  );
}
