/**
 * The close.
 *
 * The honest position this section has to hold: there is no authentication backend in this
 * repository, and the workspace is not gated behind one. Pretending that access is granted by
 * a sign-up form would be the single most dishonest thing this page could do, so the section
 * offers three routes and says plainly what each one actually does — request access (records
 * nothing, because nothing is listening), sign in (fails honestly), and open the workspace,
 * which is the only surface that reaches the C++ engine at all.
 *
 * The background is drawn, not imported: four ruled orbital shells and one transfer arc
 * between two of them. The arc is the only spectral element in the band and it earns the ramp
 * by being the thing the whole project computes — a path from one orbit to another. Every
 * colour in it comes from a class in `footer.css`, never from an attribute here.
 */

import { usePrefersReducedMotion } from '../../hooks/usePrefersReducedMotion';
import { useRoute } from '../../routes/RouteContext';
import { ActionButton, Reveal, SectionHeading, StatusDot } from '../components';

/**
 * Geometry is fixed rather than generated: these are four concentric shells and one transfer
 * between shell 1 and shell 3, with the endpoints computed on the rotated ellipses so the arc
 * actually starts and ends on a ring instead of near one. `xMaxYMid slice` keeps the ring
 * centre anchored to the right edge, so the composition survives being cropped at 360px.
 */
const ARC_CENTRE_X = 980;
const ARC_CENTRE_Y = 340;
const ARC_ROTATION = 'rotate(-22 980 340)';

const SHELLS: readonly { readonly rx: number; readonly ry: number; readonly depth: string }[] = [
  { rx: 160, ry: 66, depth: 'near' },
  { rx: 250, ry: 104, depth: 'mid' },
  { rx: 360, ry: 150, depth: 'mid' },
  { rx: 500, ry: 208, depth: 'far' },
];

/** Departure on shell 1, arrival on shell 3; the control points bow the path away from the
 * central body so it reads as a transfer rather than as a chord cutting through the planet. */
const TRANSFER_PATH = 'M 832 400 C 782 448 730 486 672 506';
const DEPARTURE = { x: 832, y: 400 };
const ARRIVAL = { x: 672, y: 506 };

function OrbitalArcs() {
  return (
    <svg
      className="mk-access-arcs"
      viewBox="0 0 1200 700"
      preserveAspectRatio="xMaxYMid slice"
      aria-hidden="true"
      focusable="false"
    >
      <defs>
        <linearGradient id="mk-access-ramp" x1="1" y1="0" x2="0" y2="1">
          <stop offset="0%" className="mk-access-arcs__stop mk-access-arcs__stop--1" />
          <stop offset="52%" className="mk-access-arcs__stop mk-access-arcs__stop--2" />
          <stop offset="100%" className="mk-access-arcs__stop mk-access-arcs__stop--3" />
        </linearGradient>
      </defs>

      <g transform={ARC_ROTATION}>
        {SHELLS.map((shell) => (
          <ellipse
            key={shell.rx}
            className={`mk-access-arcs__ring mk-access-arcs__ring--${shell.depth}`}
            cx={ARC_CENTRE_X}
            cy={ARC_CENTRE_Y}
            rx={shell.rx}
            ry={shell.ry}
          />
        ))}
      </g>

      <circle
        className="mk-access-arcs__body"
        cx={ARC_CENTRE_X}
        cy={ARC_CENTRE_Y}
        r={30}
      />

      <path className="mk-access-arcs__transfer" d={TRANSFER_PATH} />
      <path className="mk-access-arcs__spark" d={TRANSFER_PATH} />

      <circle className="mk-access-arcs__node" cx={DEPARTURE.x} cy={DEPARTURE.y} r={3.5} />
      <circle className="mk-access-arcs__node" cx={ARRIVAL.x} cy={ARRIVAL.y} r={3.5} />
    </svg>
  );
}

export function AccessSection() {
  const { navigate } = useRoute();
  // The arcs are decorative, so under a reduced-motion request the whole layer is rendered
  // static rather than merely slowed: the moving element is the one thing here that could
  // provoke the setting.
  const reducedMotion = usePrefersReducedMotion();

  const arcsClass = reducedMotion
    ? 'mk-access__backdrop mk-access__backdrop--static'
    : 'mk-access__backdrop';

  return (
    <section id="access" className="mk-section mk-access">
      <div className={arcsClass} aria-hidden="true">
        <OrbitalArcs />
      </div>

      <div className="mk-shell mk-access__shell">
        <SectionHeading
          index="07"
          label="Access"
          title={
            <>
              <span className="mk-access__title-lead">Nothing here asks to be believed.</span>
              It asks to be run.
            </>
          }
          lede={
            'The engine is standalone C++20 with no browser and no Node process in its ' +
            'dependency path, the benchmark suite reproduces the search and assignment tables ' +
            'above in about seven seconds, and the rendering measurement is a separate ' +
            'instrumented browser run. Each figure names the document it came from. If you work ' +
            'on trajectory optimisation, multi-objective search or assignment under scarcity, ' +
            'the interesting part is where the measurements refused to agree with the hypothesis.'
          }
        />

        <Reveal className="mk-access__panel">
          <div className="mk-access__actions">
            <ActionButton variant="primary" onClick={() => navigate('signup')}>
              Request research access
            </ActionButton>

            <p className="mk-access__quiet-row">
              <span className="mk-access__quiet-lead">Already have access?</span>
              <ActionButton variant="quiet" onClick={() => navigate('signin')}>
                Sign in
              </ActionButton>
            </p>

            <p className="mk-access__quiet-row">
              <span className="mk-access__quiet-lead">Running ORCA locally?</span>
              <ActionButton variant="quiet" onClick={() => navigate('workspace')}>
                Open the workspace
              </ActionButton>
            </p>
          </div>

          <div className="mk-access__note">
            <p className="mk-access__note-status">
              <StatusDot tone="neutral" />
              <span className="mk-mono">Authentication service · not connected</span>
            </p>
            <p className="mk-access__note-body">
              Registration has no backend in this build. There is no authentication service in
              this repository, so the sign-up and sign-in forms report that plainly instead of
              simulating a session, and no credential you type is stored or transmitted
              anywhere. The workspace is not gated: it runs locally against the C++ engine
              through the Node bridge, and it is the only surface on this site that talks to the
              engine at all.
            </p>
            <p className="mk-access__note-source mk-mono">Source · README</p>
          </div>
        </Reveal>
      </div>
    </section>
  );
}
