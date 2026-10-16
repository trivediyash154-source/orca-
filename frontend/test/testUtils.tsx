import type { ReactNode } from 'react';

import { EngineContext, type ConnectionStatus, type EngineContextValue } from '../src/state/EngineContext';
import type { SimulationSnapshot } from '../src/types/engine';

export function makeContextValue(overrides: Partial<EngineContextValue> = {}): EngineContextValue {
  return {
    baseUrl: 'http://localhost:4000',
    connectionStatus: 'Connected' as ConnectionStatus,
    snapshot: null as SimulationSnapshot | null,
    selected: null,
    select: () => {},
    sendCommand: async () => {},
    advance: async () => {},
    setSpeed: async () => {},
    rescoreParetoFront: async () => {},
    runAlgorithm: async () => true,
    lastError: null,
    dataSource: 'engine',
    ...overrides,
  };
}

export function withEngineContext(value: Partial<EngineContextValue>, children: ReactNode) {
  return <EngineContext.Provider value={makeContextValue(value)}>{children}</EngineContext.Provider>;
}
