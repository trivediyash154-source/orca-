import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { afterEach, describe, expect, it } from 'vitest';

import { EngineProcess } from '../src/engine/EngineProcess.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '..', '..');
const enginePath = path.join(repoRoot, 'build-direct', 'orca_engine');

describe('EngineProcess (against the real compiled C++ engine)', () => {
  let engine: EngineProcess | null = null;

  afterEach(async () => {
    if (engine) {
      await engine.shutdown(500);
      engine = null;
    }
  });

  it('launches the engine and receives a ready message', async () => {
    engine = new EngineProcess(enginePath, 5000, 5000);
    const ready = await engine.start();
    expect(ready.type).toBe('ready');
    expect(ready.schemaVersion).toBe(1);
    expect(ready.capabilities).toContain('advance');
    expect(engine.getStatus()).toBe('READY');
  });

  it('accepts a command and returns a snapshot answering it', async () => {
    engine = new EngineProcess(enginePath, 5000, 5000);
    await engine.start();
    const snapshot = await engine.sendCommand('start');
    expect(snapshot.type).toBe('snapshot');
    expect(snapshot.simulationStatus).toBe('RUNNING');
  });

  it('advances the simulation and the snapshot time changes', async () => {
    engine = new EngineProcess(enginePath, 5000, 5000);
    await engine.start();
    await engine.sendCommand('start');
    const before = await engine.sendCommand('getSnapshot');
    const after = await engine.sendCommand('advance', { seconds: 30 });
    expect(after.simulationTimeSec).toBeGreaterThan(before.simulationTimeSec);
  });

  it('satellite positions actually change as simulation time advances', async () => {
    engine = new EngineProcess(enginePath, 5000, 5000);
    await engine.start();
    await engine.sendCommand('start');
    const before = await engine.sendCommand('getSnapshot');
    const after = await engine.sendCommand('advance', { seconds: 100 });
    const beforePos = before.satellites[0]!.position;
    const afterPos = after.satellites[0]!.position;
    expect(afterPos.x !== beforePos.x || afterPos.y !== beforePos.y || afterPos.z !== beforePos.z).toBe(
      true,
    );
  });

  it('reset returns the simulation to its exact initial state', async () => {
    engine = new EngineProcess(enginePath, 5000, 5000);
    await engine.start();
    await engine.sendCommand('start');
    await engine.sendCommand('advance', { seconds: 50 });
    const afterReset = await engine.sendCommand('reset');
    expect(afterReset.simulationTimeSec).toBe(0);
    expect(afterReset.missions).toHaveLength(0);
    expect(afterReset.emergencies).toHaveLength(0);
    expect(afterReset.metrics.ticks).toBe(0);
  });

  it('the full scripted narrative runs: failure -> mission -> debris reroute -> completion', async () => {
    engine = new EngineProcess(enginePath, 5000, 20000);
    await engine.start();
    await engine.sendCommand('start');
    await engine.sendCommand('advance', { seconds: 30 }); // t=5 failure, mission dispatched
    const midRun = await engine.sendCommand('getSnapshot');
    expect(midRun.missions).toHaveLength(1);
    expect(midRun.emergencies).toHaveLength(1);

    const final = await engine.sendCommand('advance', { seconds: 600000 - midRun.simulationTimeSec });
    expect(final.missions[0]!.state).toBe('SUCCESS');
    expect(final.metrics.missionSuccesses).toBe(1);
  });

  it('rejects an unrecognized command with a structured protocol error, not a crash', async () => {
    engine = new EngineProcess(enginePath, 5000, 5000);
    let sawError: Error | null = null;
    engine.on('engineError', (msg: { code: string }) => {
      sawError = new Error(msg.code);
    });
    await engine.start();
    // Bypass sendCommand's own command-name typing to simulate a malformed/unsupported
    // command reaching the engine directly, matching brief §51's "invalid command" case.
    const child = (engine as unknown as { child: { stdin: { write: (s: string) => void } } }).child;
    child.stdin.write(JSON.stringify({ cmd: 'plan', reqId: 999 }) + '\n');
    await new Promise((resolve) => setTimeout(resolve, 300));
    expect(sawError).not.toBeNull();
    expect(engine.getStatus()).toBe('READY'); // still alive -- one bad command must not crash it
  });

  it('rejects sendCommand with a clear error when the engine binary does not exist', async () => {
    const bad = new EngineProcess('/nonexistent/orca_engine_binary', 1000, 1000);
    await expect(bad.start()).rejects.toThrow();
    expect(bad.getStatus()).toBe('CRASHED');
  });

  it('detects a real mid-session crash (the process dying after READY, not just failing to start)', async () => {
    engine = new EngineProcess(enginePath, 5000, 5000);
    await engine.start();
    await engine.sendCommand('start');
    expect(engine.getStatus()).toBe('READY');

    let sawExit = false;
    engine.on('exit', () => {
      sawExit = true;
    });
    // Send a command whose response will never arrive (the process is about to die), so the
    // pending-request rejection path (`EngineProcess.ts`'s `handleExit()`) is exercised for
    // real, not just the startup-failure path the previous test covers. A large `advance`
    // (matching `tests/simulation/simulation_engine_test.cpp`'s own `LongRunProducesNoInvalidState`
    // duration) is used deliberately, not a small one -- a cheap command can resolve before
    // the kill signal actually lands, which made an earlier version of this test racy
    // (observed failing: the promise resolved with a real snapshot instead of rejecting).
    const pendingCommand = engine.sendCommand('advance', { seconds: 600000 });
    const child = (engine as unknown as { child: { kill: (signal: string) => void; pid?: number } }).child;
    expect(child.pid).toBeGreaterThan(0);
    child.kill('SIGKILL');

    await expect(pendingCommand).rejects.toThrow(/exited/i);
    // handleExit() sets CRASHED synchronously, before the 'exit' event even needs to fire,
    // so both assertions hold once the kill has taken effect.
    await new Promise((resolve) => setTimeout(resolve, 200));
    expect(sawExit).toBe(true);
    expect(engine.getStatus()).toBe('CRASHED');
    engine = null; // already dead -- afterEach's shutdown() would just no-op on a null child
  });
});
