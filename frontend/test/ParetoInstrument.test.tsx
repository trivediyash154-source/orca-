import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { ParetoInstrument } from '../src/marketing/components/ParetoInstrument';
import {
  DEMO_CANDIDATES,
  paretoFrontier,
  preferredCandidate,
} from '../src/marketing/data/paretoDemo';
import type { DemoCandidate, DemoPriority } from '../src/marketing/data/paretoDemo';

/** The trade-space instrument. The fixture it plots is illustrative and labelled as such, but
 * `paretoFrontier()` is not: it is an exact nondominance test, and the whole claim the section
 * makes is that the drawn front is the computed one. So the frontier is pinned against a
 * hand-checked expectation rather than against whatever the function currently returns.
 *
 * The second claim is the ordering of the two stages — the front is computed first, operator
 * preference selects from it afterwards, and preference never changes which points are on it.
 * That is asserted here on the rendered instrument, not just on the functions. */

/** Risk, recovery and reserve are deliberately identical across the fixture below: they are
 * carried alongside the trade but take no part in the dominance test, and a fixture that varied
 * them would leave that ambiguous. */
function candidate(id: string, deltaVMs: number, transferHours: number): DemoCandidate {
  return { id, deltaVMs, transferHours, riskIndex: 0.5, recoveryIndex: 0.5, fuelReservePct: 50 };
}

/**
 * Hand-checked by inspection over (delta-v, transfer time), both minimised:
 *
 *   A (100, 10)  cheapest — nothing has a lower delta-v, so nothing can dominate it
 *   B (150,  7)  cheaper AND faster than D
 *   C (200,  5)  fastest — nothing has a lower transfer time
 *   D (220,  9)  dominated by B on both axes, and by C on delta-v with a worse time
 *   E (150,  7)  identical to B; neither dominates the other, so both survive
 */
const HAND_FIXTURE: readonly DemoCandidate[] = [
  candidate('A', 100, 10),
  candidate('B', 150, 7),
  candidate('C', 200, 5),
  candidate('D', 220, 9),
  candidate('E', 150, 7),
];

/** The eight nondominated ids of `DEMO_CANDIDATES`, established by sweeping the fourteen points
 * in ascending delta-v and keeping each one whose transfer time beats every cheaper point's. */
const DEMO_FRONTIER_IDS: readonly string[] = [
  'TRJ-01',
  'TRJ-03',
  'TRJ-05',
  'TRJ-06',
  'TRJ-08',
  'TRJ-10',
  'TRJ-12',
  'TRJ-13',
];

function idsOf(candidates: readonly DemoCandidate[]): readonly string[] {
  return candidates.map((entry) => entry.id);
}

describe('paretoFrontier', () => {
  it('keeps exactly the nondominated points of a hand-checked fixture', () => {
    expect(new Set(idsOf(paretoFrontier(HAND_FIXTURE)))).toEqual(new Set(['A', 'B', 'C', 'E']));
  });

  it('excludes the deliberately dominated point', () => {
    expect(idsOf(paretoFrontier(HAND_FIXTURE))).not.toContain('D');
  });

  it('excludes it on the two plotted axes alone, not on the objectives carried alongside', () => {
    // D is best in the set on risk, recovery and reserve, and still does not survive: those
    // objectives are displayed, never part of this dominance test.
    const generous = { ...candidate('D', 220, 9), riskIndex: 0, recoveryIndex: 1, fuelReservePct: 99 };
    const withGenerousD = HAND_FIXTURE.map((entry) => (entry.id === 'D' ? generous : entry));
    expect(idsOf(paretoFrontier(withGenerousD))).not.toContain('D');
  });

  it('keeps both of two identical points, which cannot dominate each other', () => {
    const ids = idsOf(paretoFrontier(HAND_FIXTURE));
    expect(ids).toContain('B');
    expect(ids).toContain('E');
  });

  it('returns the survivors sorted by ascending delta-v, ready to be drawn as a polyline', () => {
    expect(idsOf(paretoFrontier(HAND_FIXTURE))).toEqual(['A', 'B', 'E', 'C']);
  });

  it('returns nothing for an empty set rather than inventing a point', () => {
    expect(paretoFrontier([])).toEqual([]);
  });

  it('returns the single point of a one-element set', () => {
    expect(idsOf(paretoFrontier([candidate('only', 1, 1)]))).toEqual(['only']);
  });

  it('finds exactly eight nondominated transfers in the shipped fixture', () => {
    const front = paretoFrontier(DEMO_CANDIDATES);
    expect(idsOf(front)).toEqual(DEMO_FRONTIER_IDS);
    expect(DEMO_CANDIDATES.length - front.length).toBe(6);
  });

  it('is idempotent: nothing on the front dominates anything else on it', () => {
    const front = paretoFrontier(DEMO_CANDIDATES);
    expect(idsOf(paretoFrontier(front))).toEqual(idsOf(front));
  });
});

