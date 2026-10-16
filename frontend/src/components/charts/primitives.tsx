/**
 * Chart primitives for the workspace dashboard.
 *
 * Hand-written SVG rather than a charting library, for three reasons that all matter here.
 * A library's default palette is a palette this repository does not control, and `tokens.css`
 * is the single source of colour (`VISUAL_DESIGN.md` §2) -- these components paint in
 * `var(--orca-*)` only, so every chart follows the light/dark switch with no per-library theme
 * adapter. A library is also 50-150 kB for what amounts to `path` string construction. And an
 * SVG written here can carry its own `<title>`, `role` and a text alternative, which is the
 * part charting libraries most often get wrong.
 *
 * Every component is laid out in a fixed `viewBox` and scaled by CSS, so there is no resize
 * observer, no layout thrash and no measurement pass. Axis ticks are computed from the data
 * domain, never hardcoded.
 *
 * ## Reading the data
 *
 * None of these components invent, interpolate or extrapolate a value. An absent sample is a
 * gap in the line, not a zero -- a chart that draws a missing reading as zero is a chart that
 * reports an outage as a crash.
 */

import { useId, useMemo } from 'react';
import type { ReactNode } from 'react';

export interface Point {
  readonly x: number;
  /** `null` is a genuine gap: the series is broken here rather than drawn through. */
  readonly y: number | null;
}

export interface Series {
  readonly id: string;
  readonly label: string;
  /** A token NAME, e.g. `'--orca-primary'`. Not a colour literal: the chart must re-theme. */
  readonly token: string;
  readonly points: readonly Point[];
}

interface Domain {
  readonly min: number;
  readonly max: number;
}

function domainOf(values: readonly number[], padFraction = 0.08): Domain {
  if (values.length === 0) return { min: 0, max: 1 };
  let min = Math.min(...values);
  let max = Math.max(...values);
  if (min === max) {
    // A flat series still needs a band to be drawn in, or it collapses onto the axis and reads
    // as no data at all.
    const magnitude = Math.abs(min) || 1;
    min -= magnitude * 0.1;
    max += magnitude * 0.1;
  }
  const pad = (max - min) * padFraction;
  return { min: min - pad, max: max + pad };
}

/** "Nice" tick values (1, 2, 2.5, 5, 10 x 10^n), so an axis reads 0 / 250 / 500 rather than
 * 0 / 237 / 474. */
function niceTicks(domain: Domain, count: number): number[] {
  const span = domain.max - domain.min;
  if (span <= 0 || !Number.isFinite(span)) return [domain.min];

  const rough = span / Math.max(1, count);
  const magnitude = 10 ** Math.floor(Math.log10(rough));
  const normalised = rough / magnitude;
  const step =
    (normalised <= 1 ? 1 : normalised <= 2 ? 2 : normalised <= 2.5 ? 2.5 : normalised <= 5 ? 5 : 10) *
    magnitude;

  const first = Math.ceil(domain.min / step) * step;
  const ticks: number[] = [];
  for (let value = first; value <= domain.max + step * 0.001; value += step) {
    // Floating-point accumulation leaves 0.30000000000000004 on an axis label otherwise.
    ticks.push(Math.round(value / step) * step);
  }
  return ticks;
}

/** Drops ticks whose LABELS collide. A count axis spanning 0 to 2 otherwise prints "1, 1, 2"
 * after a one-decimal formatter rounds 0.5 and 1.0 to the same string, which reads as a broken
 * axis rather than as a rounding choice. */
function dedupeByLabel<T extends { readonly value: number }>(
  ticks: readonly T[],
  format: (value: number) => string,
): T[] {
  const seen = new Set<string>();
  return ticks.filter((tick) => {
    const label = format(tick.value);
    if (seen.has(label)) return false;
    seen.add(label);
    return true;
  });
}

