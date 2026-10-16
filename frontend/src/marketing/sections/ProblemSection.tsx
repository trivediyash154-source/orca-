/**
 * The problem statement, and the one fact that settles whether it is a real problem.
 *
 * The three constraints are laid out as a divided ledger -- full-bleed columns separated by
 * hairlines -- rather than as three cards. The distinction is not stylistic: cards read as
 * three product features of equal, interchangeable weight, and these are three coupled
 * physical constraints that a reader is meant to hold at once. Ruled columns are how a
 * technical document presents that; boxes are how a brochure presents features.
 *
 * Each column carries its own geometric figure, drawn inline. They are motifs, not plots: no
 * data is encoded in them and none is claimed to be, which is why they carry no axes, no
 * numbers and no legend. Every number in this section is in the text, cited.
 *
 * The 25.4-hour synodic period is pulled out as a figure because it is the section's load
 * -bearing claim. Without it, "propellant and time conflict" is an assertion a reader has to
 * take on trust; with it, the conflict is a property of the two orbits.
 */

import { Reveal, SectionHeading } from '../components';
import { PROBLEM_CONSTRAINTS } from '../data/landingContent';

/** Last known fix, then three states of growing positional uncertainty, sampled along one
 * inclined orbit. The growth is the subject, so the marker radii are the only thing that
 * changes between them. */
function TargetStateFigure() {
  return (
    <svg
      className="mk-figure"
      viewBox="0 0 180 110"
      fill="none"
      aria-hidden="true"
      focusable="false"
    >
      <ellipse
        className="mk-figure__orbit"
        cx="90"
        cy="55"
        rx="76"
        ry="34"
        transform="rotate(-12 90 55)"
      />
      <circle className="mk-figure__node" cx="17.7" cy="58.5" r="3.4" />
      <circle className="mk-figure__uncertain" cx="57.9" cy="29.2" r="6" />
      <circle className="mk-figure__uncertain" cx="108.8" cy="18.3" r="10" />
      <circle className="mk-figure__uncertain" cx="150.9" cy="24.7" r="14.5" />
      <path className="mk-figure__tick" d="M17.7 44.5v-9M17.7 72.5v9" />
    </svg>
  );
}

/** Two circular orbits and the tangential transfer between them, with the departure phase
 * angle marked. The transfer arc touches both circles because a Hohmann transfer does; drawing
 * it as a decorative swoosh that missed them would be a picture of nothing. */
function MarginsFigure() {
  return (
    <svg
      className="mk-figure"
      viewBox="0 0 180 110"
      fill="none"
      aria-hidden="true"
      focusable="false"
    >
      <circle className="mk-figure__orbit" cx="58" cy="55" r="22" />
      <circle className="mk-figure__orbit" cx="58" cy="55" r="40" />
      <path className="mk-figure__transfer" d="M36 55A31 29.7 0 0 1 98 55" />
      <path className="mk-figure__phase" d="M58 55h40M58 55l20-34.6" />
      <path className="mk-figure__phase-arc" d="M72 55A14 14 0 0 0 65 42.9" />
      <circle className="mk-figure__node" cx="36" cy="55" r="3.2" />
      <circle className="mk-figure__node-open" cx="98" cy="55" r="3.2" />
      <path className="mk-figure__tick" d="M132 34h24M132 55h24M132 76h24" />
    </svg>
  );
}

/** The direct chord crosses four occupied debris cells; the flown path does not, and is longer
 * for it. The uniform grid is the engine's actual spatial index structure, not a texture. */
function CouplingFigure() {
  return (
    <svg
      className="mk-figure"
      viewBox="0 0 180 110"
      fill="none"
      aria-hidden="true"
      focusable="false"
    >
      <g className="mk-figure__grid">
        <path d="M64 26h60M64 38h60M64 50h60M64 62h60M64 74h60" />
        <path d="M64 26v48M76 26v48M88 26v48M100 26v48M112 26v48M124 26v48" />
      </g>
      <g className="mk-figure__occupied">
        <rect x="64" y="50" width="12" height="12" />
        <rect x="76" y="50" width="12" height="12" />
        <rect x="88" y="50" width="12" height="12" />
        <rect x="100" y="38" width="12" height="12" />
      </g>
      <path className="mk-figure__chord" d="M8 82L172 22" />
      <path className="mk-figure__transfer" d="M8 82C48 92 74 88 96 76S140 44 172 22" />
      <circle className="mk-figure__node" cx="8" cy="82" r="3.2" />
      <circle className="mk-figure__node-open" cx="172" cy="22" r="3.2" />
    </svg>
  );
}

