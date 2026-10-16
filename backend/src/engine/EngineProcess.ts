import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import { EventEmitter } from 'node:events';

import { isParseFailure, NdjsonLineParser } from '../ipc/ndjsonParser.js';
import {
  isErrorMessage,
  isReadyMessage,
  isSnapshotMessage,
  type EngineCommand,
  type EngineCommandName,
  type EngineMessage,
  type ErrorMessage,
  type ReadyMessage,
  type SnapshotMessage,
} from '../ipc/protocol.js';

export type EngineStatus = 'STOPPED' | 'STARTING' | 'READY' | 'CRASHED';

interface PendingRequest {
  resolve: (msg: SnapshotMessage) => void;
  reject: (err: Error) => void;
  timer: NodeJS.Timeout;
}

/**
 * Owns the C++ engine child process — spawning it, speaking NDJSON over its stdin/stdout,
 * and correlating command → response by `reqId`. This is the ONLY place in the backend that
 * touches the child process directly; `api/server.ts` only ever calls `sendCommand()` and
 * reads `getLatestSnapshot()`.
 *
 * No planning/simulation logic lives here (brief §22, §58) — every method either forwards a
 * command byte-for-byte or reports state the engine already computed.
 */
export class EngineProcess extends EventEmitter {
  private child: ChildProcessWithoutNullStreams | null = null;
  private readonly parser = new NdjsonLineParser();
  private status: EngineStatus = 'STOPPED';
  private latestSnapshot: SnapshotMessage | null = null;
  private ready: ReadyMessage | null = null;
  private nextReqId = 1;
  private readonly pending = new Map<number, PendingRequest>();
  private readonly stderrTail: string[] = [];
  private static readonly kMaxStderrTail = 20;

  constructor(
    private readonly enginePath: string,
    private readonly startupTimeoutMs: number,
    private readonly commandTimeoutMs: number,
    /**
     * `<family>:<size>[:<seed>[:<failures>]]`, or null for the fixed demo scenario.
     *
     * Optional and last so every existing construction site -- the test suite builds these
     * directly -- keeps working unchanged and keeps getting the demo scenario it asserts on.
     */
    private readonly scenarioSpec: string | null = null,
  ) {
    super();
  }

  getStatus(): EngineStatus {
    return this.status;
  }
  /** The compiled binary's own path -- exposed so `api/server.ts` can spawn a SEPARATE,
   * one-shot `--visualScenario` process (Phase 12 brief §5) using the same real binary,
   * without this class growing a second responsibility for a request/response subprocess
   * that has nothing to do with the persistent NDJSON session this class owns. */
  getEnginePath(): string {
    return this.enginePath;
  }
  getLatestSnapshot(): SnapshotMessage | null {
    return this.latestSnapshot;
  }
  getReady(): ReadyMessage | null {
    return this.ready;
  }
  getStderrTail(): readonly string[] {
    return [...this.stderrTail];
  }

  /** Spawns the engine and waits for its `ready` message. @throws if the process fails to
   * spawn, exits before announcing readiness, or exceeds `startupTimeoutMs`. */
  async start(): Promise<ReadyMessage> {
    if (this.child) {
      throw new Error('EngineProcess.start: already started');
    }
    this.status = 'STARTING';

    // `--serve` alone boots the small fixed demo scenario (3 satellites). A scenario spec
    // boots a generated one -- hundreds of objects WITH a deterministic failure schedule, so
    // Mission Control shows the rescue loop rather than a static population.
    const args = this.scenarioSpec === null ? ['--serve'] : ['--serve', this.scenarioSpec];
    const child = spawn(this.enginePath, args, { stdio: ['pipe', 'pipe', 'pipe'] });
    this.child = child;

    child.stdout.setEncoding('utf8');
    child.stdout.on('data', (chunk: string) => this.handleStdout(chunk));
    child.stderr.setEncoding('utf8');
    child.stderr.on('data', (chunk: string) => this.handleStderr(chunk));
    child.on('exit', (code, signal) => this.handleExit(code, signal));
    child.on('error', (err) => this.handleSpawnError(err));
    // A real bug, found by an actual mid-session crash test (not a flaky test): if the
    // engine process dies (crash, kill signal) with a write already in flight or about to
    // happen, `child.stdin` itself emits its own 'error' (EPIPE) event -- distinct from the
    // ChildProcess-level 'error' above, and Node's EventEmitter contract means an
    // unlistened 'error' event THROWS, crashing this bridge process. `sendCommand()`'s own
    // write callback already handles the same EPIPE for the request that was in flight;
    // this handler exists only so the stream itself never gets to throw uncaught.
    child.stdin.on('error', () => {
      // Intentionally empty -- `handleExit()` (via the process 'exit' event, which always
      // fires for a dead child) is what actually rejects pending requests and flips
      // status to CRASHED. This handler's only job is to stop the EPIPE from being
      // unlistened.
    });

    return new Promise<ReadyMessage>((resolve, reject) => {
      const timer = setTimeout(() => {
        reject(new Error(`engine did not send 'ready' within ${this.startupTimeoutMs}ms`));
      }, this.startupTimeoutMs);
      this.once('ready', (msg: ReadyMessage) => {
        clearTimeout(timer);
        resolve(msg);
      });
      this.once('spawnError', (err: Error) => {
        clearTimeout(timer);
        reject(err);
      });
      this.once('exit', () => {
        clearTimeout(timer);
        reject(new Error('engine process exited before announcing readiness'));
      });
    });
  }

