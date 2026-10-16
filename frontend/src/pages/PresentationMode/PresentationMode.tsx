import { useEffect } from 'react';

import { useEngine } from '../../state/EngineContext';
import { useUI } from '../../state/UIContext';
import { useRelevantMission } from '../../components/mission/MissionIntelligence';
import { Scene } from '../../visualization/scene/Scene';

/** Phase 11 brief §61's presentation-mode keyboard shortcuts -- every key maps to a REAL
 * command already exposed by `useEngine()`, never a locally-simulated effect. `SimulationClock::start()`
 * (`cpp/simulation/SimulationClock.cpp`) sets `running_ = true` unconditionally, so `start`
 * is safe to send even when the sim was previously paused (identical effect to `resume`
 * here) -- Space does not need to track which of the two to send. */
export function usePresentationKeyboardShortcuts(): void {
  const { sendCommand, setSpeed, snapshot } = useEngine();
  const { setPresentationMode } = useUI();
  const running = snapshot?.simulationStatus === 'RUNNING';

  useEffect(() => {
    function onKeyDown(e: KeyboardEvent): void {
      switch (e.key) {
        case ' ':
          e.preventDefault();
          void sendCommand(running ? 'pause' : 'start');
          break;
        case 'r':
        case 'R':
          void sendCommand('reset');
          break;
        case 's':
        case 'S':
          void sendCommand('step');
          break;
        case '1':
          void setSpeed(1);
          break;
        case '2':
          void setSpeed(4);
          break;
        case '3':
          void setSpeed(16);
          break;
        case 'Escape':
          setPresentationMode(false);
          break;
        default:
          break;
      }
    }
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [sendCommand, setSpeed, setPresentationMode, running]);
}

/** Brief §50-51: minimal UI, maximum Earth/emergency/trajectory/status -- for presenting
 * to a professor. Real values only; the large banner reflects the actual relevant
 * mission's actual state, never a scripted narration. */
export function PresentationModeView() {
  const { setPresentationMode } = useUI();
  const { snapshot } = useEngine();
  const mission = useRelevantMission();
  const emergency = mission && snapshot ? snapshot.emergencies.find((e) => e.id === mission.emergencyId) : null;
  usePresentationKeyboardShortcuts();

  const statusLine = !mission
    ? 'STANDING BY'
    : mission.state === 'SUCCESS'
      ? 'RESCUE COMPLETE'
      : mission.replanCount > 0
        ? 'MISSION ACTIVE — REPLANNED'
        : 'MISSION ACTIVE';

  return (
    <div className="presentation-mode">
      <button className="presentation-exit" onClick={() => setPresentationMode(false)} aria-label="Exit presentation mode">
        ✕ Exit
      </button>
      <div className="presentation-scene">
        <Scene />
      </div>
      <div className="presentation-hud">
        <span className="presentation-brand">ORCA</span>
        {emergency && mission && (
          <div className="presentation-parties">
            <span className="presentation-party presentation-party-critical">{emergency.subjectId}</span>
            <span className="presentation-arrow">→</span>
            <span className="presentation-party presentation-party-rescue">{mission.vehicleId}</span>
          </div>
        )}
        <span className="presentation-status">{statusLine}</span>
      </div>
      <div className="presentation-shortcuts" aria-hidden="true">
        Space start/pause · R reset · S step · 1/2/3 speed · Esc exit
      </div>
    </div>
  );
}
