import { useId } from 'react';

/**
 * The no-WebGL path, and the first frame while the capability probe is still running.
 *
 * It is a drawing, not a placeholder. A reader whose machine blocks WebGL — a locked-down
 * corporate build, a GPU on a driver blocklist, a browser with hardware acceleration off — is
 * exactly the reader most likely to conclude the page is broken, so the fallback carries the
 * same visual language as the scene it replaces: Earth's limb low-left, exaggerated orbital
 * shells, one spectral transfer arc, waypoint crosshairs, monospaced channel labels.
 *
 * ## Geometry
 *
 * The shells are drawn with the SAME altitude exaggeration `coordinates.ts` documents for the
 * workspace, and the diagram says so in a visible label. At true scale a 500 km and an 800 km
 * orbit sit at 1.078 and 1.125 Earth radii — a 4% difference that collapses into a single line
 * hugging the surface. Exaggerating is what makes three shells legible as three shells; hiding
 * that it happened would be the dishonest version.
 *
 * Every number in the markup is a viewBox coordinate. Nothing here is a measurement, and the
 * diagram states that too.
 *
 * ## Colour
 *
 * Not one colour literal appears below. Fills, strokes and gradient stops are all set from
 * `scene.css`, which reads the same `--orca-*` tokens as the rest of the page — so the SVG
 * fallback and the WebGL scene cannot drift apart in palette.
 */

export type SceneFallbackVariant = 'hero' | 'auth';

export interface SceneFallbackProps {
  readonly variant?: SceneFallbackVariant;
  readonly className?: string;
}

/** Earth's centre and radius in viewBox units — every other coordinate below was derived from
 * these two, so changing them means re-deriving the shells and nodes. */
const EARTH_CX = 300;
const EARTH_CY = 880;
const EARTH_R = 470;

/** Shell semi-axes. The ratio ry/rx encodes the viewing elevation; all three share it, because
 * three coplanar-ish shells seen from one camera must share a vanishing geometry or the
 * diagram stops being readable as a single space. */
const SHELLS: readonly { readonly rx: number; readonly ry: number; readonly label: string }[] = [
  { rx: 660, ry: 520, label: 'SHELL 01' },
  { rx: 760, ry: 560, label: 'SHELL 02' },
  { rx: 900, ry: 665, label: 'SHELL 03' },
];

/** Common tilt of the shell plane, degrees. */
const SHELL_TILT_DEG = -16;

/** Nodes, in viewBox coordinates, each solved from its shell's parametric form. */
const DEPARTURE_X = 382;
const DEPARTURE_Y = 348;
const APEX_X = 683;
const APEX_Y = 231;
const ARRIVAL_X = 1007;
const ARRIVAL_Y = 385;

/** Label anchors on the left flank of each shell arc, again solved from the parametric form. */
const SHELL_LABEL_POINTS: readonly { readonly x: number; readonly y: number }[] = [
  { x: 179, y: 374 },
  { x: 171, y: 335 },
  { x: 147, y: 232 },
];

function Crosshair({ x, y, size }: { readonly x: number; readonly y: number; readonly size: number }) {
  return (
    <g className="mk-scene-fallback__node" transform={`translate(${x} ${y})`}>
      <line x1={-size} y1={0} x2={-size * 0.34} y2={0} />
      <line x1={size * 0.34} y1={0} x2={size} y2={0} />
      <line x1={0} y1={-size} x2={0} y2={-size * 0.34} />
      <line x1={0} y1={size * 0.34} x2={0} y2={size} />
      <circle className="mk-scene-fallback__node-ring" cx={0} cy={0} r={size * 0.56} />
    </g>
  );
}

