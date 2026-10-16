/** Presentation-only formatting helpers — pure functions of already-computed engine
 * values, never a derivation of new meaning from them (brief §57: "React displays received
 * results"). Formatting seconds as `HH:MM:SS` is not a simulation calculation. */

export function formatSimTime(totalSeconds: number): string {
  const total = Math.max(0, Math.floor(totalSeconds));
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  const seconds = total % 60;
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${pad(hours)}:${pad(minutes)}:${pad(seconds)}`;
}

export function formatPercent(fraction: number, digits = 1): string {
  return `${(fraction * 100).toFixed(digits)}%`;
}

export function formatMinutes(seconds: number): string {
  return `${(seconds / 60).toFixed(1)} min`;
}

export function formatKm(km: number): string {
  return `${km.toLocaleString(undefined, { maximumFractionDigits: 0 })} km`;
}
