/**
 * The planning pipeline as a system map: a stage rail that selects, and a panel that shows what
 * the selected stage consumes, does and produces.
 *
 * ## Why it is interactive rather than five icons in a row
 *
 * A row of icon cards states that five things exist. What matters about this pipeline is what
 * each stage *transforms* -- three named inputs into one named output -- and that the last
 * stage feeds back into the second. Those are relations, and a reader can only check them by
 * looking at one stage at a time against the same frame.
 *
 * ## Interaction rules
 *
 * The rail is a set of toggle buttons, so `aria-pressed` reports selection and `aria-controls`
 * names the panel each one governs. Arrow keys move selection and focus together; every button
 * also stays in the tab order, because a reader who tabs into the rail and presses Tab again
 * should not be trapped in a roving-tabindex widget they were given no instructions for.
 *
 * Auto-advance exists to show that the rail is operable, and it gives up the moment it stops
 * being an introduction: it never starts before the section has been seen, it pauses under the
 * pointer and under focus, it stops in a hidden tab, it is off entirely under reduced motion,
 * and the first deliberate selection ends it permanently. Content that keeps moving after a
 * reader has taken hold of it is a defect, not a feature.
 */

import { useCallback, useEffect, useRef, useState } from 'react';
import type { CSSProperties, KeyboardEvent as ReactKeyboardEvent } from 'react';

import { useDocumentVisible } from '../../hooks/useDocumentVisible';
import { usePrefersReducedMotion } from '../../hooks/usePrefersReducedMotion';
import { useScrollReveal } from '../../hooks/useScrollReveal';
import { SectionHeading } from '../components';
import { PIPELINE_STAGES } from '../data/landingContent';

const PANEL_ID = 'mk-pipeline-panel';

/** Long enough to read a stage title and its tag before the next one arrives. Anything under
 * about four seconds reads as a slideshow rather than as a demonstration. */
const AUTO_ADVANCE_MS = 6500;

/**
 * The transformation, drawn as geometry only: three inputs converge on the stage node, one
 * edge leaves it, and the closed loop returns to the input side.
 *
 * There is deliberately no text in this SVG. Every label the diagram would carry is real HTML
 * beside it, so it can be selected, searched, translated and read aloud -- an image of a word
 * is not a word.
 */
function FlowDiagram({ feedbackActive }: { readonly feedbackActive: boolean }) {
  return (
    <svg
      className="mk-flow"
      viewBox="0 0 300 210"
      fill="none"
      aria-hidden="true"
      focusable="false"
    >
      <path className="mk-flow__edge" d="M16 42C70 42 88 105 128 105" />
      <path className="mk-flow__edge" d="M16 105H128" />
      <path className="mk-flow__edge" d="M16 168C70 168 88 105 128 105" />

      <path
        className={feedbackActive ? 'mk-flow__edge is-feedback is-live' : 'mk-flow__edge is-feedback'}
        d="M284 112V190C284 198 278 202 268 202H32C22 202 16 198 16 190V112"
      />

      <path className="mk-flow__edge mk-flow__edge--out" d="M172 105H278" />

      <circle className="mk-flow__node" cx="16" cy="42" r="3.6" />
      <circle className="mk-flow__node" cx="16" cy="105" r="3.6" />
      <circle className="mk-flow__node" cx="16" cy="168" r="3.6" />

      <circle className="mk-flow__core-ring" cx="150" cy="105" r="31" />
      <path className="mk-flow__core" d="M150 83L172 105L150 127L128 105Z" />

      <circle className="mk-flow__node mk-flow__node--out" cx="284" cy="105" r="5" />
    </svg>
  );
}

