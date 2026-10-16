/**
 * A label-over-value pair in the instrumentation register.
 *
 * `mono` defaults to true because the overwhelming majority of readouts carry a number, and
 * tabular monospaced figures are what stop a value from shifting horizontally when it changes.
 * It is switchable for the few readouts whose value is a word rather than a measurement.
 *
 * Tone is a semantic state (active / warm / critical), never a decorative choice — see the
 * spectral discipline in `tokens.css`.
 */
export interface TelemetryReadoutProps {
  readonly label: string;
  readonly value: string;
  readonly unit?: string;
  readonly tone?: 'neutral' | 'active' | 'warm' | 'critical';
  readonly mono?: boolean;
}

export function TelemetryReadout({
  label,
  value,
  unit,
  tone = 'neutral',
  mono = true,
}: TelemetryReadoutProps) {
  const classes = ['mk-readout', `mk-readout--${tone}`, mono ? 'mk-readout--mono' : null]
    .filter((entry): entry is string => Boolean(entry))
    .join(' ');

  return (
    <div className={classes}>
      <span className="mk-readout__label">{label}</span>
      <span className="mk-readout__value">
        {value}
        {unit ? <span className="mk-readout__unit">{unit}</span> : null}
      </span>
    </div>
  );
}
