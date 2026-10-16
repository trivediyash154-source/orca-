import { createApp } from './api/server.js';
import { CatalogStore } from './catalog/CatalogStore.js';
import { loadConfig } from './config/index.js';
import { EngineProcess } from './engine/EngineProcess.js';

async function main(): Promise<void> {
  const config = loadConfig();
  const engine = new EngineProcess(
    config.enginePath,
    config.engineStartupTimeoutMs,
    config.commandTimeoutMs,
    config.engineScenario,
  );

  engine.on('protocolError', (err: Error, raw: string) => {
    console.error(`[bridge] protocol error: ${err.message} (raw: ${raw.slice(0, 200)})`);
  });
  engine.on('stderr', (chunk: string) => {
    process.stderr.write(`[engine] ${chunk}`);
  });
  engine.on('exit', (code: number | null, signal: NodeJS.Signals | null) => {
    console.error(`[bridge] engine process exited (code=${code}, signal=${signal})`);
  });

  try {
    const ready = await engine.start();
    console.log(
      `[bridge] engine ready: v${ready.engineVersion}, schema ${ready.schemaVersion}, ` +
        `scenario ${config.engineScenario ?? 'demo (scripted)'}`,
    );
    // Real bug found empirically (Phase 11's browser-based rendering measurement): without
    // this, `/snapshot` 503s until SOME client happens to send the first command, which
    // never happens on its own -- the frontend's `connectionStatus` never reaches
    // 'Connected', so every control (including the Start button that would normally send
    // that first command) stays permanently disabled. `getSnapshot` is a no-mutation
    // command that already exists for exactly this read; sending it once at boot breaks the
    // deadlock without inventing a new protocol message.
    await engine.sendCommand('getSnapshot');
  } catch (err) {
    console.error(`[bridge] engine failed to start: ${err instanceof Error ? err.message : String(err)}`);
    process.exitCode = 1;
    return;
  }

  // The catalog is independent of the engine on purpose: it is fetched from a public provider
  // rather than computed, so it must survive the engine failing to start. The sync is fired
  // without awaiting -- a 14 MB fetch from a third party should not hold the port closed, and
  // `GET /catalog/status` reports SYNCHRONISING honestly while it runs.
  const catalog = new CatalogStore({
    gpUrl: config.catalog.gpUrl,
    satcatUrl: config.catalog.satcatUrl,
    group: config.catalog.group,
    staleAfterHours: config.catalog.staleAfterHours,
    fetchTimeoutMs: config.catalog.fetchTimeoutMs,
    cacheDir: config.catalog.cacheDir,
    minRefetchMinutes: config.catalog.minRefetchMinutes,
    userAgent: config.catalog.userAgent,
    credentialledProvider: config.catalog.credentialledProvider,
  });
  if (config.catalog.syncOnStart) {
    void catalog
      .sync()
      .then((status) => {
        console.log(
          `[catalog] ${status.state}: ${status.objectCount} object(s) from group "${status.group}"` +
            (status.lastError ? ` (last error: ${status.lastError})` : ''),
        );
      })
      .catch((err: unknown) => {
        console.error(`[catalog] sync failed: ${err instanceof Error ? err.message : String(err)}`);
      });
  }

  const app = createApp(engine, config.benchmarksDir, catalog);
  const server = app.listen(config.port, () => {
    console.log(`[bridge] listening on http://localhost:${config.port}`);
  });

  // Brief §81: clean shutdown on either signal -- stop accepting new HTTP requests, tell
  // the engine to shut down, give it a grace period, then exit. No orphaned child process.
  let shuttingDown = false;
  const shutdown = (signal: NodeJS.Signals) => {
    if (shuttingDown) return;
    shuttingDown = true;
    console.log(`[bridge] received ${signal}, shutting down`);
    server.close();
    void engine.shutdown().then(() => process.exit(0));
  };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}

void main();