export function PipelineSection() {
  const reducedMotion = usePrefersReducedMotion();
  const documentVisible = useDocumentVisible();
  const { ref: sectionRef, revealed } = useScrollReveal<HTMLElement>();

  const firstStage = PIPELINE_STAGES[0];
  const [activeId, setActiveId] = useState<string>(firstStage?.id ?? '');
  const [autoPaused, setAutoPaused] = useState(false);
  const [userSelected, setUserSelected] = useState(false);
  const buttonRefs = useRef<(HTMLButtonElement | null)[]>([]);

  const selectStage = useCallback((id: string) => {
    setUserSelected(true);
    setActiveId(id);
  }, []);

  useEffect(() => {
    if (reducedMotion || userSelected || autoPaused || !documentVisible || !revealed) {
      return undefined;
    }

    const timer = window.setTimeout(() => {
      setActiveId((current) => {
        const position = PIPELINE_STAGES.findIndex((stage) => stage.id === current);
        return PIPELINE_STAGES[(position + 1) % PIPELINE_STAGES.length]?.id ?? current;
      });
    }, AUTO_ADVANCE_MS);

    return () => window.clearTimeout(timer);
  }, [reducedMotion, userSelected, autoPaused, documentVisible, revealed, activeId]);

  function handleStageKeyDown(event: ReactKeyboardEvent<HTMLButtonElement>, position: number) {
    const total = PIPELINE_STAGES.length;
    let target = -1;

    if (event.key === 'ArrowDown' || event.key === 'ArrowRight') target = (position + 1) % total;
    else if (event.key === 'ArrowUp' || event.key === 'ArrowLeft') {
      target = (position - 1 + total) % total;
    } else if (event.key === 'Home') target = 0;
    else if (event.key === 'End') target = total - 1;

    if (target < 0) return;

    const stage = PIPELINE_STAGES[target];
    if (!stage) return;

    // Only now: arrow keys inside a page still scroll, and taking that over for a key this
    // component does not handle would be worse than not handling it at all.
    event.preventDefault();
    selectStage(stage.id);
    buttonRefs.current[target]?.focus();
  }

  const activePosition = PIPELINE_STAGES.findIndex((stage) => stage.id === activeId);
  const active = PIPELINE_STAGES[activePosition] ?? firstStage;

  // `PIPELINE_STAGES` is a non-empty literal, so this branch is unreachable in practice; it
  // exists because an indexed read is `T | undefined` under this tsconfig and a section that
  // rendered half a panel would be worse than one that renders none.
  if (!active) return null;

  const total = PIPELINE_STAGES.length;
  const progress = (Math.max(activePosition, 0) + 1) / total;

  return (
    <section
      ref={sectionRef}
      id="approach"
      className="mk-section mk-pipeline"
      aria-labelledby="mk-pipeline-title"
    >
      <div className="mk-shell">
        <SectionHeading
          index="02"
          label="Planning pipeline"
          title="Five stages, and the loop that closes them"
          id="mk-pipeline-title"
          lede={
            'Each stage consumes named inputs and produces one named result. Select a stage to ' +
            'see what it transforms; the final stage returns its measured state to the second, ' +
            'which is the only version of this problem that survives contact with reality.'
          }
        />

        <div
          className={revealed ? 'mk-pipeline__layout mk-reveal is-revealed' : 'mk-pipeline__layout mk-reveal'}
          onPointerEnter={() => setAutoPaused(true)}
          onPointerLeave={() => setAutoPaused(false)}
          onFocus={() => setAutoPaused(true)}
          onBlur={() => setAutoPaused(false)}
        >
          <div className="mk-pipeline__rail">
            <p className="mk-eyebrow" id="mk-pipeline-rail-label">
              Stages
            </p>
            <ul
              className="mk-pipeline__stages"
              style={{ '--mk-pipeline-progress': `${progress}` } as CSSProperties}
            >
              {PIPELINE_STAGES.map((stage, position) => {
                const selected = stage.id === active.id;
                return (
                  <li key={stage.id}>
                    <button
                      ref={(node) => {
                        buttonRefs.current[position] = node;
                      }}
                      type="button"
                      className={selected ? 'mk-pipeline__stage is-selected' : 'mk-pipeline__stage'}
                      aria-pressed={selected}
                      aria-controls={PANEL_ID}
                      onClick={() => selectStage(stage.id)}
                      onKeyDown={(event) => handleStageKeyDown(event, position)}
                    >
                      <span className="mk-pipeline__stage-index mk-mono">{stage.index}</span>
                      <span className="mk-pipeline__stage-text">
                        <span className="mk-pipeline__stage-title">{stage.title}</span>
                        <span className="mk-pipeline__stage-tag mk-mono">{stage.tag}</span>
                      </span>
                    </button>
                  </li>
                );
              })}
            </ul>
          </div>

          <div id={PANEL_ID} className="mk-pipeline__panel">
            <div className="mk-pipeline__panel-body" key={active.id}>
              <header className="mk-pipeline__panel-head">
                <span className="mk-pipeline__panel-step mk-mono">
                  Stage {active.index} / {String(total).padStart(2, '0')}
                </span>
                <h3 className="mk-pipeline__panel-title">{active.title}</h3>
                <p className="mk-pipeline__panel-text">{active.body}</p>
              </header>

              <div className="mk-pipeline__flow">
                <div className="mk-pipeline__io">
                  <p className="mk-eyebrow">Inputs</p>
                  <ul className="mk-pipeline__io-list">
                    {active.inputs.map((input) => (
                      <li key={input} className="mk-pipeline__io-item">
                        {input}
                      </li>
                    ))}
                  </ul>
                </div>

                <FlowDiagram feedbackActive={active.id === 'replan'} />

                <div className="mk-pipeline__io mk-pipeline__io--out">
                  <p className="mk-eyebrow">Output</p>
                  <p className="mk-pipeline__output">{active.output}</p>
                </div>
              </div>

              <p className="mk-pipeline__loop">
                <span className="mk-technical-tag">Closed loop</span>
                Stage 05 returns measured position, achieved delta-v and revised risk to stage 02,
                so the feasible envelope is rebuilt against the state that actually exists rather
                than the one that was planned against.
              </p>
            </div>
          </div>
        </div>

        {/* Polite, and outside the panel: the panel's own content changes wholesale, and a live
            region around it would read the entire stage aloud on every arrow press. */}
        <p className="mk-pipeline__announce" aria-live="polite">
          {`Stage ${active.index} of ${String(total).padStart(2, '0')}: ${active.title}`}
        </p>
      </div>
    </section>
  );
}
