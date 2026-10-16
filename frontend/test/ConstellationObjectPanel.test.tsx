import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { ConstellationObjectPanel } from '../src/components/ConstellationObjectPanel';
import type { VisualObjectView } from '../src/types/engine';

const OBJ: VisualObjectView = {
  id: 'SAT-0042',
  name: 'SAT-0042',
  status: 'NOMINAL',
  position: { x: 6878.137, y: 0, z: 0 },
  altitudeKm: 500,
  inclinationDeg: 51.6,
  raanDeg: 120.5,
  phaseDeg: 33.3,
  band: 'LEO',
};

describe('ConstellationObjectPanel', () => {
  it('derives semi-major axis and orbital period from the real altitude', () => {
    render(<ConstellationObjectPanel object={OBJ} onClose={() => {}} />);
    // a = 6378.137 + 500 = 6878 km
    expect(screen.getByText('6878 km')).toBeInTheDocument();
    // T = 2*pi*sqrt(a^3/mu) = 5677 s = 94.6 min -- the textbook LEO period
    // `docs/VIVA_GUIDE.md` already lists as a validated number.
    expect(screen.getByText('94.6 min')).toBeInTheDocument();
  });

  it('labels the object as synthetic rather than implying it is a real satellite', () => {
    render(<ConstellationObjectPanel object={OBJ} onClose={() => {}} />);
    expect(screen.getByText('SYNTHETIC VISUALIZATION OBJECT')).toBeInTheDocument();
  });

  it('says a value is unavailable instead of fabricating one', () => {
    render(<ConstellationObjectPanel object={OBJ} onClose={() => {}} />);
    expect(screen.getAllByText(/Unavailable from current simulation model/).length).toBeGreaterThan(0);
  });

  it('shows the real orbital elements the engine reported', () => {
    render(<ConstellationObjectPanel object={OBJ} onClose={() => {}} />);
    expect(screen.getByText('51.60°')).toBeInTheDocument();
    expect(screen.getByText('120.50°')).toBeInTheDocument();
  });
});
