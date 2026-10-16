import { Line } from '@react-three/drei';

import { eciToSceneExaggerated } from '../coordinates';
import type { MissionLegView } from '../../types/engine';

const STATUS_COLOR: Record<MissionLegView['status'], string> = {
  completed: '#6b7280',
  current: '#ffb84d',
  upcoming: '#4fa8ff',
};

export interface MissionLinkProps {
  readonly leg: MissionLegView;
  readonly exaggeration?: number;
}

/** One mission leg, rendered as a straight connector between its two REAL endpoints --
 * brief §44/§77: proving mission/route data reaches the browser, not a physically accurate
 * transfer arc (exact mid-leg position is not modelled anywhere in the engine either,
 * `DYNAMIC_REPLANNING.md` §13.2 -- so a straight line is the honest choice, not a
 * simplification hidden from the viewer). */
export function MissionLink({ leg, exaggeration = 1 }: MissionLinkProps) {
  const from = eciToSceneExaggerated(leg.fromPosition, exaggeration);
  const to = eciToSceneExaggerated(leg.toPosition, exaggeration);
  return (
    <Line
      points={[from, to]}
      color={STATUS_COLOR[leg.status]}
      lineWidth={leg.status === 'current' ? 4 : 2}
      dashed={leg.status === 'upcoming'}
      dashSize={0.03}
      gapSize={0.02}
      transparent
      opacity={leg.status === 'completed' ? 0.3 : leg.status === 'current' ? 1 : 0.75}
    />
  );
}
