/**
 * Section 03 — the multi-objective argument, stated on the left and demonstrated on the right.
 *
 * The two halves are not decoration for each other. The prose makes a falsifiable claim (a
 * weighted sum cannot select a point sitting in a concave dent of the front, for any
 * non-negative weights — docs/OPTIMIZATION_MODEL.md §1.1, reason 2); the instrument beside it
 * lets a reader operate the two-stage alternative and watch the front stay fixed while the
 * preference moves across it.
 */

import { Reveal, SectionHeading } from '../components';
import { ParetoInstrument } from '../components/ParetoInstrument';
import { OBJECTIVES, OBJECTIVES_LIMIT_NOTE, OBJECTIVES_SOURCE } from '../data/landingContent';

const TITLE_ID = 'optimization-title';

/**
 * The concave-dent schematic from docs/OPTIMIZATION_MODEL.md §1.1, redrawn.
 *
 * Deliberately unlabelled on its axes beyond the objective names: it carries a geometric claim
 * about which points a supporting hyperplane can touch, and putting numbers on it would invite
 * the reader to treat a proof sketch as a measurement.
 */
function ConcaveDentFigure() {
  return (
    <figure className="mk-dent">
      <svg
        className="mk-dent__svg"
        viewBox="0 0 300 190"
        role="img"
        aria-label={
          'Schematic of a nonconvex Pareto front. Points A and C lie on the convex hull and are ' +
          'each selected by some weighted sum. Point B sits in a concave dent between them and ' +
          'is selected by no weight vector, although it may be the best available compromise.'
        }
      >
        <g className="mk-dent__axis">
          <line x1="34" y1="14" x2="34" y2="140" />
          <line x1="34" y1="140" x2="278" y2="140" />
        </g>

        {/* The supporting hyperplane (a line, in two objectives), drawn through A and C and
            extended past both. Delta-v increases upward and transfer time to the right, so the
            ideal corner is lower left: B lies on the far side of the chord from it, which is
            precisely what puts it out of reach. Move B below the chord and the figure would
            assert the opposite of what the caption claims. */}
        <line className="mk-dent__hull" x1="50" y1="28.5" x2="254" y2="121.5" />

        <g className="mk-dent__reachable">
          <circle cx="62" cy="34" r="4.5" />
          <circle cx="242" cy="116" r="4.5" />
        </g>
        <circle className="mk-dent__unreachable" cx="152" cy="50" r="5.2" />

        <g className="mk-dent__label">
          <text x="62" y="22" textAnchor="middle">
            A
          </text>
          <text x="163" y="54" textAnchor="start">
            B
          </text>
          <text x="252" y="112" textAnchor="middle">
            C
          </text>
          <text x="34" y="158" textAnchor="start" className="mk-dent__axis-title">
            TRANSFER TIME
          </text>
          <text
            className="mk-dent__axis-title"
            transform="translate(18 77) rotate(-90)"
            textAnchor="middle"
          >
            DELTA-V
          </text>
        </g>

        <text className="mk-dent__callout" x="152" y="182" textAnchor="middle">
          NO WEIGHT VECTOR SELECTS B
        </text>
      </svg>
      <figcaption className="mk-dent__caption">
        Schematic, after docs/OPTIMIZATION_MODEL.md §1.1. Sweeping every weight from 0 to 1
        returns A and C and never B.
      </figcaption>
    </figure>
  );
}

export function OptimizationSection() {
  return (
    <section id="optimization" className="mk-section" aria-labelledby={TITLE_ID}>
      <div className="mk-shell mk-optimisation">
        <Reveal className="mk-optimisation__aside">
          <SectionHeading
            index="03"
            label="Optimisation"
            title="The trade is made once, in the open"
            lede={
              'Four objectives are carried separately through the search. Nothing is collapsed ' +
              'into a single score before the options exist, because a score is an exchange rate ' +
              'chosen in advance by someone who has not yet seen what it costs.'
            }
            id={TITLE_ID}
          />

          <h3 className="mk-optimisation__subhead">Objectives carried separately</h3>
          {/* The MIN badge is on every row rather than on none because uniformity is the claim:
              nothing here is maximised, which is what lets the dominance test stay generic. */}
          <ul className="mk-objectives">
            {OBJECTIVES.map((objective) => (
              <li className="mk-objectives__item" key={objective.key}>
                <span className="mk-objectives__label">
                  {objective.label}
                  <span className="mk-objectives__unit mk-mono">{objective.unit}</span>
                </span>
                <span className="mk-objectives__dir mk-mono">MIN</span>
                <span className="mk-objectives__note">{objective.note}</span>
              </li>
            ))}
          </ul>
          <p className="mk-objectives__limit">{OBJECTIVES_LIMIT_NOTE}</p>
          <p className="mk-objectives__source mk-mono">Source: {OBJECTIVES_SOURCE}</p>

          <div className="mk-scalarisation">
            <h3 className="mk-scalarisation__title">Why not scalarise first</h3>
            <p className="mk-scalarisation__body">
              A weighted sum selects the point where a hyperplane of slope &minus;w first touches
              the solution set. A point lying in a concave dent of the front is never touched by
              any such hyperplane, for any non-negative weights — so no amount of tuning recovers
              it. This is a limitation of the method, not of the weights.
            </p>
            <ConcaveDentFigure />
            <p className="mk-scalarisation__body">
              ORCA therefore computes the Pareto-optimal set first and applies operator preference
              afterwards. Every front point — including the concave ones — is already computed by
              the time a preference is expressed, so the same objection does not bite at the
              selection stage.
            </p>
            <p className="mk-scalarisation__source mk-mono">
              Source: README; docs/OPTIMIZATION_MODEL.md §1.1
            </p>
          </div>
        </Reveal>

        <Reveal className="mk-optimisation__main" delayStep={1}>
          <ParetoInstrument labelledBy={TITLE_ID} />
        </Reveal>
      </div>
    </section>
  );
}