describe('preferredCandidate', () => {
  const EXPECTED: Readonly<Record<DemoPriority, string>> = {
    balanced: 'TRJ-10',
    fuel: 'TRJ-06',
    time: 'TRJ-12',
    risk: 'TRJ-08',
  };

  it.each(Object.keys(EXPECTED) as readonly DemoPriority[])(
    'selects %s deterministically from the shipped fixture',
    (priority) => {
      expect(preferredCandidate(DEMO_CANDIDATES, priority)?.id).toBe(EXPECTED[priority]);
    },
  );

  it('actually honours the axis each priority names', () => {
    const fuel = preferredCandidate(DEMO_CANDIDATES, 'fuel');
    const time = preferredCandidate(DEMO_CANDIDATES, 'time');
    const risk = preferredCandidate(DEMO_CANDIDATES, 'risk');
    expect(fuel).not.toBeNull();
    expect(time).not.toBeNull();
    expect(risk).not.toBeNull();
    if (!fuel || !time || !risk) return;

    // Propellant buys a cheaper burn at the cost of a slower transfer, and time the reverse.
    expect(fuel.deltaVMs).toBeLessThan(time.deltaVMs);
    expect(time.transferHours).toBeLessThan(fuel.transferHours);
    // Risk picks the lowest risk index in the whole set, not merely a low-ish one.
    const lowestRisk = Math.min(...DEMO_CANDIDATES.map((entry) => entry.riskIndex));
    expect(risk.riskIndex).toBe(lowestRisk);
    // Propellant reserve falls as the burn grows, so the fuel pick must hold the larger reserve.
    expect(fuel.fuelReservePct).toBeGreaterThan(time.fuelReservePct);
  });

  it('gives each priority a distinct answer, so the control is not decorative', () => {
    expect(new Set(Object.values(EXPECTED)).size).toBe(Object.keys(EXPECTED).length);
  });

  it('selects only from points that survive the dominance test, on this fixture', () => {
    for (const priority of Object.keys(EXPECTED) as readonly DemoPriority[]) {
      expect(DEMO_FRONTIER_IDS).toContain(preferredCandidate(DEMO_CANDIDATES, priority)?.id);
    }
  });

  it('returns null for an empty set rather than a sentinel candidate', () => {
    for (const priority of Object.keys(EXPECTED) as readonly DemoPriority[]) {
      expect(preferredCandidate([], priority)).toBeNull();
    }
  });

  it('breaks a tie on input order, so repeated calls agree', () => {
    const tied = [candidate('first', 100, 10), candidate('second', 100, 10)];
    expect(preferredCandidate(tied, 'balanced')?.id).toBe('first');
    expect(preferredCandidate(tied, 'balanced')?.id).toBe('first');
  });

  it('collapses a zero-width axis instead of dividing by it', () => {
    const identical = [candidate('one', 200, 5), candidate('two', 200, 5)];
    const chosen = preferredCandidate(identical, 'fuel');
    expect(chosen?.id).toBe('one');
  });
});

/** Reads a rendered `TelemetryReadout` by its visible label, with the unit stripped off the
 * value so the assertion is about the number rather than about how it is annotated. */
function readoutValue(container: HTMLElement, label: string): string {
  const block = Array.from(container.querySelectorAll('.mk-readout')).find(
    (node) => node.querySelector('.mk-readout__label')?.textContent === label,
  );
  const value = block?.querySelector('.mk-readout__value');
  const unit = value?.querySelector('.mk-readout__unit')?.textContent ?? '';
  const text = value?.textContent ?? '';
  return unit.length > 0 ? text.slice(0, text.length - unit.length) : text;
}

