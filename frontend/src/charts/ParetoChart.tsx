import { useState } from 'react';

import { formatPercent } from '../utils/format';
import type { ParetoPointView } from '../types/engine';

export interface ParetoChartProps {
  readonly points: readonly ParetoPointView[];
}

const WIDTH = 480;
const HEIGHT = 320;
const PAD = 40;

/** Brief §39-41: X = rescue time, Y = fuel cost, point size = risk. Reads
 * `ParetoPointView[]` directly from the engine snapshot -- no dominance calculation
 * happens here, `dominated`/`recommended` are already engine-computed booleans. */
export function ParetoChart({ points }: ParetoChartProps) {
  const [hovered, setHovered] = useState<ParetoPointView | null>(null);

  if (points.length === 0) {
    return <p className="muted">No Pareto front available for the current selection.</p>;
  }

  const times = points.map((p) => p.cost.time);
  const fuels = points.map((p) => p.cost.fuel);
  const minTime = Math.min(...times);
  const maxTime = Math.max(...times);
  const minFuel = Math.min(...fuels);
  const maxFuel = Math.max(...fuels);
  const timeSpan = maxTime - minTime || 1;
  const fuelSpan = maxFuel - minFuel || 1;

  const xFor = (t: number) => PAD + ((t - minTime) / timeSpan) * (WIDTH - 2 * PAD);
  const yFor = (f: number) => HEIGHT - PAD - ((f - minFuel) / fuelSpan) * (HEIGHT - 2 * PAD);

  return (
    <div className="pareto-chart">
      <svg viewBox={`0 0 ${WIDTH} ${HEIGHT}`} role="img" aria-label="Pareto front: time vs fuel">
        <line x1={PAD} y1={HEIGHT - PAD} x2={WIDTH - PAD} y2={HEIGHT - PAD} className="chart-axis" />
        <line x1={PAD} y1={PAD} x2={PAD} y2={HEIGHT - PAD} className="chart-axis" />
        <text x={WIDTH / 2} y={HEIGHT - 8} className="chart-axis-label" textAnchor="middle">
          Rescue Time (s)
        </text>
        <text
          x={12}
          y={HEIGHT / 2}
          className="chart-axis-label"
          textAnchor="middle"
          transform={`rotate(-90 12 ${HEIGHT / 2})`}
        >
          Fuel Cost
        </text>

        {points.map((p) => {
          const r = p.recommended ? 8 : 4 + p.cost.risk * 6;
          return (
            <circle
              key={p.planId}
              cx={xFor(p.cost.time)}
              cy={yFor(p.cost.fuel)}
              r={r}
              className={`pareto-point${p.dominated ? ' pareto-point-dominated' : ''}${p.recommended ? ' pareto-point-recommended' : ''}`}
              onMouseEnter={() => setHovered(p)}
              onMouseLeave={() => setHovered((cur) => (cur?.planId === p.planId ? null : cur))}
            />
          );
        })}
      </svg>

      <div className="pareto-detail">
        {hovered ? (
          <dl>
            <dt>Plan</dt>
            <dd>{hovered.planId}</dd>
            <dt>Time</dt>
            <dd>{hovered.cost.time.toFixed(1)} s</dd>
            <dt>Fuel</dt>
            <dd>{formatPercent(hovered.cost.fuel)}</dd>
            <dt>Risk</dt>
            <dd>{formatPercent(hovered.cost.risk)}</dd>
            <dt>Distance</dt>
            <dd>{hovered.cost.distance.toFixed(0)} km</dd>
            <dt>Status</dt>
            <dd>{hovered.recommended ? 'Recommended' : hovered.dominated ? 'Dominated' : 'Non-dominated'}</dd>
          </dl>
        ) : (
          <p className="muted">Hover a point to inspect its plan.</p>
        )}
      </div>
    </div>
  );
}
