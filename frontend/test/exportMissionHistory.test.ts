import { describe, expect, it } from 'vitest';

import { missionsToCsv, missionsToJson } from '../src/utils/exportMissionHistory';
import { sampleSnapshot } from './fixtures';

describe('exportMissionHistory', () => {
  it('missionsToCsv produces a header plus one row per mission with the real field values', () => {
    const csv = missionsToCsv(sampleSnapshot.missions);
    const lines = csv.trim().split('\n');
    expect(lines[0]).toBe('id,vehicleId,emergencyId,state,startedAtSec,completedAtSec,replanCount');
    expect(lines).toHaveLength(1 + sampleSnapshot.missions.length);
    expect(lines[1]).toBe('M-000,RV-01,E-000,IN_TRANSIT,5,,0');
  });

  it('missionsToCsv escapes values containing commas or quotes', () => {
    const csv = missionsToCsv([{ ...sampleSnapshot.missions[0]!, id: 'M-000,with"quote' }]);
    expect(csv).toContain('"M-000,with""quote"');
  });

  it('missionsToJson round-trips the real mission objects', () => {
    const json = missionsToJson(sampleSnapshot.missions);
    expect(JSON.parse(json)).toEqual(sampleSnapshot.missions);
  });

  it('missionsToCsv on an empty list produces only the header', () => {
    const csv = missionsToCsv([]);
    expect(csv.trim()).toBe('id,vehicleId,emergencyId,state,startedAtSec,completedAtSec,replanCount');
  });
});
