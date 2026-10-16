import { useState } from 'react';

import { useEngine } from '../state/EngineContext';

const SPEED_OPTIONS = [0.25, 0.5, 1, 2, 4, 8, 16];

/** Brief §46-47: buttons send REAL commands to the engine; nothing is simulated locally.
 * Every button here maps to exactly one command `cpp/io/CommandProtocol.cpp` recognizes. */
export function ControlPanel() {
  const { snapshot, sendCommand, advance, setSpeed, connectionStatus } = useEngine();
  const [advanceSeconds, setAdvanceSeconds] = useState(30);
  const disabled = connectionStatus !== 'Connected';
  const running = snapshot?.simulationStatus === 'RUNNING';

  return (
    <section className="control-panel">
      <h2>Controls</h2>
      <div className="control-row">
        <button disabled={disabled || running} onClick={() => void sendCommand('start')}>
          ▶ Start
        </button>
        <button disabled={disabled || !running} onClick={() => void sendCommand('pause')}>
          ⏸ Pause
        </button>
        <button disabled={disabled || running} onClick={() => void sendCommand('resume')}>
          ⏵ Resume
        </button>
        <button disabled={disabled} onClick={() => void sendCommand('reset')}>
          ↻ Reset
        </button>
      </div>
      <div className="control-row">
        <button disabled={disabled} onClick={() => void sendCommand('step')}>
          Step
        </button>
        <input
          type="number"
          min={1}
          value={advanceSeconds}
          onChange={(e) => setAdvanceSeconds(Number(e.target.value))}
          aria-label="Advance seconds"
        />
        <button disabled={disabled} onClick={() => void advance(advanceSeconds)}>
          Advance {advanceSeconds}s
        </button>
      </div>
      <div className="control-row">
        <label htmlFor="speed-select">Speed</label>
        <select
          id="speed-select"
          disabled={disabled}
          value={snapshot?.speedMultiplier ?? 1}
          onChange={(e) => void setSpeed(Number(e.target.value))}
        >
          {SPEED_OPTIONS.map((s) => (
            <option key={s} value={s}>
              {s}x
            </option>
          ))}
        </select>
      </div>
    </section>
  );
}
