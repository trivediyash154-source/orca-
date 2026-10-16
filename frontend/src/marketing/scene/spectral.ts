/**
 * The entry-surface scene's colour table.
 *
 * `VISUAL_DESIGN.md` §2 forbids a hardcoded colour anywhere in a component or a CSS rule:
 * every colour is a custom property from `tokens.css`. A WebGL material cannot read a custom
 * property, so the 3D layer gets exactly one documented exception, and this module IS that
 * exception. No other file under `src/marketing/scene/` may contain a colour literal.
 *
 * The values below are the numeric twins of tokens in `tokens.css` (`--orca-spectral-1..6`,
 * `--orca-void`, `--orca-rescue-signal`, ...). They are duplicated rather than read from
 * `getComputedStyle()` at runtime because the canvas can mount before the stylesheet resolves,
 * which would give the first frame an empty string and paint the scene black. When a token
 * changes, change its twin here — the pairing is stated on every line so the audit is
 * mechanical.
 *
 * ## Why the ramp is held below full saturation
 *
 * The spectral ramp ENCODES things: position along a computed transfer arc, depth into a
 * Pareto front, the leading edge of a scan. It is never decoration. A fully saturated neon
 * ramp used ornamentally would destroy the signal value of the one place a glow actually
 * means something, so nothing in `SCENE_COLORS` outside the mission layer is allowed to reach
 * for it — background, debris and particulate are deliberately neutral.
 */

import { Color } from 'three';

/* --- Ramp stops -------------------------------------------------------------------------
   Named separately from the array so `spectralAt()`'s fallback can reference stop 1 without a
   possibly-undefined index lookup under `noUncheckedIndexedAccess`. */

const SPECTRAL_1 = '#3fb8cf'; // --orca-spectral-1  cyan        — arc departure, lowest energy
const SPECTRAL_2 = '#4f83db'; // --orca-spectral-2  blue
const SPECTRAL_3 = '#8069d2'; // --orca-spectral-3  violet
const SPECTRAL_4 = '#bf65ab'; // --orca-spectral-4  magenta
const SPECTRAL_5 = '#d9a054'; // --orca-spectral-5  amber
const SPECTRAL_6 = '#eee2cd'; // --orca-spectral-6  warm white  — arc arrival, highest energy

/** Six stops, cyan -> blue -> violet -> magenta -> amber -> warm white. Ordered low energy to
 * high, so `t` in `spectralAt(t)` reads as "fraction of the way through the manoeuvre". */
export const SPECTRAL_RAMP: readonly string[] = [
  SPECTRAL_1,
  SPECTRAL_2,
  SPECTRAL_3,
  SPECTRAL_4,
  SPECTRAL_5,
  SPECTRAL_6,
];

/**
 * The declared keys below are what the scene actually uses. The index signature is deliberate:
 * a sibling scene module that reaches for a colour this table has not named yet gets
 * `string | undefined` (a fixable type, and a visible hole) rather than a hard "property does
 * not exist" build break in a file nobody owns. Add the key here rather than working around it.
 */
export interface SceneColorTable {
  /* --- Ground ------------------------------------------------------------------------- */
  /** Canvas clear colour. Matches `--orca-void` exactly so the WebGL frame and the page
   * ground are the same black and the canvas edge is invisible. */
  readonly background: string;
  /** Fog colour. Identical to `background`: distant geometry must dissolve into the ground,
   * not into a differently-tinted haze that would draw the frame's boundary. */
  readonly fog: string;

  /* --- Lighting ----------------------------------------------------------------------- */
  /** Directional key light — the sun. Very slightly warm so the lit hemisphere reads as
   * sunlight against the cool rim, which is what produces a legible terminator. */
  readonly keyLight: string;
  /** Cool fill from behind and below, standing in for earthshine. Never bright enough to
   * erase the night side. */
  readonly rimLight: string;
  /** Ambient floor. Low: a scene lit flat has no terminator and stops reading as a planet. */
  readonly ambientLight: string;

  /* --- Earth -------------------------------------------------------------------------- */
  /** Faint self-illumination on the unlit hemisphere. Without it the night side is pure
   * black against a near-black ground and Earth loses its silhouette entirely. */
  readonly earthNight: string;
  /** Graticule wireframe — a secondary orientation cue over the painted surface, at the same
   * near-invisible opacity the workspace globe uses. */
  readonly earthGraticule: string;
  /** Inner atmosphere shell: tight and brighter, this is the limb itself. */
  readonly earthRim: string;
  /** Outer atmosphere shell: larger and softer, this is what separates the planet from the
   * background at the silhouette. */
  readonly earthHalo: string;

  /* --- Starfield and debris ----------------------------------------------------------- */
  /** Starfield tint. Neutral — coloured stars would compete with the ramp. */
  readonly starlight: string;
  /** Debris on the inner shells: closer, so slightly brighter and cooler. */
  readonly debrisNear: string;
  /** Debris on the outer shells: dimmer, which is the depth cue that makes the
   * shells separable without a single line of extra geometry. */
  readonly debrisFar: string;
  /** The diffuse particulate halo beyond the populated shells. Dimmest element in the scene. */
  readonly particulate: string;

