/**
 * The evaluation section: the place the project either earns belief or does not.
 *
 * It is modelled on this repository's own reporting culture rather than on a marketing
 * "results" band. `docs/FINAL_BENCHMARK_REPORT.md` §8 is titled "Interpretation — Do Not
 * Oversell" and states that reporting the instancing win without its caveat "would be
 * misleading"; §7 of `docs/RESEARCH_EVALUATION.md` lists what the evaluation deliberately does
 * not answer. Both are reproduced here, so the page cannot claim more than the documents do.
 *
 * Three structural consequences of that:
 *
 * 1. Every finding renders with its caveat. `ResearchResult.caveat` is a required field, so a
 *    result physically cannot appear on this page stripped of its limitation.
 * 2. Figures are real `<table>` elements with a `<caption>` and scoped headers, not styled
 *    divs. A reader who wants to check a number should be able to read it the way they read a
 *    table in the source document, including with a screen reader.
 * 3. Nothing is coloured for emphasis. The single accent in this section is amber, and it
 *    means exactly one thing: a limitation attached to the finding above it.
 */

import { Reveal, SectionHeading } from '../components';
import { DISCLOSURE, MEASUREMENT_ENVIRONMENT, RESEARCH_RESULTS } from '../data/landingContent';
import type { ResearchResult, ResearchTable } from '../data/landingContent';

/**
 * The assignment result gets its own treatment because the benchmark report singles it out:
 * it is "the strongest result in this report specifically because a wrong, more
 * flattering-sounding initial reading ... was caught and corrected before being reported"
 * (docs/FINAL_BENCHMARK_REPORT.md §8). A correction found before publication is evidence about
 * the process, not just about the algorithm, so it is presented as such.
 */
const CORRECTION_RESULT_ID = 'assignment-quality';

const CORRECTION_NOTE =
  'This is the result the benchmark report itself calls the strongest one on record, and the ' +
  'reason is not the algorithm. A more flattering reading was available, it was checked, it ' +
  'was wrong, and it was corrected before anything was published. The correction is left ' +
  'visible below rather than replaced by its conclusion.';

const CORRECTION_NOTE_SOURCE = 'docs/FINAL_BENCHMARK_REPORT.md §8';

interface OpenQuestion {
  readonly id: string;
  readonly title: string;
  readonly body: string;
  readonly source: string;
}

/**
 * Transcribed from the two "what this does not answer" passages in the repository, and held
 * here rather than in `landingContent.ts` only because that module is owned elsewhere in this
 * change. Each entry keeps the document that states the limitation, for the same reason every
 * result does: a disclosure a reader cannot check is a disclaimer, not a disclosure.
 */
const OPEN_QUESTIONS: readonly OpenQuestion[] = [
  {
    id: 'mission-outcomes',
    title: 'Whether Pareto planning produces better missions',
    body:
      'The evaluation measures search and assignment in isolation. It does not answer whether ' +
      'Pareto-planned missions achieve better outcomes — success rate, propellant, risk — ' +
      'than greedy or single-objective planning end to end. Answering it needs full ' +
      'simulation runs across many seeded scenarios; it is explicitly deferred, not quietly ' +
      'omitted.',
    source: 'docs/RESEARCH_EVALUATION.md §7',
  },
  {
    id: 'significance',
    title: 'Statistical significance',
    body:
      'No hypothesis testing was performed. There are no Wilcoxon signed-rank tests and no ' +
      'confidence intervals; the engine computes mean, median, min, max and standard ' +
      'deviation, and this page reports exactly that and nothing beyond it.',
    source: 'docs/RESEARCH_EVALUATION.md §7',
  },
  {
    id: 'scaling-exponents',
    title: 'Fitted scaling behaviour',
    body:
      'The growth figures quoted above are simple ratios between the smallest and largest ' +
      'tested sizes. No curve was fitted, so none of them is an exponent with an error bar, ' +
      'and none should be extrapolated past 100 objects.',
    source: 'docs/RESEARCH_EVALUATION.md §7',
  },
  {
    id: 'generality',
    title: 'Anything about these algorithms in general',
    body:
      'The results come from one seed-paired benchmark suite on one machine. They characterise ' +
      "this repository's own graph and algorithm implementations. They are not claims about A*, " +
      'Dijkstra or label-setting search in the abstract, and results on other hardware will ' +
      'differ.',
    source: 'docs/FINAL_BENCHMARK_REPORT.md §8',
  },
];

