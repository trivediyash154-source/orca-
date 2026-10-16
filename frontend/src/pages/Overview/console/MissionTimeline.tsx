import { useMemo } from 'react';

import { formatClock } from '../../../components/charts/primitives';
import type { SimulationSnapshot } from '../../../types/engine';

/**
 * Missions on a shared clock: one row per mission, each transfer leg a bar from departure to
 * arrival, the emergency's survival deadline as a diamond on the same row, and "now" as one line
 * through all of them. The distance between the end of the last bar and the diamond is the
 * rescue's margin — read at a glance, without a table.
 */

const W = 1000;
const LEFT = 196;
const RIGHT = 24;
const ROW = 34;
const TOP = 34;
const BEFORE_SEC = 3_600;
const AFTER_SEC = 5_400;

export function MissionTimeline({ snapshot }: { readonly snapshot: SimulationSnapshot }) {
  const now = snapshot.simulationTimeSec;
  const t0 = now - BEFORE_SEC;
  const t1 = now + AFTER_SEC;
  const sx = (t: number) => LEFT + ((Math.min(t1, Math.max(t0, t)) - t0) / (t1 - t0)) * (W - LEFT - RIGHT);

  const rows = useMemo(() => {
    return snapshot.missions
      .filter((m) => {
        const end = m.completedAtSec ?? m.legs.at(-1)?.arrivalTimeSec ?? now;
        return end >= t0 && m.startedAtSec <= t1;
      })
      .sort((a, b) => b.startedAtSec - a.startedAtSec)
      .slice(0, 8);
  }, [snapshot.missions, now, t0, t1]);

  const ticks: number[] = [];
  const step = 1_800;
  for (let t = Math.ceil(t0 / step) * step; t <= t1; t += step) ticks.push(t);

  const height = TOP + Math.max(1, rows.length) * ROW + 28;

  return (
    <figure className="cx-timeline">
      <svg viewBox={`0 0 ${W} ${height}`} role="img" aria-label={`Mission timeline: ${rows.length} missions around T+${formatClock(now)}`}>
        <g className="cx-timeline__axis">
          {ticks.map((t) => (
            <g key={t}>
              <line x1={sx(t)} x2={sx(t)} y1={TOP - 8} y2={height - 24} />
              <text x={sx(t)} y={TOP - 14} textAnchor="middle">T+{formatClock(t).slice(0, 5)}</text>
            </g>
          ))}
        </g>

        {rows.length === 0 ? (
          <text x={LEFT} y={TOP + 22} className="cx-timeline__empty">No missions in this window.</text>
        ) : null}

        {rows.map((m, i) => {
          const yy = TOP + i * ROW;
          const emergency = snapshot.emergencies.find((e) => e.id === m.emergencyId);
          const subject = snapshot.satellites.find((s) => s.id === emergency?.subjectId);
          const replanAt = m.paretoHistory.length > 1 ? m.paretoHistory.at(-1)?.timestampSec : undefined;
          return (
            <g key={m.id} className="cx-timeline__row">
              <rect x="0" y={yy} width={W} height={ROW} className={i % 2 === 0 ? 'cx-timeline__band' : 'cx-timeline__band cx-timeline__band--alt'} />
              <text x="12" y={yy + 15} className="cx-timeline__id">{m.id}</text>
              <text x="12" y={yy + 28} className="cx-timeline__route">
                {m.vehicleId} → {subject?.name ?? emergency?.subjectId ?? '—'}
              </text>
              {m.legs.map((leg, k) => {
                const xa = sx(leg.departureTimeSec);
                const xb = sx(leg.arrivalTimeSec);
                const progress =
                  leg.status === 'current'
                    ? Math.min(1, Math.max(0, (now - leg.departureTimeSec) / Math.max(1, leg.arrivalTimeSec - leg.departureTimeSec)))
                    : leg.status === 'completed'
                      ? 1
                      : 0;
                return (
                  <g key={k}>
                    <rect x={xa} y={yy + 9} width={Math.max(2, xb - xa)} height="16" rx="3" className={`cx-timeline__leg cx-timeline__leg--${leg.status}`}>
                      <title>{`${leg.fromEntityId} → ${leg.toEntityId}: T+${formatClock(leg.departureTimeSec)} to T+${formatClock(leg.arrivalTimeSec)}, ${leg.deltaVKms.toFixed(3)} km/s`}</title>
                    </rect>
                    {progress > 0 && progress < 1 ? (
                      <rect x={xa} y={yy + 9} width={Math.max(1, (xb - xa) * progress)} height="16" rx="3" className="cx-timeline__fill" />
                    ) : null}
                  </g>
                );
              })}
              {replanAt !== undefined ? (
                <g transform={`translate(${sx(replanAt)} ${yy + 6})`} className="cx-timeline__replan">
                  <path d="M-5 0L5 0L0 6Z" />
                  <title>{`Replanned at T+${formatClock(replanAt)}`}</title>
                </g>
              ) : null}
              {emergency ? (
                <g transform={`translate(${sx(emergency.deadlineSec)} ${yy + 17})`} className="cx-timeline__deadline">
                  <path d="M0 -7L7 0L0 7L-7 0Z" />
                  <title>{`Survival deadline T+${formatClock(emergency.deadlineSec)}`}</title>
                </g>
              ) : null}
            </g>
          );
        })}

        <g className="cx-timeline__now">
          <line x1={sx(now)} x2={sx(now)} y1={TOP - 8} y2={height - 24} />
          <text x={sx(now)} y={height - 8} textAnchor="middle">NOW · T+{formatClock(now)}</text>
        </g>
      </svg>
      <figcaption className="cx-legend">
        <span><i className="cx-bar cx-bar--completed" /> Leg flown</span>
        <span><i className="cx-bar cx-bar--current" /> Leg in progress</span>
        <span><i className="cx-bar cx-bar--upcoming" /> Leg planned</span>
        <span><i className="cx-diamond" /> Survival deadline</span>
        <span><i className="cx-replan" /> Replan</span>
      </figcaption>
    </figure>
  );
}