export function formatCompact(value: number): string {
  const abs = Math.abs(value);
  if (abs >= 1_000_000) return `${(value / 1_000_000).toFixed(abs >= 10_000_000 ? 0 : 1)}M`;
  if (abs >= 1_000) return `${(value / 1_000).toFixed(abs >= 10_000 ? 0 : 1)}k`;
  if (abs >= 100) return value.toFixed(0);
  if (abs >= 10) return value.toFixed(1);
  if (abs >= 1) return value.toFixed(2);
  if (abs === 0) return '0';
  return value.toFixed(3);
}

export function formatClock(totalSeconds: number): string {
  const seconds = Math.max(0, Math.floor(totalSeconds));
  const h = Math.floor(seconds / 3_600);
  const m = Math.floor((seconds % 3_600) / 60);
  const s = seconds % 60;
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

/* ====================================================================== Panel ============== */

export interface ChartPanelProps {
  readonly title: string;
  readonly subtitle?: string;
  /** Units, counts, "live" markers -- anything that belongs in the header's right edge. */
  readonly meta?: ReactNode;
  readonly children: ReactNode;
  readonly footnote?: ReactNode;
  /** Spans two columns of the dashboard grid. */
  readonly wide?: boolean;
  readonly tall?: boolean;
}

export function ChartPanel({
  title,
  subtitle,
  meta,
  children,
  footnote,
  wide,
  tall,
}: ChartPanelProps) {
  const classes = ['dash-panel', wide ? 'dash-panel--wide' : null, tall ? 'dash-panel--tall' : null]
    .filter((entry): entry is string => entry !== null)
    .join(' ');

  return (
    <section className={classes}>
      <header className="dash-panel__head">
        <div>
          <h3 className="dash-panel__title">{title}</h3>
          {subtitle ? <p className="dash-panel__subtitle">{subtitle}</p> : null}
        </div>
        {meta ? <div className="dash-panel__meta">{meta}</div> : null}
      </header>
      <div className="dash-panel__body">{children}</div>
      {footnote ? <p className="dash-panel__footnote">{footnote}</p> : null}
    </section>
  );
}

/* ================================================================== Line / area ============ */

export interface LineChartProps {
  readonly series: readonly Series[];
  readonly yLabel?: string;
  readonly xLabel?: string;
  /** Formats an x tick. Defaults to `formatCompact`; time series pass `formatClock`. */
  readonly formatX?: (value: number) => string;
  readonly formatY?: (value: number) => string;
  /** Fills under the first series. Used for single-series charts where the magnitude, not the
   * comparison, is the subject. */
  readonly area?: boolean;
  readonly height?: number;
  /** Forces the y domain to include zero -- correct for counts, wrong for a narrow band of
   * altitudes where it would flatten the entire signal. */
  readonly zeroBased?: boolean;
}

const PLOT = { width: 680, height: 240, left: 54, right: 14, top: 14, bottom: 30 } as const;

export function LineChart({
  series,
  yLabel,
  xLabel,
  formatX = formatCompact,
  formatY = formatCompact,
  area = false,
  height,
  zeroBased = false,
}: LineChartProps) {
  const gradientId = useId();

  const geometry = useMemo(() => {
    const xs = series.flatMap((entry) => entry.points.map((point) => point.x));
    const ys = series.flatMap((entry) =>
      entry.points.map((point) => point.y).filter((value): value is number => value !== null),
    );
    const xDomain = domainOf(xs, 0);
    const yDomain = zeroBased
      ? { min: 0, max: domainOf([...ys, 0]).max }
      : domainOf(ys);

    const innerWidth = PLOT.width - PLOT.left - PLOT.right;
    const innerHeight = PLOT.height - PLOT.top - PLOT.bottom;

    const sx = (value: number) =>
      PLOT.left +
      (xDomain.max === xDomain.min
        ? innerWidth / 2
        : ((value - xDomain.min) / (xDomain.max - xDomain.min)) * innerWidth);
    const sy = (value: number) =>
      PLOT.top +
      innerHeight -
      (yDomain.max === yDomain.min
        ? innerHeight / 2
        : ((value - yDomain.min) / (yDomain.max - yDomain.min)) * innerHeight);

    const paths = series.map((entry) => {
      // Each run of consecutive non-null samples is its own subpath, so a gap is a visible
      // break rather than a straight line across missing data.
      let path = '';
      let open = false;
      for (const point of entry.points) {
        if (point.y === null) {
          open = false;
          continue;
        }
        path += `${open ? 'L' : 'M'}${sx(point.x).toFixed(2)} ${sy(point.y).toFixed(2)} `;
        open = true;
      }

      const defined = entry.points.filter(
        (point): point is { x: number; y: number } => point.y !== null,
      );
      const areaPath =
        defined.length > 1
          ? `M${sx(defined[0]!.x).toFixed(2)} ${(PLOT.top + innerHeight).toFixed(2)} ` +
            defined.map((point) => `L${sx(point.x).toFixed(2)} ${sy(point.y).toFixed(2)}`).join(' ') +
            ` L${sx(defined[defined.length - 1]!.x).toFixed(2)} ${(PLOT.top + innerHeight).toFixed(2)} Z`
          : '';

      const last = defined[defined.length - 1];
      return { entry, path: path.trim(), areaPath, last: last ?? null };
    });

    return {
      paths,
      xTicks: niceTicks(xDomain, 5).map((value) => ({ value, x: sx(value) })),
      yTicks: niceTicks(yDomain, 4).map((value) => ({ value, y: sy(value) })),
      innerHeight,
      innerWidth,
      sx,
      sy,
    };
  }, [series, zeroBased]);

  const summary = series
    .map((entry) => {
      const defined = entry.points.filter((point) => point.y !== null);
      const last = defined[defined.length - 1]?.y;
      return `${entry.label}: ${last === null || last === undefined ? 'no data' : formatY(last)}`;
    })
    .join('; ');

  return (
    <figure className="dash-chart" style={height ? { height: `${height}px` } : undefined}>
      <svg
        viewBox={`0 0 ${PLOT.width} ${PLOT.height}`}
        preserveAspectRatio="none"
        role="img"
        aria-label={`${yLabel ?? 'Value'} over ${xLabel ?? 'time'}. ${summary}`}
      >
        {area ? (
          <defs>
            <linearGradient id={gradientId} x1="0" y1="0" x2="0" y2="1">
              <stop
                offset="0%"
                stopColor={`var(${series[0]?.token ?? '--orca-primary'})`}
                stopOpacity="0.34"
              />
              <stop
                offset="100%"
                stopColor={`var(${series[0]?.token ?? '--orca-primary'})`}
                stopOpacity="0"
              />
            </linearGradient>
          </defs>
        ) : null}

        {dedupeByLabel(geometry.yTicks, formatY).map((tick) => (
          <g key={`y${tick.value}`}>
            <line
              className="dash-grid"
              x1={PLOT.left}
              x2={PLOT.width - PLOT.right}
              y1={tick.y}
              y2={tick.y}
            />
            <text className="dash-axis-text" x={PLOT.left - 8} y={tick.y + 3.5} textAnchor="end">
              {formatY(tick.value)}
            </text>
          </g>
        ))}

        {dedupeByLabel(geometry.xTicks, formatX).map((tick) => (
          <text
            key={`x${tick.value}`}
            className="dash-axis-text"
            x={tick.x}
            y={PLOT.height - 10}
            textAnchor="middle"
          >
            {formatX(tick.value)}
          </text>
        ))}

        {area && geometry.paths[0]?.areaPath ? (
          <path d={geometry.paths[0].areaPath} fill={`url(#${gradientId})`} stroke="none" />
        ) : null}

        {geometry.paths.map(({ entry, path, last }) => (
          <g key={entry.id}>
            <path
              className="dash-line"
              d={path}
              stroke={`var(${entry.token})`}
              fill="none"
              vectorEffect="non-scaling-stroke"
            />
            {/* A dot on the newest sample: on a live chart it marks where "now" is, which a
                bare polyline does not. */}
            {last ? (
              <circle
                className="dash-line__head"
                r="3"
                fill={`var(${entry.token})`}
                cx={geometry.sx(last.x)}
                cy={geometry.sy(last.y)}
              />
            ) : null}
          </g>
        ))}

        <line
          className="dash-axis"
          x1={PLOT.left}
          x2={PLOT.width - PLOT.right}
          y1={PLOT.top + geometry.innerHeight}
          y2={PLOT.top + geometry.innerHeight}
        />
      </svg>

      {series.length > 1 ? (
        <figcaption className="dash-legend">
          {series.map((entry) => (
            <span key={entry.id} className="dash-legend__item">
              <span className="dash-legend__swatch" style={{ background: `var(${entry.token})` }} />
              {entry.label}
            </span>
          ))}
        </figcaption>
      ) : null}
    </figure>
  );
}

/* ====================================================================== Bars =============== */

export interface BarDatum {
  readonly label: string;
  readonly value: number;
  readonly token?: string;
  /** Secondary value drawn as a lighter backing bar, for "of N" comparisons. */
  readonly reference?: number;
  readonly note?: string;
}

export interface BarChartProps {
  readonly data: readonly BarDatum[];
  readonly format?: (value: number) => string;
  readonly unit?: string;
  /** Horizontal bars with the label on the left. Better than rotated axis labels whenever the
   * category names are words rather than numbers. */
  readonly horizontal?: boolean;
}

export function BarChart({ data, format = formatCompact, unit, horizontal = true }: BarChartProps) {
  const max = Math.max(...data.map((entry) => Math.max(entry.value, entry.reference ?? 0)), 1);

  if (horizontal) {
    return (
      <div className="dash-bars" role="table" aria-label="Bar chart">
        {data.map((entry) => (
          <div className="dash-bars__row" role="row" key={entry.label}>
            <span className="dash-bars__label" role="cell">
              {entry.label}
            </span>
            <span className="dash-bars__track" role="cell">
              {entry.reference !== undefined ? (
                <span
                  className="dash-bars__reference"
                  style={{ width: `${(entry.reference / max) * 100}%` }}
                />
              ) : null}
              <span
                className="dash-bars__fill"
                style={{
                  width: `${(entry.value / max) * 100}%`,
                  background: `var(${entry.token ?? '--orca-primary'})`,
                }}
              />
            </span>
            <span className="dash-bars__value mono" role="cell">
              {format(entry.value)}
              {unit ? <span className="dash-bars__unit">{unit}</span> : null}
            </span>
          </div>
        ))}
      </div>
    );
  }

  const width = 680;
  const height = 220;
  const gap = 14;
  const barWidth = (width - gap * (data.length + 1)) / Math.max(1, data.length);

  return (
    <figure className="dash-chart">
      <svg viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none" role="img" aria-label="Bar chart">
        {data.map((entry, index) => {
          const barHeight = (entry.value / max) * (height - 46);
          const x = gap + index * (barWidth + gap);
          return (
            <g key={entry.label}>
              <rect
                x={x}
                y={height - 28 - barHeight}
                width={barWidth}
                height={Math.max(1, barHeight)}
                rx="2"
                fill={`var(${entry.token ?? '--orca-primary'})`}
              />
              <text
                className="dash-axis-text"
                x={x + barWidth / 2}
                y={height - 12}
                textAnchor="middle"
              >
                {entry.label}
              </text>
              <text
                className="dash-bar-value"
                x={x + barWidth / 2}
                y={height - 34 - barHeight}
                textAnchor="middle"
              >
                {format(entry.value)}
              </text>
            </g>
          );
        })}
      </svg>
    </figure>
  );
}

/* ====================================================================== Donut ============== */

export interface DonutSlice {
  readonly label: string;
  readonly value: number;
  readonly token: string;
}

export function DonutChart({
  slices,
  centreValue,
  centreLabel,
}: {
  readonly slices: readonly DonutSlice[];
  readonly centreValue: string;
  readonly centreLabel: string;
}) {
  const total = slices.reduce((sum, slice) => sum + slice.value, 0);
  const radius = 54;
  const circumference = 2 * Math.PI * radius;
  let offset = 0;

  return (
    <figure className="dash-donut">
      <svg
        viewBox="0 0 140 140"
        role="img"
        aria-label={
          total === 0
            ? 'No data'
            : slices
                .map((slice) => `${slice.label}: ${slice.value} (${Math.round((slice.value / total) * 100)}%)`)
                .join('; ')
        }
      >
        <circle cx="70" cy="70" r={radius} className="dash-donut__track" />
        {total > 0
          ? slices.map((slice) => {
              const length = (slice.value / total) * circumference;
              const dash = `${length} ${circumference - length}`;
              const element = (
                <circle
                  key={slice.label}
                  cx="70"
                  cy="70"
                  r={radius}
                  fill="none"
                  stroke={`var(${slice.token})`}
                  strokeWidth="16"
                  strokeDasharray={dash}
                  strokeDashoffset={-offset}
                  // -90deg so the first slice starts at twelve o'clock, which is where a reader
                  // expects a donut to begin.
                  transform="rotate(-90 70 70)"
                  strokeLinecap="butt"
                />
              );
              offset += length;
              return element;
            })
          : null}
        <text className="dash-donut__value" x="70" y="68" textAnchor="middle">
          {centreValue}
        </text>
        <text className="dash-donut__label" x="70" y="86" textAnchor="middle">
          {centreLabel}
        </text>
      </svg>
      <figcaption className="dash-legend dash-legend--stack">
        {slices.map((slice) => (
          <span key={slice.label} className="dash-legend__item">
            <span className="dash-legend__swatch" style={{ background: `var(${slice.token})` }} />
            {slice.label}
            <span className="dash-legend__count mono">{slice.value}</span>
          </span>
        ))}
      </figcaption>
    </figure>
  );
}

/* ==================================================================== Scatter ============== */

export interface ScatterDatum {
  readonly x: number;
  readonly y: number;
  readonly label: string;
  readonly token: string;
  readonly emphasis?: boolean;
  readonly radius?: number;
}

export function ScatterChart({
  data,
  xLabel,
  yLabel,
  formatX = formatCompact,
  formatY = formatCompact,
}: {
  readonly data: readonly ScatterDatum[];
  readonly xLabel: string;
  readonly yLabel: string;
  readonly formatX?: (value: number) => string;
  readonly formatY?: (value: number) => string;
}) {
  const geometry = useMemo(() => {
    const xDomain = domainOf(data.map((d) => d.x));
    const yDomain = domainOf(data.map((d) => d.y));
    const innerWidth = PLOT.width - PLOT.left - PLOT.right;
    const innerHeight = PLOT.height - PLOT.top - PLOT.bottom;

    return {
      points: data.map((datum) => ({
        datum,
        cx:
          PLOT.left +
          ((datum.x - xDomain.min) / (xDomain.max - xDomain.min || 1)) * innerWidth,
        cy:
          PLOT.top +
          innerHeight -
          ((datum.y - yDomain.min) / (yDomain.max - yDomain.min || 1)) * innerHeight,
      })),
      xTicks: niceTicks(xDomain, 5).map((value) => ({
        value,
        x: PLOT.left + ((value - xDomain.min) / (xDomain.max - xDomain.min || 1)) * innerWidth,
      })),
      yTicks: niceTicks(yDomain, 4).map((value) => ({
        value,
        y:
          PLOT.top +
          innerHeight -
          ((value - yDomain.min) / (yDomain.max - yDomain.min || 1)) * innerHeight,
      })),
      innerHeight,
    };
  }, [data]);

  return (
    <figure className="dash-chart">
      <svg
        viewBox={`0 0 ${PLOT.width} ${PLOT.height}`}
        preserveAspectRatio="none"
        role="img"
        aria-label={`${yLabel} against ${xLabel}, ${data.length} plans plotted.`}
      >
        {dedupeByLabel(geometry.yTicks, formatY).map((tick) => (
          <g key={`y${tick.value}`}>
            <line className="dash-grid" x1={PLOT.left} x2={PLOT.width - PLOT.right} y1={tick.y} y2={tick.y} />
            <text className="dash-axis-text" x={PLOT.left - 8} y={tick.y + 3.5} textAnchor="end">
              {formatY(tick.value)}
            </text>
          </g>
        ))}
        {dedupeByLabel(geometry.xTicks, formatX).map((tick) => (
          <text key={`x${tick.value}`} className="dash-axis-text" x={tick.x} y={PLOT.height - 10} textAnchor="middle">
            {formatX(tick.value)}
          </text>
        ))}
        {geometry.points.map(({ datum, cx, cy }) => (
          <g key={datum.label}>
            {datum.emphasis ? (
              <circle cx={cx} cy={cy} r={(datum.radius ?? 5) + 5} fill="none" stroke={`var(${datum.token})`} strokeWidth="1.2" opacity="0.6" />
            ) : null}
            <circle cx={cx} cy={cy} r={datum.radius ?? 5} fill={`var(${datum.token})`} opacity={datum.emphasis ? 1 : 0.82}>
              <title>{datum.label}</title>
            </circle>
          </g>
        ))}
        <line
          className="dash-axis"
          x1={PLOT.left}
          x2={PLOT.width - PLOT.right}
          y1={PLOT.top + geometry.innerHeight}
          y2={PLOT.top + geometry.innerHeight}
        />
      </svg>
      <figcaption className="dash-axis-caption">
        <span>{xLabel}</span>
        <span>{yLabel}</span>
      </figcaption>
    </figure>
  );
}

/* ====================================================================== Gauge ============== */

export function GaugeRing({
  value,
  label,
  token = '--orca-primary',
  caption,
}: {
  /** 0..1. Values outside are clamped rather than drawn past the ring, which would wrap. */
  readonly value: number;
  readonly label: string;
  readonly token?: string;
  readonly caption?: string;
}) {
  const clamped = Math.min(1, Math.max(0, value));
  const radius = 34;
  // A 270-degree arc, not a full circle: the gap is what distinguishes "28% full" from "28%
  // through a cycle" at a glance.
  const sweep = 0.75;
  const circumference = 2 * Math.PI * radius;

  return (
    <div className="dash-gauge">
      <svg viewBox="0 0 90 90" role="img" aria-label={`${label}: ${Math.round(clamped * 100)}%`}>
        <circle
          cx="45"
          cy="45"
          r={radius}
          className="dash-gauge__track"
          strokeDasharray={`${circumference * sweep} ${circumference}`}
          transform="rotate(135 45 45)"
        />
        <circle
          cx="45"
          cy="45"
          r={radius}
          fill="none"
          stroke={`var(${token})`}
          strokeWidth="7"
          strokeLinecap="round"
          strokeDasharray={`${circumference * sweep * clamped} ${circumference}`}
          transform="rotate(135 45 45)"
        />
        <text className="dash-gauge__value" x="45" y="48" textAnchor="middle">
          {Math.round(clamped * 100)}%
        </text>
      </svg>
      <p className="dash-gauge__label">{label}</p>
      {caption ? <p className="dash-gauge__caption mono">{caption}</p> : null}
    </div>
  );
}

/* ==================================================================== Heat grid ============ */

export interface HeatCell {
  readonly row: string;
  readonly column: string;
  /** 0..1, where 1 is healthiest. `null` renders as an explicit "no reading" cell. */
  readonly value: number | null;
}

export function HeatGrid({
  cells,
  rows,
  columns,
  lowLabel,
  highLabel,
}: {
  readonly cells: readonly HeatCell[];
  readonly rows: readonly string[];
  readonly columns: readonly string[];
  readonly lowLabel: string;
  readonly highLabel: string;
}) {
  const lookup = useMemo(() => {
    const map = new Map<string, number | null>();
    for (const cell of cells) map.set(`${cell.row}\u0000${cell.column}`, cell.value);
    return map;
  }, [cells]);

  return (
    <div className="dash-heat">
      <table className="dash-heat__table">
        <caption className="dash-vh">
          Subsystem health by entity. Darker cells are lower readings.
        </caption>
        <thead>
          <tr>
            <th scope="col" className="dash-heat__corner">
              Entity
            </th>
            {columns.map((column) => (
              <th scope="col" key={column}>
                {column}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {rows.map((row) => (
            <tr key={row}>
              <th scope="row" className="dash-heat__row-label">
                {row}
              </th>
              {columns.map((column) => {
                const value = lookup.get(`${row}\u0000${column}`) ?? null;
                return (
                  <td
                    key={column}
                    className="dash-heat__cell"
                    // The ramp is one hue at varying alpha plus a critical hue below 0.6, so the
                    // reading is legible to a reader who cannot separate red from green.
                    style={
                      value === null
                        ? undefined
                        : {
                            background:
                              value < 0.6
                                ? `color-mix(in srgb, var(--orca-critical) ${Math.round((1 - value) * 90)}%, transparent)`
                                : `color-mix(in srgb, var(--orca-success) ${Math.round(value * 70)}%, transparent)`,
                          }
                    }
                    title={`${row} · ${column}: ${value === null ? 'no reading' : `${Math.round(value * 100)}%`}`}
                  >
                    <span className="mono">{value === null ? '—' : Math.round(value * 100)}</span>
                  </td>
                );
              })}
            </tr>
          ))}
        </tbody>
      </table>
      <p className="dash-heat__scale">
        <span>{lowLabel}</span>
        <span className="dash-heat__ramp" aria-hidden="true" />
        <span>{highLabel}</span>
      </p>
    </div>
  );
}

/* ==================================================================== Sparkline ============ */

export function Sparkline({
  values,
  token = '--orca-primary',
  ariaLabel,
}: {
  readonly values: readonly number[];
  readonly token?: string;
  readonly ariaLabel: string;
}) {
  if (values.length < 2) {
    return <span className="dash-spark dash-spark--empty" aria-label={`${ariaLabel}: collecting`} />;
  }

  const min = Math.min(...values);
  const max = Math.max(...values);
  const span = max - min || 1;
  const path = values
    .map((value, index) => {
      const x = (index / (values.length - 1)) * 100;
      const y = 26 - ((value - min) / span) * 22;
      return `${index === 0 ? 'M' : 'L'}${x.toFixed(1)} ${y.toFixed(1)}`;
    })
    .join(' ');

  return (
    <svg className="dash-spark" viewBox="0 0 100 30" preserveAspectRatio="none" role="img" aria-label={ariaLabel}>
      <path d={path} fill="none" stroke={`var(${token})`} strokeWidth="1.6" vectorEffect="non-scaling-stroke" />
    </svg>
  );
}

/* ================================================================== Stat tile ============== */

export interface StatTileProps {
  readonly label: string;
  readonly value: string;
  readonly unit?: string;
  readonly delta?: { readonly direction: 'up' | 'down' | 'flat'; readonly text: string };
  readonly token?: string;
  readonly trend?: readonly number[];
  readonly caption?: string;
}

export function StatTile({ label, value, unit, delta, token, trend, caption }: StatTileProps) {
  return (
    <article className="dash-stat">
      <p className="dash-stat__label">{label}</p>
      <p className="dash-stat__value" style={token ? { color: `var(${token})` } : undefined}>
        {value}
        {unit ? <span className="dash-stat__unit">{unit}</span> : null}
      </p>
      {trend && trend.length > 1 ? (
        <Sparkline values={trend} token={token ?? '--orca-primary'} ariaLabel={`${label} trend`} />
      ) : null}
      {delta ? (
        <p className={`dash-stat__delta dash-stat__delta--${delta.direction}`}>
          <span aria-hidden="true">
            {delta.direction === 'up' ? '▲' : delta.direction === 'down' ? '▼' : '▬'}
          </span>{' '}
          {delta.text}
        </p>
      ) : null}
      {caption ? <p className="dash-stat__caption">{caption}</p> : null}
    </article>
  );
}