/**
 * Figures align right so digits stack into a readable column; cells carrying a phrase rather
 * than a measurement align left, because a right-ragged sentence inside a table reads as a
 * layout fault. A lone em dash counts as a figure so placeholder cells stay on the numeric
 * axis instead of drifting to the other side of the column.
 */
const FIGURE_CELL = /^(—|[−–—<>≥≤~+-]?\s*[\d.])/u;

function isFigureCell(cell: string): boolean {
  return FIGURE_CELL.test(cell);
}

interface ResultTableProps {
  readonly table: ResearchTable;
  readonly captionId: string;
}

function ResultTable({ table, captionId }: ResultTableProps) {
  const [rowHeaderColumn, ...dataColumns] = table.columns;

  return (
    // The wrapper scrolls rather than the page, and carries `tabindex`/`role` so a keyboard
    // user can reach and scroll it -- an overflow container that only a mouse can pan is an
    // accessibility defect, not a responsive layout.
    <div
      className="mk-research-table-scroll"
      role="region"
      aria-labelledby={captionId}
      tabIndex={0}
    >
      <table className="mk-research-table">
        <caption className="mk-research-table__caption mk-mono" id={captionId}>
          {table.caption}
        </caption>
        <thead>
          <tr>
            <th scope="col" className="mk-research-table__rowhead">
              {rowHeaderColumn ?? ''}
            </th>
            {dataColumns.map((column) => (
              <th key={column} scope="col">
                {column}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {table.rows.map((row) => {
            const [rowHeader, ...cells] = row;
            const key = rowHeader ?? row.join(' ');
            return (
              <tr key={key}>
                <th scope="row" className="mk-research-table__rowhead">
                  {rowHeader ?? ''}
                </th>
                {cells.map((cell, index) => (
                  <td
                    key={`${key}:${index}`}
                    className={
                      isFigureCell(cell)
                        ? 'mk-research-table__cell'
                        : 'mk-research-table__cell mk-research-table__cell--text'
                    }
                  >
                    {cell}
                  </td>
                ))}
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}

interface ResearchResultPanelProps {
  readonly result: ResearchResult;
  readonly ordinal: number;
}

function ResearchResultPanel({ result, ordinal }: ResearchResultPanelProps) {
  const isCorrection = result.id === CORRECTION_RESULT_ID;
  const className = isCorrection
    ? 'mk-research-result mk-research-result--correction'
    : 'mk-research-result';

  return (
    <Reveal as="article" className={className}>
      <span className="mk-research-result__ordinal mk-mono" aria-hidden="true">
        {`R${ordinal}`}
      </span>

      <div className="mk-research-result__head">
        <p className="mk-eyebrow">{isCorrection ? 'Result · correction record' : 'Result'}</p>
        <h3 className="mk-research-result__question">{result.question}</h3>
      </div>

      <div className="mk-research-result__body">
        {isCorrection ? (
          <p className="mk-research-result__note">
            {CORRECTION_NOTE}{' '}
            <span className="mk-research-result__note-source mk-mono">
              {CORRECTION_NOTE_SOURCE}
            </span>
          </p>
        ) : null}

        <p className="mk-research-result__finding">{result.finding}</p>

        <ResultTable table={result.table} captionId={`mk-research-${result.id}-caption`} />

        <div className="mk-research-caveat">
          <p className="mk-research-caveat__label mk-mono">
            {isCorrection ? 'What the flattering reading got wrong' : 'Limitation'}
          </p>
          <p className="mk-research-caveat__body">{result.caveat}</p>
        </div>

        <p className="mk-research-result__source mk-mono">Source · {result.source}</p>
      </div>
    </Reveal>
  );
}

interface EnvironmentField {
  readonly term: string;
  readonly value: string;
  /** Takes two grid tracks on a wide viewport. A compiler flag list broken over four lines in
   * a 15rem column stops reading as one statement. */
  readonly wide?: boolean;
}

/** Ordered as a methods statement reads: what it ran on, what built it, when, and what was
 * actually run. A millisecond figure without these is a number, not a measurement. The two
 * long values are last so they pack into their own row rather than leaving a hole above. */
const ENVIRONMENT_FIELDS: readonly EnvironmentField[] = [
  { term: 'Machine', value: MEASUREMENT_ENVIRONMENT.machine },
  { term: 'Operating system', value: MEASUREMENT_ENVIRONMENT.os },
  { term: 'Compiler', value: MEASUREMENT_ENVIRONMENT.compiler },
  { term: 'Measured', value: MEASUREMENT_ENVIRONMENT.date },
  { term: 'Build flags', value: MEASUREMENT_ENVIRONMENT.flags, wide: true },
  { term: 'Suite', value: MEASUREMENT_ENVIRONMENT.suite, wide: true },
];

export function ResearchSection() {
  return (
    <section id="research" className="mk-section mk-research">
      <div className="mk-shell">
        <SectionHeading
          index="06"
          label="Evaluation"
          title="The measurements, with the results that went against the hypothesis"
          lede={
            'Four questions the benchmark suite was built to answer, each reported with the ' +
            'limitation that qualifies it and the document the figures were transcribed from. ' +
            'Where a more flattering reading existed and turned out to be wrong, the wrong ' +
            'reading is shown too.'
          }
        />

        <Reveal className="mk-research-methods">
          <div className="mk-research-methods__head">
            <p className="mk-eyebrow">Methods</p>
            <h3 className="mk-research-methods__title">Measurement environment</h3>
          </div>
          <dl className="mk-research-methods__grid">
            {ENVIRONMENT_FIELDS.map((field) => (
              <div
                key={field.term}
                className={
                  field.wide
                    ? 'mk-research-methods__item mk-research-methods__item--wide'
                    : 'mk-research-methods__item'
                }
              >
                <dt className="mk-research-methods__term mk-mono">{field.term}</dt>
                <dd className="mk-research-methods__value">{field.value}</dd>
              </div>
            ))}
          </dl>
          <p className="mk-research-methods__source mk-mono">
            Source · {MEASUREMENT_ENVIRONMENT.source}
          </p>
        </Reveal>

        <div className="mk-research-results">
          {RESEARCH_RESULTS.map((result, index) => (
            <ResearchResultPanel key={result.id} result={result} ordinal={index + 1} />
          ))}
        </div>

        <Reveal className="mk-research-disclosure">
          <div className="mk-research-disclosure__head">
            <p className="mk-eyebrow">{DISCLOSURE.heading}</p>
            <h3 className="mk-research-disclosure__title">What this does not answer</h3>
            <p className="mk-research-disclosure__source mk-mono">Source · {DISCLOSURE.source}</p>
          </div>

          <div className="mk-research-disclosure__body">
            <p className="mk-research-disclosure__lede">{DISCLOSURE.body}</p>
            <p className="mk-research-disclosure__lede">{DISCLOSURE.claims}</p>

            <ul className="mk-research-open">
              {OPEN_QUESTIONS.map((question) => (
                <li key={question.id} className="mk-research-open__item">
                  <p className="mk-research-open__title">{question.title}</p>
                  <p className="mk-research-open__body">{question.body}</p>
                  <p className="mk-research-open__source mk-mono">{question.source}</p>
                </li>
              ))}
            </ul>
          </div>
        </Reveal>
      </div>
    </section>
  );
}