function originLine(container: HTMLElement): string {
  return container.querySelector('.mk-instrument__origin')?.textContent ?? '';
}

describe('ParetoInstrument (the rendered instrument)', () => {
  it('opens on the plan the default preference selects, and says that is why', () => {
    const { container } = render(<ParetoInstrument />);
    expect(originLine(container)).toBe(
      'TRJ-10 · PARETO-OPTIMAL SOLUTION · SELECTED BY PREFERENCE',
    );
    expect(readoutValue(container, 'Delta-v')).toBe('268');
    expect(readoutValue(container, 'Transfer time')).toBe('4.8');
  });

  it('updates the readout when a candidate is selected', () => {
    const { container } = render(<ParetoInstrument />);
    fireEvent.click(screen.getByRole('button', { name: /^TRJ-01\./ }));

    expect(readoutValue(container, 'Delta-v')).toBe('138');
    expect(readoutValue(container, 'Transfer time')).toBe('13.9');
    expect(readoutValue(container, 'Recovery index')).toBe('0.34');
    expect(readoutValue(container, 'Propellant reserve')).toBe('62.4');
  });

  it('distinguishes an operator choice from a preference-driven one', () => {
    const { container } = render(<ParetoInstrument />);
    fireEvent.click(screen.getByRole('button', { name: /^TRJ-01\./ }));
    expect(originLine(container)).toBe(
      'TRJ-01 · PARETO-OPTIMAL SOLUTION · SELECTED BY OPERATOR',
    );
  });

  it('says so when the selected plan is a dominated candidate', () => {
    const { container } = render(<ParetoInstrument />);
    // TRJ-02 is dominated by TRJ-03 on both plotted axes.
    fireEvent.click(screen.getByRole('button', { name: /^TRJ-02\./ }));
    expect(originLine(container)).toBe('TRJ-02 · DOMINATED CANDIDATE · SELECTED BY OPERATOR');
    expect(readoutValue(container, 'Delta-v')).toBe('166');
  });

  it('is operable from the keyboard, not only by pointer', () => {
    const { container } = render(<ParetoInstrument />);
    const node = screen.getByRole('button', { name: /^TRJ-13\./ });
    expect(node).toHaveAttribute('tabindex', '0');
    fireEvent.keyDown(node, { key: 'Enter' });
    expect(originLine(container)).toContain('TRJ-13');
  });

  it('moves the selection when the operator preference changes', () => {
    const { container } = render(<ParetoInstrument />);
    fireEvent.click(screen.getByLabelText('Propellant'));
    expect(originLine(container)).toBe(
      'TRJ-06 · PARETO-OPTIMAL SOLUTION · SELECTED BY PREFERENCE',
    );

    fireEvent.click(screen.getByLabelText('Risk'));
    expect(originLine(container)).toBe(
      'TRJ-08 · PARETO-OPTIMAL SOLUTION · SELECTED BY PREFERENCE',
    );
  });

  it('never lets the preference change which points are on the front', () => {
    const { container } = render(<ParetoInstrument />);
    const frontierNodes = () => container.querySelectorAll('.mk-instrument__node--frontier').length;
    const before = frontierNodes();
    expect(before).toBe(DEMO_FRONTIER_IDS.length);

    for (const label of ['Propellant', 'Time', 'Risk', 'Balanced']) {
      fireEvent.click(screen.getByLabelText(label));
      expect(frontierNodes()).toBe(before);
    }
  });

  it('offers every candidate as a real control, not a decorated dot', () => {
    render(<ParetoInstrument />);
    expect(screen.getAllByRole('button', { name: /^TRJ-\d\d\./ })).toHaveLength(
      DEMO_CANDIDATES.length,
    );
  });

  it('states the split it computed, and labels the fixture as a fixture', () => {
    render(<ParetoInstrument />);
    expect(screen.getByText('8 nondominated · 6 dominated')).toBeInTheDocument();
    expect(
      screen.getByText(/Illustrative fixture data, not engine output\./),
    ).toBeInTheDocument();
  });

  it('announces the readout panel as it changes', () => {
    const { container } = render(<ParetoInstrument />);
    expect(container.querySelector('.mk-instrument__panel')).toHaveAttribute(
      'aria-live',
      'polite',
    );
  });
});