  private handleStdout(chunk: string): void {
    const items = this.parser.push(chunk);
    for (const item of items) {
      if (isParseFailure(item)) {
        this.emit('protocolError', item.error, item.raw);
        continue;
      }
      const msg = item.value as EngineMessage;
      if (isReadyMessage(msg)) {
        this.status = 'READY';
        this.ready = msg;
        this.emit('ready', msg);
      } else if (isSnapshotMessage(msg)) {
        this.latestSnapshot = msg;
        this.emit('snapshot', msg);
        this.resolvePending(msg.reqId, msg, null);
      } else if (isErrorMessage(msg)) {
        this.emit('engineError', msg);
        this.resolvePending(msg.reqId, null, msg);
      } else {
        this.emit('protocolError', new Error('unrecognized message type'), item.raw);
      }
    }
  }

  private resolvePending(
    reqId: number | undefined,
    snapshot: SnapshotMessage | null,
    error: ErrorMessage | null,
  ): void {
    if (reqId === undefined) return;
    const pending = this.pending.get(reqId);
    if (!pending) return;
    this.pending.delete(reqId);
    clearTimeout(pending.timer);
    if (snapshot) {
      pending.resolve(snapshot);
    } else if (error) {
      pending.reject(new Error(`[${error.code}] ${error.message}`));
    }
  }

  private handleStderr(chunk: string): void {
    for (const line of chunk.split('\n')) {
      if (line.length === 0) continue;
      this.stderrTail.push(line);
      if (this.stderrTail.length > EngineProcess.kMaxStderrTail) {
        this.stderrTail.shift();
      }
    }
    this.emit('stderr', chunk);
  }

  private handleExit(code: number | null, signal: NodeJS.Signals | null): void {
    this.status = 'CRASHED';
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(new Error(`engine process exited (code=${code}, signal=${signal}) before responding`));
    }
    this.pending.clear();
    this.child = null;
    this.emit('exit', code, signal);
  }

  private handleSpawnError(err: Error): void {
    this.status = 'CRASHED';
    this.emit('spawnError', err);
  }

  /** Sends one command and resolves with the snapshot that answers it (or rejects on a
   * protocol-level error / timeout / the process not currently being up). Every command this
   * project's engine supports responds with a snapshot (`CommandProtocol.hpp`), so this is
   * the one request/response shape every caller needs. */
  async sendCommand(
    cmd: EngineCommandName,
    params: Partial<
      Pick<EngineCommand, 'seconds' | 'multiplier' | 'missionId' | 'profile' | 'algorithm' | 'vehicleId' | 'emergencyId'>
    > = {},
  ): Promise<SnapshotMessage> {
    const child = this.child;
    if (!child || this.status !== 'READY') {
      throw new Error('EngineProcess.sendCommand: engine is not running');
    }
    const reqId = this.nextReqId++;
    const command: EngineCommand = { cmd, reqId, ...params };
    const line = JSON.stringify(command) + '\n';

    return new Promise<SnapshotMessage>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(reqId);
        reject(new Error(`command '${cmd}' (reqId=${reqId}) timed out after ${this.commandTimeoutMs}ms`));
      }, this.commandTimeoutMs);
      this.pending.set(reqId, { resolve, reject, timer });
      child.stdin.write(line, (err) => {
        if (err) {
          clearTimeout(timer);
          this.pending.delete(reqId);
          reject(err);
        }
      });
    });
  }

  /** Sends `shutdown` and gives the process `graceMs` to exit on its own before
   * force-killing it — brief §81's clean-shutdown requirement: no orphaned engine process. */
  async shutdown(graceMs = 2000): Promise<void> {
    const child = this.child;
    if (!child) return;
    try {
      child.stdin.write(JSON.stringify({ cmd: 'shutdown' }) + '\n');
    } catch {
      // stdin may already be closed if the engine crashed; the timeout below covers it
    }
    await new Promise<void>((resolve) => {
      const timer = setTimeout(() => {
        if (!child.killed) child.kill('SIGTERM');
        resolve();
      }, graceMs);
      child.once('exit', () => {
        clearTimeout(timer);
        resolve();
      });
    });
  }
}