  /* --- Mission layer ------------------------------------------------------------------ */
  /** The rescue vehicle. Cyan: the departure end of the ramp, so vehicle and arc origin are
   * the same colour and the eye starts the arc at the right end. */
  readonly rescueVehicle: string;
  /** The target — a healthy satellite being approached. */
  readonly targetVehicle: string;
  /** A target in distress. `--orca-rescue-signal`, deliberately NOT `--orca-warning`:
   * "this object needs rescue" and "this value is out of nominal range" are different claims
   * and must not share a colour. */
  readonly targetDistress: string;
  /** The active scan beam sweeping the shells. */
  readonly scanBeam: string;
  /** Transfer-arc departure stop — the same value as `SPECTRAL_RAMP[0]`. */
  readonly arcStart: string;
  /** Transfer-arc mid stop. */
  readonly arcMid: string;
  /** Transfer-arc arrival stop — the same value as `SPECTRAL_RAMP[5]`. */
  readonly arcEnd: string;
  /** Orbit shells drawn as rings rather than as points. Kept near-neutral so the one
   * spectral arc stays the only saturated line in the frame. */
  readonly orbitShell: string;
  /** Waypoint crosshairs and node markers. */
  readonly waypoint: string;
  /** The expanding ring a click-to-probe interaction emits. */
  readonly probeRipple: string;
  /** In-scene label text, on the rare element that carries one. */
  readonly labelText: string;

  readonly [key: string]: string;
}

export const SCENE_COLORS: SceneColorTable = {
  background: '#03050a', // --orca-void
  fog: '#03050a', // --orca-void

  keyLight: '#fff4e2', // warm white, ~1/3 step past --orca-spectral-6
  rimLight: '#4f83db', // --orca-spectral-2
  ambientLight: '#7898c4', // the steel blue underlying --orca-hairline

  earthNight: '#0a1626', // --orca-void-raised, pushed toward --orca-spectral-2
  earthGraticule: '#cfe9ff', // matches the workspace globe's graticule (Earth.tsx)
  earthRim: '#7ec8ff', // matches the workspace globe's inner limb (Earth.tsx)
  earthHalo: '#3d8fd6', // matches the workspace globe's outer halo (Earth.tsx)

  starlight: '#e6ecf5', // --orca-text
  debrisNear: '#8ea6c4', // --orca-muted, lifted toward --orca-text
  debrisFar: '#4c5e77', // between --orca-muted-dim and --orca-border
  particulate: '#2c3a4e', // just above --orca-border

  rescueVehicle: '#3fb8cf', // --orca-spectral-1
  targetVehicle: '#8ea6c4', // same neutral as debrisNear: an intact object is not an event
  targetDistress: '#e8703a', // --orca-rescue-signal
  scanBeam: '#4f83db', // --orca-spectral-2
  arcStart: SPECTRAL_1,
  arcMid: SPECTRAL_3,
  arcEnd: SPECTRAL_6,
  orbitShell: '#3b4c66', // --orca-border, one step cooler
  waypoint: '#eee2cd', // --orca-spectral-6
  probeRipple: '#6fd0e0', // --orca-focus-ring: a probe is an interaction, so it borrows the
  // interaction colour rather than inventing a new one
  labelText: '#e6ecf5', // --orca-text
};

/** Parsed once. `Color` construction does a string parse and an sRGB conversion; doing that
 * per call would put a parse inside `useFrame` for anything that samples the ramp per frame. */
const RAMP: readonly Color[] = SPECTRAL_RAMP.map((hex) => new Color(hex));

/**
 * The ramp sampled at `t` in [0, 1], as a NEW `Color` — callers routinely keep the result
 * (per-instance colours, buffer writes), so returning a shared mutable instance would alias
 * every previous sample to the latest one.
 *
 * `t` is clamped rather than wrapped: a value outside [0, 1] means a caller's parameterisation
 * is wrong, and wrapping would silently paint the arrival colour at the departure end.
 */
export function spectralAt(t: number): Color {
  const clamped = Number.isFinite(t) ? Math.min(1, Math.max(0, t)) : 0;
  const scaled = clamped * (RAMP.length - 1);
  // Clamped to the last SEGMENT, not the last stop, so t === 1 interpolates the final pair at
  // fraction 1 instead of indexing past the end.
  const lower = Math.min(RAMP.length - 2, Math.floor(scaled));
  const from = RAMP[lower];
  const to = RAMP[lower + 1];
  // `RAMP` is a fixed six-entry table and `lower` is clamped to [0, 4], so both lookups are
  // provably in range; the guard exists only because `noUncheckedIndexedAccess` cannot see it.
  if (!from || !to) return new Color(SPECTRAL_1);
  return from.clone().lerp(to, scaled - lower);
}