function ConstraintFigure({ index }: { readonly index: string }) {
  if (index === '01') return <TargetStateFigure />;
  if (index === '02') return <MarginsFigure />;
  return <CouplingFigure />;
}

interface DefinitionRow {
  readonly term: string;
  readonly detail: string;
}

/** A restatement of the pipeline in three lines, kept here so the section can say what ORCA
 * *is* before the next section says how it works. */
const DEFINITION_ROWS: readonly DefinitionRow[] = [
  {
    term: 'Input',
    detail:
      'Orbital elements for every object in play, the rescue vehicle’s propellant state, and a ' +
      'declared emergency with its survival deadline.',
  },
  {
    term: 'Search',
    detail:
      'Multi-objective label-setting over a feasible transfer graph, pruned by Pareto ' +
      'dominance rather than collapsed into a weighted score.',
  },
  {
    term: 'Output',
    detail:
      'The nondominated set — every plan that no other plan beats on all objectives at once — ' +
      'with the trade each one makes stated.',
  },
];

export function ProblemSection() {
  return (
    <section id="overview" className="mk-section mk-problem" aria-labelledby="mk-problem-title">
      <div className="mk-shell">
        <SectionHeading
          index="01"
          label="Problem definition"
          title="The mission is never one-dimensional"
          id="mk-problem-title"
          lede={
            'A rescue transfer has to be cheap enough to fly, fast enough to matter and safe ' +
            'enough to survive. Those three requirements are not independent, and no ordering ' +
            'of them is neutral.'
          }
        />

        <Reveal className="mk-problem__statement">
          <div className="mk-problem__statement-copy">
            <p>
              ORCA plans a transfer to a vehicle that cannot recover on its own. The planning
              problem is not shortest path: propellant, transfer time, collision exposure,
              recovery probability and the reserve held back for the return leg are separate
              quantities, and improving any one of them moves the others.
            </p>
            <p>
              The decision itself is made by a standalone C++20 engine that owns the graph, the
              search and the objective vector, and runs with no Node process and no browser
              attached. This interface displays what the engine decided and why; it never
              computes a value the engine could compute.
            </p>
          </div>

          <dl className="mk-problem__definition">
            {DEFINITION_ROWS.map((row) => (
              <div key={row.term} className="mk-problem__definition-row">
                <dt className="mk-mono">{row.term}</dt>
                <dd>{row.detail}</dd>
              </div>
            ))}
          </dl>
        </Reveal>
      </div>

      <div className="mk-problem__ledger">
        <h3 className="mk-problem__ledger-label mk-mono">Three constraints, all physical</h3>
        <div className="mk-problem__ledger-grid">
          {PROBLEM_CONSTRAINTS.map((constraint, position) => (
            <Reveal
              key={constraint.index}
              as="article"
              className="mk-problem__entry"
              delayStep={position}
            >
              <span className="mk-problem__entry-index mk-mono" aria-hidden="true">
                {constraint.index}
              </span>
              <ConstraintFigure index={constraint.index} />
              <h4 className="mk-problem__entry-title">{constraint.title}</h4>
              <p className="mk-problem__entry-body">{constraint.body}</p>
              <span className="mk-technical-tag">{constraint.tag}</span>
            </Reveal>
          ))}
        </div>
      </div>

      <div className="mk-shell">
        <Reveal>
          <figure className="mk-problem__pull">
            <div className="mk-problem__pull-value">
              <span className="mk-problem__pull-number mk-mono">25.4</span>
              <span className="mk-problem__pull-unit mk-mono">hours</span>
            </div>
            <p className="mk-problem__pull-body">
              The synodic period between a 500 km and an 800 km orbit. The fuel-optimal departure
              phase therefore recurs roughly once a day, while a survival window runs to tens of
              minutes. Waiting for the cheapest departure and arriving in time are, in the general
              case, incompatible requests.
            </p>
            {/* Direct child of the figure: a figcaption is only a caption where the parser can
                see that it is one. */}
            <figcaption className="mk-problem__pull-source mk-mono">
              Source — ORCA README and project documentation. The conflict is a property of the two
              orbits, not of the problem statement.
            </figcaption>
          </figure>
        </Reveal>
      </div>
    </section>
  );
}