export function SceneFallback({ variant = 'hero', className }: SceneFallbackProps) {
  // Two of these can be on the page at once (a hero and an auth panel in the same tree during
  // a route transition), and duplicate gradient ids would make one of them reference the
  // other's stops. The colons React puts in `useId()` are legal in an id but awkward inside a
  // `url(#...)` reference, so they are stripped.
  const uid = useId().replace(/:/g, '');
  const titleId = `${uid}-title`;
  const descId = `${uid}-desc`;
  const arcGradientId = `${uid}-arc`;
  const bodyGradientId = `${uid}-body`;
  const limbGradientId = `${uid}-limb`;
  const gridPatternId = `${uid}-grid`;
  const discClipId = `${uid}-disc`;

  const rootClass = ['mk-scene-fallback', `mk-scene-fallback--${variant}`, className]
    .filter(Boolean)
    .join(' ');

  return (
    <svg
      className={rootClass}
      viewBox="0 0 1200 760"
      preserveAspectRatio={variant === 'auth' ? 'xMidYMax meet' : 'xMidYMid slice'}
      role="img"
      aria-hidden={false}
      aria-labelledby={`${titleId} ${descId}`}
      focusable="false"
    >
      <title id={titleId}>Orbital rescue schematic</title>
      <desc id={descId}>
        A schematic of an orbital rescue. Earth&apos;s limb fills the lower left. Three orbital
        shells, drawn with altitude deliberately exaggerated so they can be told apart, arc
        across the frame. A rescue vehicle departs a waypoint on the innermost shell and follows
        a single transfer arc, coloured cyan through violet to warm white along its length, to a
        disabled satellite on the outermost shell. The geometry is illustrative and is not
        engine output.
      </desc>

      <defs>
        {/* The spectral ramp, laid along the arc's own direction of travel: departure at the
            cyan end, arrival at warm white. Stop colours come from `scene.css`. */}
        <linearGradient id={arcGradientId} x1="0" y1="1" x2="1" y2="0">
          <stop className="mk-scene-fallback__stop-1" offset="0" />
          <stop className="mk-scene-fallback__stop-2" offset="0.22" />
          <stop className="mk-scene-fallback__stop-3" offset="0.44" />
          <stop className="mk-scene-fallback__stop-4" offset="0.64" />
          <stop className="mk-scene-fallback__stop-5" offset="0.83" />
          <stop className="mk-scene-fallback__stop-6" offset="1" />
        </linearGradient>

        {/* Off-centre toward the upper right, which is where the key light sits in the WebGL
            scene. That offset IS the terminator: the disc darkens away from the light rather
            than being split by a painted line. */}
        <radialGradient id={bodyGradientId} cx="0.79" cy="0.11" r="0.56">
          <stop className="mk-scene-fallback__body-core" offset="0" />
          <stop className="mk-scene-fallback__body-lit" offset="0.3" />
          <stop className="mk-scene-fallback__body-mid" offset="0.62" />
          <stop className="mk-scene-fallback__body-dark" offset="1" />
        </radialGradient>

        {/* A ring of atmosphere: transparent inside the disc, brightest just outside it, gone
            shortly after. Same job as the additive back-side shells in `HeroEarth`. */}
        <radialGradient id={limbGradientId} cx="0.5" cy="0.5" r="0.5">
          <stop className="mk-scene-fallback__limb-clear" offset="0.88" />
          <stop className="mk-scene-fallback__limb-glow" offset="0.945" />
          <stop className="mk-scene-fallback__limb-clear" offset="1" />
        </radialGradient>

        <pattern id={gridPatternId} width="60" height="60" patternUnits="userSpaceOnUse">
          <path className="mk-scene-fallback__grid" d="M 60 0 L 0 0 0 60" />
        </pattern>

        <clipPath id={discClipId}>
          <circle cx={EARTH_CX} cy={EARTH_CY} r={EARTH_R} />
        </clipPath>
      </defs>

      {/* --- Survey register ------------------------------------------------------------ */}
      <rect x="0" y="0" width="1200" height="760" fill={`url(#${gridPatternId})`} />

      {/* --- Shells, drawn BEFORE the planet so the disc occludes what passes behind it --- */}
      <g transform={`translate(${EARTH_CX} ${EARTH_CY}) rotate(${SHELL_TILT_DEG})`}>
        {SHELLS.map((shell, index) => (
          <g key={shell.label}>
            <path
              className={`mk-scene-fallback__shell mk-scene-fallback__shell--far-${index + 1}`}
              d={`M ${-shell.rx} 0 A ${shell.rx} ${shell.ry} 0 0 1 ${shell.rx} 0`}
            />
            <path
              className={`mk-scene-fallback__shell mk-scene-fallback__shell--near-${index + 1}`}
              d={`M ${shell.rx} 0 A ${shell.rx} ${shell.ry} 0 0 1 ${-shell.rx} 0`}
            />
          </g>
        ))}
      </g>

      {/* --- Earth ---------------------------------------------------------------------- */}
      <circle
        cx={EARTH_CX}
        cy={EARTH_CY}
        r={EARTH_R * 1.13}
        fill={`url(#${limbGradientId})`}
      />
      <circle cx={EARTH_CX} cy={EARTH_CY} r={EARTH_R} fill={`url(#${bodyGradientId})`} />

      <g clipPath={`url(#${discClipId})`} className="mk-scene-fallback__graticule">
        {/* Parallels: ellipses of decreasing semi-minor axis, which is what latitude circles
            project to when the pole is tilted toward the viewer. */}
        <ellipse cx={EARTH_CX} cy={EARTH_CY} rx={EARTH_R} ry={148} />
        <ellipse cx={EARTH_CX} cy={EARTH_CY - 210} rx={410} ry={124} />
        <ellipse cx={EARTH_CX} cy={EARTH_CY + 210} rx={410} ry={124} />
        {/* Meridians: ellipses sharing the polar axis, at three longitudes. */}
        <ellipse cx={EARTH_CX} cy={EARTH_CY} rx={168} ry={EARTH_R} />
        <ellipse cx={EARTH_CX} cy={EARTH_CY} rx={352} ry={EARTH_R} />
        <ellipse cx={EARTH_CX} cy={EARTH_CY} rx={EARTH_R} ry={EARTH_R} />
      </g>

      <circle
        className="mk-scene-fallback__limb-edge"
        cx={EARTH_CX}
        cy={EARTH_CY}
        r={EARTH_R}
        fill="none"
      />

      {/* --- Transfer arc ---------------------------------------------------------------
          Drawn twice: a wide, low-opacity pass for the bloom and a crisp pass on top. A
          Gaussian filter would give a softer halo, but filters rasterise their whole region
          and this is the path served to machines that could not run WebGL in the first
          place. */}
      <path
        className="mk-scene-fallback__arc-bloom"
        d={`M ${DEPARTURE_X} ${DEPARTURE_Y} C 520 196, 838 176, ${ARRIVAL_X} ${ARRIVAL_Y}`}
        stroke={`url(#${arcGradientId})`}
        fill="none"
      />
      <path
        className="mk-scene-fallback__arc"
        d={`M ${DEPARTURE_X} ${DEPARTURE_Y} C 520 196, 838 176, ${ARRIVAL_X} ${ARRIVAL_Y}`}
        stroke={`url(#${arcGradientId})`}
        fill="none"
      />

      {/* --- Nodes ----------------------------------------------------------------------- */}
      <Crosshair x={DEPARTURE_X} y={DEPARTURE_Y} size={26} />
      <Crosshair x={ARRIVAL_X} y={ARRIVAL_Y} size={26} />
      <g className="mk-scene-fallback__apex">
        <circle cx={APEX_X} cy={APEX_Y} r={4.5} />
        <circle className="mk-scene-fallback__apex-ring" cx={APEX_X} cy={APEX_Y} r={13} />
      </g>

      {/* --- Labels ---------------------------------------------------------------------- */}
      <g className="mk-scene-fallback__label">
        <text x={DEPARTURE_X + 30} y={402}>
          RESCUE VEHICLE
        </text>
        <text className="mk-scene-fallback__label--dim" x={DEPARTURE_X + 30} y={420}>
          DEPARTURE NODE
        </text>

        <text x={984} y={428} textAnchor="end">
          TARGET — DISABLED
        </text>
        <text className="mk-scene-fallback__label--dim" x={984} y={446} textAnchor="end">
          ARRIVAL NODE
        </text>

        <text className="mk-scene-fallback__label--accent" x={APEX_X} y={APEX_Y + 44} textAnchor="middle">
          COMPUTED TRANSFER ARC
        </text>
        <text className="mk-scene-fallback__label--dim" x={APEX_X} y={APEX_Y + 66} textAnchor="middle">
          ILLUSTRATIVE GEOMETRY — NOT ENGINE OUTPUT
        </text>

        {SHELLS.map((shell, index) => {
          const point = SHELL_LABEL_POINTS[index];
          if (!point) return null;
          return (
            <g key={shell.label}>
              <line
                className="mk-scene-fallback__leader"
                x1={point.x}
                y1={point.y}
                x2={point.x + 12}
                y2={point.y}
              />
              <text className="mk-scene-fallback__label--dim" x={point.x + 18} y={point.y + 4}>
                {shell.label}
              </text>
            </g>
          );
        })}

        <line className="mk-scene-fallback__leader" x1={734} y1={700} x2={734} y2={674} />
        <text className="mk-scene-fallback__label--dim" x={722} y={666} textAnchor="end">
          EARTH LIMB
        </text>
      </g>

      {/* --- Frame furniture --------------------------------------------------------------
          A measurement register that states what it is measuring and admits the transform
          applied to it. Deliberately no corner ticks: `preserveAspectRatio="slice"` crops
          whichever axis the container does not use, and a tick mark whose entire purpose is
          to sit at the frame edge is the one element guaranteed to be cut in half. Everything
          that has to be read is inside x 50..1160, y 118..700, and the one claim that must
          survive any crop — that this geometry is illustrative — sits at the optical centre
          beside the arc rather than in a corner. */}
      <g className="mk-scene-fallback__furniture">
        <line x1={60} y1={148} x2={300} y2={148} />
        <line x1={60} y1={142} x2={60} y2={154} />
        <line x1={120} y1={144} x2={120} y2={152} />
        <line x1={180} y1={144} x2={180} y2={152} />
        <line x1={240} y1={144} x2={240} y2={152} />
        <line x1={300} y1={142} x2={300} y2={154} />
      </g>

      <g className="mk-scene-fallback__label">
        <text className="mk-scene-fallback__label--dim" x={60} y={132}>
          ALTITUDE EXAGGERATED — coordinates.ts
        </text>
        <text x={1140} y={128} textAnchor="end">
          STATIC SCHEMATIC
        </text>
      </g>
      <rect className="mk-scene-fallback__status-dot" x={1150} y={120} width="9" height="9" />
    </svg>
  );
}
