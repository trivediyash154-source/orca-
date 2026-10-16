import { useEngine } from '../../state/EngineContext';
import { formatSimTime } from '../../utils/format';
import { downloadTextFile, missionsToCsv, missionsToJson } from '../../utils/exportMissionHistory';

/** Brief §47: current in-memory session history only -- the engine has no persistence
 * layer, so `snapshot.missions` (which retains terminal missions, never pruned) is the
 * complete, honest history for this run. Export (brief §60) downloads exactly this same
 * data, client-side -- no server round-trip, nothing added beyond what the table shows. */
export function MissionHistoryPage() {
  const { snapshot, select } = useEngine();

  if (!snapshot) {
    return <div className="page-placeholder">No snapshot yet.</div>;
  }

  if (snapshot.missions.length === 0) {
    return <p className="muted">No missions have been created yet this run.</p>;
  }

  const missions = snapshot.missions;

  return (
    <div className="mission-history-page">
      <div className="mission-history-export">
        <button onClick={() => downloadTextFile('orca-mission-history.csv', missionsToCsv(missions), 'text/csv')}>
          Export CSV
        </button>
        <button
          onClick={() =>
            downloadTextFile('orca-mission-history.json', missionsToJson(missions), 'application/json')
          }
        >
          Export JSON
        </button>
      </div>
      <table className="data-table">
        <thead>
          <tr>
            <th>Mission</th>
            <th>Vehicle</th>
            <th>Emergency</th>
            <th>State</th>
            <th>Started</th>
            <th>Completed</th>
            <th>Replans</th>
          </tr>
        </thead>
        <tbody>
          {missions.map((m) => (
            <tr key={m.id} onClick={() => select({ kind: 'mission', id: m.id })} className="data-row-clickable">
              <td>{m.id}</td>
              <td>{m.vehicleId}</td>
              <td>{m.emergencyId}</td>
              <td>
                <span className={`state-badge state-badge-${m.state.toLowerCase()}`}>{m.state}</span>
              </td>
              <td>{formatSimTime(m.startedAtSec)}</td>
              <td>{m.completedAtSec !== null ? formatSimTime(m.completedAtSec) : '—'}</td>
              <td>{m.replanCount}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
